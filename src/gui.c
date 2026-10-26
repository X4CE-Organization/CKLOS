#include "gui.h"
#include "memory.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"
#include "unicode.h"

#include "font8x8_basic.h"

#define CURSOR_SIZE 16

static uint8_t *framebuffer = (uint8_t *)0xA0000;
static uint32_t screen_width = 320;
static uint32_t screen_height = 200;
static uint32_t screen_pitch = 320;
static uint32_t screen_bpp = 8;
static bool linear_framebuffer;
static uint8_t foreground = 7;
static uint8_t background = 0;
static uint32_t row;
static uint32_t column;
static int32_t cursor_x = 160;
static int32_t cursor_y = 100;
static uint8_t cursor_background[CURSOR_SIZE * CURSOR_SIZE * 4];
static bool cursor_saved;
static uint8_t active_panel;
static uint32_t text_x;
static uint32_t text_y;
static uint32_t text_columns;
static uint32_t text_rows;
static uint32_t status_y;
static uint32_t window_x;
static uint32_t window_y;
static uint32_t window_width;
static uint32_t window_height;
static bool editor_mode;
static uint8_t utf8_pending[4];
static uint32_t utf8_pending_length;
static uint32_t utf8_pending_expected;

static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

void gui_configure(const boot_info *info) {
    if (info && info->video_mode && info->framebuffer
        && info->width && info->height && info->pitch
        && (info->bpp == 32 || info->bpp == 16)) {
        framebuffer = (uint8_t *)(uintptr_t)info->framebuffer;
        screen_width = info->width;
        screen_height = info->height;
        screen_pitch = info->pitch;
        screen_bpp = info->bpp;
        linear_framebuffer = true;
    } else {
        framebuffer = (uint8_t *)0xA0000;
        screen_width = 320;
        screen_height = 200;
        screen_pitch = 320;
        screen_bpp = 8;
        linear_framebuffer = false;
    }
    window_x = screen_width / 48;
    if (window_x < 8) window_x = 8;
    window_y = 48;
    window_width = screen_width - window_x * 2;
    window_height = screen_height - window_y - 40;
    text_x = window_x + 16;
    text_y = window_y + 48;
    text_columns = (window_width - 32) / 8;
    text_rows = (window_height - 72) / 16;
    status_y = screen_height - 24;
}

uint32_t gui_width(void) { return screen_width; }
uint32_t gui_height(void) { return screen_height; }
uint32_t gui_bpp(void) { return screen_bpp; }
bool gui_linear(void) { return linear_framebuffer; }
void gui_set_editor_mode(bool enabled) { editor_mode = enabled; }

static void pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x >= screen_width || y >= screen_height) return;
    uint8_t *address = framebuffer + y * screen_pitch;
    if (screen_bpp == 32) {
        ((uint32_t *)address)[x] = palette[color & 15];
    } else if (screen_bpp == 16) {
        uint32_t rgb = palette[color & 15];
        uint16_t value = (uint16_t)(((rgb >> 19) & 0x1F) << 11
                                  | ((rgb >> 10) & 0x3F) << 5
                                  | ((rgb >> 3) & 0x1F));
        ((uint16_t *)address)[x] = value;
    } else {
        address[x] = color;
    }
}

void gui_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height, uint8_t color) {
    for (uint32_t py = y; py < y + height && py < screen_height; py++) {
        for (uint32_t px = x; px < x + width && px < screen_width; px++) {
            pixel(px, py, color);
        }
    }
}

static void draw_codepoint(uint32_t x, uint32_t y, uint32_t codepoint,
                           uint8_t color, uint8_t background_color) {
    if (codepoint < 128) {
        if (background_color != 0xFF) gui_fill_rect(x, y, 8, 16, background_color);
        for (uint32_t glyph_y = 0; glyph_y < 8; glyph_y++) {
            uint8_t bits = (uint8_t)font8x8_basic[codepoint][glyph_y];
            for (uint32_t glyph_x = 0; glyph_x < 8; glyph_x++) {
                if (bits & (1U << glyph_x)) pixel(x + glyph_x, y + glyph_y, color);
            }
        }
        return;
    }
    const uint8_t *glyph = unicode_glyph(codepoint);
    if (!glyph) {
        draw_codepoint(x, y, '?', color, background_color);
        return;
    }
    if (background_color != 0xFF) gui_fill_rect(x, y, 16, 16, background_color);
    for (uint32_t glyph_y = 0; glyph_y < 16; glyph_y++) {
        uint16_t bits = (uint16_t)((uint16_t)glyph[glyph_y * 2] << 8)
                      | glyph[glyph_y * 2 + 1];
        for (uint32_t glyph_x = 0; glyph_x < 16; glyph_x++) {
            if (bits & (1U << (15 - glyph_x))) {
                pixel(x + glyph_x, y + glyph_y, color);
            }
        }
    }
}

void gui_draw_text_n(uint32_t x, uint32_t y, const char *text, uint32_t size,
                     uint8_t color, uint8_t background_color) {
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t consumed;
        uint32_t codepoint = utf8_decode(text + offset, &consumed);
        if (consumed > size - offset) consumed = 1;
        draw_codepoint(x, y, codepoint, color, background_color);
        x += codepoint < 128 ? 8 : 16;
        offset += consumed;
    }
}

void gui_draw_text(uint32_t x, uint32_t y, const char *text,
                   uint8_t color, uint8_t background_color) {
    gui_draw_text_n(x, y, text, (uint32_t)strlen(text), color, background_color);
}

static void text_at(uint32_t x, uint32_t y, const char *text, uint8_t color) {
    gui_draw_text(x, y, text, color, 0xFF);
}

static void chrome(void) {
    gui_fill_rect(0, 0, screen_width, screen_height, 8);
    gui_fill_rect(0, 0, screen_width, 32, 1);
    text_at(12, 10, "CKLOS", 15);
    gui_fill_rect(96, 8, 64, 18, 11);
    text_at(108, 13, "SYS", 0);
    gui_fill_rect(168, 8, 64, 18, 10);
    text_at(180, 13, "NET", 0);
    gui_fill_rect(240, 8, 64, 18, 12);
    text_at(252, 13, "CLR", 0);
    text_at(screen_width - 96, 11, "AMD64", 14);

    gui_fill_rect(window_x + 6, window_y + 6, window_width, window_height, 0);
    gui_fill_rect(window_x, window_y, window_width, window_height, 7);
    gui_fill_rect(window_x + 2, window_y + 2, window_width - 4,
                  window_height - 4, 0);
    gui_fill_rect(window_x + 4, window_y + 4, window_width - 8, 28, 3);
    text_at(window_x + 14, window_y + 12, "CKLOS Terminal - 64-bit", 0);
    text_at(window_x + window_width - 80, window_y + 12, "_ [] X", 8);

    gui_fill_rect(0, status_y, screen_width, screen_height - status_y, 7);
    text_at(10, status_y + 8,
            linear_framebuffer ? "VBE LINEAR   MOUSE READY   CTRL+L"
                               : "VGA 13H   MOUSE READY   CTRL+L", 0);
}

static void cursor_hide(void) {
    if (!cursor_saved) return;
    for (uint32_t y = 0; y < CURSOR_SIZE; y++) {
        for (uint32_t x = 0; x < CURSOR_SIZE; x++) {
            uint32_t source = (y * CURSOR_SIZE + x)
                            * (screen_bpp >= 16 ? 4 : 1);
            uint32_t target_x = (uint32_t)cursor_x + x;
            uint32_t target_y = (uint32_t)cursor_y + y;
            if (target_x >= screen_width || target_y >= screen_height) continue;
            uint8_t *target = framebuffer + target_y * screen_pitch;
            if (screen_bpp == 32) {
                ((uint32_t *)target)[target_x] =
                    *(uint32_t *)(cursor_background + source);
            } else if (screen_bpp == 16) {
                ((uint16_t *)target)[target_x] =
                    *(uint16_t *)(cursor_background + source);
            } else {
                target[target_x] = cursor_background[source];
            }
        }
    }
    cursor_saved = false;
}

static void cursor_show(void) {
    static const char *shape[CURSOR_SIZE] = {
        "#               ",
        "##              ",
        "#W#             ",
        "#WW#            ",
        "#WWW#           ",
        "#WWWW#          ",
        "#WWWWW#         ",
        "#WWWWWW#        ",
        "#WWWWWWW#       ",
        "#WWWWWWWW#      ",
        "#WWWW#####      ",
        "#WW#WW#         ",
        "#W# #WW#        ",
        "##  #WW#        ",
        "#    #W#        ",
        "      ##        "
    };
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x > (int32_t)screen_width - CURSOR_SIZE) {
        cursor_x = (int32_t)screen_width - CURSOR_SIZE;
    }
    if (cursor_y > (int32_t)screen_height - CURSOR_SIZE) {
        cursor_y = (int32_t)screen_height - CURSOR_SIZE;
    }
    for (uint32_t y = 0; y < CURSOR_SIZE; y++) {
        for (uint32_t x = 0; x < CURSOR_SIZE; x++) {
            uint32_t target_x = (uint32_t)cursor_x + x;
            uint32_t target_y = (uint32_t)cursor_y + y;
            uint32_t target = (y * CURSOR_SIZE + x)
                            * (screen_bpp >= 16 ? 4 : 1);
            uint8_t *address = framebuffer + target_y * screen_pitch;
            if (screen_bpp == 32) {
                *(uint32_t *)(cursor_background + target) =
                    ((uint32_t *)address)[target_x];
            } else if (screen_bpp == 16) {
                *(uint16_t *)(cursor_background + target) =
                    ((uint16_t *)address)[target_x];
            } else {
                cursor_background[target] = address[target_x];
            }
        }
    }
    cursor_saved = true;
    for (uint32_t y = 0; y < CURSOR_SIZE; y++) {
        for (uint32_t x = 0; shape[y][x]; x++) {
            if (shape[y][x] == '#') {
                pixel((uint32_t)cursor_x + x, (uint32_t)cursor_y + y, 0);
            } else if (shape[y][x] == 'W') {
                pixel((uint32_t)cursor_x + x, (uint32_t)cursor_y + y, 15);
            }
        }
    }
}

static void show_panel(void) {
    if (!active_panel) return;
    uint32_t panel_width = 300;
    uint32_t panel_x = screen_width - panel_width - 24;
    uint32_t panel_y = 72;
    gui_fill_rect(panel_x, panel_y, panel_width, 110, 7);
    gui_fill_rect(panel_x + 2, panel_y + 2, panel_width - 4, 106, 0);
    gui_fill_rect(panel_x + 2, panel_y + 2, panel_width - 4, 20,
                  active_panel == 1 ? 10 : 11);
    text_at(panel_x + 12, panel_y + 8,
            active_panel == 1 ? "SYSTEM" : "NETWORK", 0);
    if (active_panel == 1) {
        text_at(panel_x + 12, panel_y + 32, "CKLOS 0.11 x86-64", 14);
        text_at(panel_x + 12, panel_y + 48,
                linear_framebuffer ? "VBE 1024x768x32" : "VGA 320x200x256", 7);
        text_at(panel_x + 12, panel_y + 64, "MOUSE  PS/2 IRQ12", 11);
        text_at(panel_x + 12, panel_y + 80,
                mouse_available() ? "STATUS ONLINE" : "STATUS WAIT", 10);
    } else {
        text_at(panel_x + 12, panel_y + 32, "RTL8139  10.0.2.15", 14);
        text_at(panel_x + 12, panel_y + 48, "TCP PORT 22 DEV", 7);
        text_at(panel_x + 12, panel_y + 64, "HOST 2222 -> 22", 11);
        text_at(panel_x + 12, panel_y + 80, "ICMP READY", 10);
    }
}

void gui_tick(void) {
    int32_t next_x = mouse_x();
    int32_t next_y = mouse_y();
    bool click = editor_mode ? false : mouse_take_click();
    if (next_x == cursor_x && next_y == cursor_y && !click) return;
    cursor_hide();
    cursor_x = next_x;
    cursor_y = next_y;
    if (click && cursor_y < 32) {
        if (cursor_x >= 96 && cursor_x < 160) active_panel = 1;
        else if (cursor_x >= 168 && cursor_x < 232) active_panel = 2;
        else if (cursor_x >= 240 && cursor_x < 304) {
            active_panel = 0;
            gui_clear();
        }
        show_panel();
    }
    cursor_show();
}

void gui_init(void) {
    row = 0;
    column = 0;
    foreground = 7;
    background = 0;
    active_panel = 0;
    cursor_saved = false;
    cursor_x = (int32_t)screen_width / 2;
    cursor_y = (int32_t)screen_height / 2;
    mouse_set_bounds((int32_t)screen_width, (int32_t)screen_height);
    chrome();
    cursor_show();
    serial_write("\x1B[2J\x1B[H");
}

void gui_clear(void) {
    gui_fill_rect(text_x, text_y, text_columns * 8, text_rows * 8, background);
    row = 0;
    column = 0;
    serial_write("\x1B[2J\x1B[H");
}

void gui_set_color(uint8_t fg, uint8_t bg) {
    foreground = fg;
    background = bg;
}

uint8_t gui_color(void) {
    return foreground | background << 4;
}

static void scroll(void) {
    if (row < text_rows) return;
    for (uint32_t line = 0; line < (text_rows - 1) * 16; line++) {
        for (uint32_t x = 0; x < text_columns * 8; x++) {
            uint32_t source_x = text_x + x;
            uint32_t source_y = text_y + line + 16;
            uint32_t target_y = text_y + line;
            uint8_t *source = framebuffer + source_y * screen_pitch;
            uint8_t *target = framebuffer + target_y * screen_pitch;
            if (screen_bpp == 32) {
                ((uint32_t *)target)[source_x] =
                    ((uint32_t *)source)[source_x];
            } else if (screen_bpp == 16) {
                ((uint16_t *)target)[source_x] =
                    ((uint16_t *)source)[source_x];
            } else {
                target[source_x] = source[source_x];
            }
        }
    }
    gui_fill_rect(text_x, text_y + (text_rows - 1) * 16,
                  text_columns * 8, 16, background);
    row = text_rows - 1;
}

void gui_putc(char character) {
    uint8_t byte = (uint8_t)character;
    if (utf8_pending_length) {
        utf8_pending[utf8_pending_length++] = byte;
        if (utf8_pending_length == utf8_pending_expected) {
            uint32_t consumed;
            uint32_t codepoint = utf8_decode((const char *)utf8_pending, &consumed);
            uint32_t cells = unicode_width(codepoint);
            gui_fill_rect(text_x + column * 8, text_y + row * 16,
                          cells * 8, 16, background);
            draw_codepoint(text_x + column * 8, text_y + row * 16,
                           codepoint, foreground, background);
            column += cells;
            utf8_pending_length = 0;
            if (column >= text_columns) {
                column = 0;
                row++;
            }
            scroll();
        }
        return;
    }
    if (byte >= 0xC0) {
        utf8_pending[0] = byte;
        utf8_pending_length = 1;
        utf8_pending_expected = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : 2;
        return;
    }
    if (character == '\n') {
        column = 0;
        row++;
    } else if (character == '\r') {
        column = 0;
    } else if (character == '\b') {
        if (column) column--;
        else if (row) {
            row--;
            column = text_columns - 1;
        }
        gui_fill_rect(text_x + column * 8, text_y + row * 16, 8, 16, background);
    } else if (character == '\t') {
        for (uint32_t spaces = 4 - column % 4; spaces; spaces--) {
            gui_putc(' ');
        }
        return;
    } else if (byte >= 32) {
        if (byte >= 128) byte = '?';
        gui_fill_rect(text_x + column * 8, text_y + row * 16, 8, 16, background);
        for (uint32_t glyph_y = 0; glyph_y < 8; glyph_y++) {
            uint8_t bits = (uint8_t)font8x8_basic[byte][glyph_y];
            for (uint32_t glyph_x = 0; glyph_x < 8; glyph_x++) {
                if (bits & (1U << glyph_x)) {
                    pixel(text_x + column * 8 + glyph_x,
                          text_y + row * 16 + glyph_y, foreground);
                }
            }
        }
        if (++column == text_columns) {
            column = 0;
            row++;
        }
    }
    scroll();
}
