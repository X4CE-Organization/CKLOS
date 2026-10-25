#include "gui.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"

#include "font8x8_basic.h"

#define GUI_WIDTH 320
#define GUI_HEIGHT 200
#define WINDOW_X 4
#define WINDOW_Y 18
#define WINDOW_WIDTH 312
#define WINDOW_HEIGHT 176
#define TEXT_X 10
#define TEXT_Y 42
#define TEXT_COLUMNS 37
#define TEXT_ROWS 17
#define STATUS_Y 190

static uint8_t *framebuffer = (uint8_t *)0xA0000;
static uint8_t foreground = 7;
static uint8_t background = 0;
static uint32_t row;
static uint32_t column;
static int32_t cursor_x = 160;
static int32_t cursor_y = 100;
static uint8_t cursor_background[16 * 16];
static bool cursor_saved;
static uint8_t active_panel;

static void pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x < GUI_WIDTH && y < GUI_HEIGHT) {
        framebuffer[y * GUI_WIDTH + x] = color;
    }
}

static void fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color) {
    for (uint32_t py = y; py < y + height && py < GUI_HEIGHT; py++) {
        memset(framebuffer + py * GUI_WIDTH + x, color, width);
    }
}

static void text_at(uint32_t x, uint32_t y, const char *text, uint8_t color) {
    while (*text) {
        uint8_t character = (uint8_t)*text++;
        if (character >= 128) {
            character = '?';
        }
        for (uint32_t glyph_y = 0; glyph_y < 8; glyph_y++) {
            uint8_t bits = (uint8_t)font8x8_basic[character][glyph_y];
            for (uint32_t glyph_x = 0; glyph_x < 8; glyph_x++) {
                if (bits & (1U << glyph_x)) {
                    pixel(x + glyph_x, y + glyph_y, color);
                }
            }
        }
        x += 8;
    }
}

static void chrome(void) {
    fill(0, 0, GUI_WIDTH, GUI_HEIGHT, 8);
    fill(0, 0, GUI_WIDTH, 16, 1);
    text_at(6, 4, "CKLOS", 15);
    fill(58, 3, 34, 11, 11);
    text_at(64, 5, "SYS", 0);
    fill(96, 3, 34, 11, 10);
    text_at(102, 5, "NET", 0);
    fill(134, 3, 34, 11, 12);
    text_at(140, 5, "CLR", 0);
    text_at(248, 4, "AMD64", 14);

    fill(WINDOW_X + 4, WINDOW_Y + 6, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    fill(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT, 7);
    fill(WINDOW_X + 2, WINDOW_Y + 2, WINDOW_WIDTH - 4, WINDOW_HEIGHT - 4, 0);
    fill(WINDOW_X + 4, WINDOW_Y + 4, WINDOW_WIDTH - 8, 14, 3);
    text_at(WINDOW_X + 8, WINDOW_Y + 7, "CKLOS Terminal - 64-bit", 0);
    text_at(WINDOW_X + 278, WINDOW_Y + 7, "_ [] X", 8);

    fill(0, STATUS_Y, GUI_WIDTH, GUI_HEIGHT - STATUS_Y, 7);
    text_at(5, STATUS_Y + 3, "MOUSE READY   PORT 22   CTRL+L", 0);
}

static void cursor_hide(void) {
    if (!cursor_saved) {
        return;
    }
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 16; x++) {
            pixel((uint32_t)cursor_x + x, (uint32_t)cursor_y + y,
                  cursor_background[y * 16 + x]);
        }
    }
    cursor_saved = false;
}

static void cursor_show(void) {
    static const char *shape[16] = {
        "#           ",
        "##          ",
        "#W#         ",
        "#WW#        ",
        "#WWW#       ",
        "#WWWW#      ",
        "#WWWWW#     ",
        "#WWWWWW#    ",
        "#WWWWWWW#   ",
        "#WWWWWWWW#  ",
        "#WWWW####   ",
        "#WW#WW#     ",
        "#W# #WW#    ",
        "##  #WW#    ",
        "#    #W#    ",
        "      ##    "
    };
    if (cursor_x > 303) {
        cursor_x = 303;
    }
    if (cursor_y > 183) {
        cursor_y = 183;
    }
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 16; x++) {
            cursor_background[y * 16 + x] =
                framebuffer[((uint32_t)cursor_y + y) * GUI_WIDTH +
                            (uint32_t)cursor_x + x];
        }
    }
    cursor_saved = true;
    for (uint32_t y = 0; shape[y][0]; y++) {
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
    if (!active_panel) {
        return;
    }
    fill(172, 126, 138, 58, 7);
    fill(174, 128, 134, 54, 0);
    fill(174, 128, 134, 11, active_panel == 1 ? 10 : 11);
    text_at(179, 130, active_panel == 1 ? "SYSTEM" : "NETWORK", 0);
    if (active_panel == 1) {
        text_at(179, 143, "CKLOS 0.5 x86-64", 14);
        text_at(179, 153, "GUI  320x200x256", 7);
        text_at(179, 163, "MOUSE  PS/2 IRQ12", 11);
        text_at(179, 173, mouse_available() ? "STATUS ONLINE" : "STATUS WAIT", 10);
    } else {
        text_at(179, 143, "RTL8139  10.0.2.15", 14);
        text_at(179, 153, "TCP PORT 22 DEV", 7);
        text_at(179, 163, "HOST 2222 -> 22", 11);
        text_at(179, 173, "ICMP  READY", 10);
    }
}

void gui_tick(void) {
    int32_t next_x = mouse_x();
    int32_t next_y = mouse_y();
    bool click = mouse_take_click();
    if (next_x != cursor_x || next_y != cursor_y || click) {
        cursor_hide();
        cursor_x = next_x;
        cursor_y = next_y;
        if (click) {
            if (cursor_y < 16 && cursor_x >= 58 && cursor_x < 92) {
                active_panel = 1;
                show_panel();
            } else if (cursor_y < 16 && cursor_x >= 96 && cursor_x < 130) {
                active_panel = 2;
                show_panel();
            } else if (cursor_y < 16 && cursor_x >= 134 && cursor_x < 168) {
                active_panel = 0;
                gui_clear();
            }
        }
        cursor_show();
    }
}

void gui_init(void) {
    row = 0;
    column = 0;
    foreground = 7;
    background = 0;
    active_panel = 0;
    cursor_saved = false;
    chrome();
    cursor_show();
    serial_write("\x1B[2J\x1B[H");
}

void gui_clear(void) {
    fill(TEXT_X, TEXT_Y, TEXT_COLUMNS * 8, TEXT_ROWS * 8, background);
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
    if (row < TEXT_ROWS) {
        return;
    }
    uint32_t bytes = TEXT_COLUMNS * 8;
    for (uint32_t line = 0; line < (TEXT_ROWS - 1) * 8; line++) {
        memmove(framebuffer + (TEXT_Y + line) * GUI_WIDTH + TEXT_X,
                framebuffer + (TEXT_Y + line + 8) * GUI_WIDTH + TEXT_X,
                bytes);
    }
    fill(TEXT_X, TEXT_Y + (TEXT_ROWS - 1) * 8, bytes, 8, background);
    row = TEXT_ROWS - 1;
}

void gui_putc(char character) {
    if (character == '\n') {
        column = 0;
        row++;
    } else if (character == '\r') {
        column = 0;
    } else if (character == '\b') {
        if (column) {
            column--;
        } else if (row) {
            row--;
            column = TEXT_COLUMNS - 1;
        }
        fill(TEXT_X + column * 8, TEXT_Y + row * 8, 8, 8, background);
    } else if (character == '\t') {
        for (uint32_t spaces = 4 - column % 4; spaces; spaces--) {
            gui_putc(' ');
        }
        return;
    } else if ((uint8_t)character >= 32) {
        uint8_t byte = (uint8_t)character;
        if (byte >= 128) {
            byte = '?';
        }
        fill(TEXT_X + column * 8, TEXT_Y + row * 8, 8, 8, background);
        for (uint32_t glyph_y = 0; glyph_y < 8; glyph_y++) {
            uint8_t bits = (uint8_t)font8x8_basic[byte][glyph_y];
            for (uint32_t glyph_x = 0; glyph_x < 8; glyph_x++) {
                if (bits & (1U << glyph_x)) {
                    pixel(TEXT_X + column * 8 + glyph_x,
                          TEXT_Y + row * 8 + glyph_y, foreground);
                }
            }
        }
        if (++column == TEXT_COLUMNS) {
            column = 0;
            row++;
        }
    }
    scroll();
}
