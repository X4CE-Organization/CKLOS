#include "gui.h"
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
    text_at(58, 4, "GRAPHICAL TERMINAL", 11);
    text_at(244, 4, "320x200", 14);

    fill(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT, 7);
    fill(WINDOW_X + 2, WINDOW_Y + 2, WINDOW_WIDTH - 4, WINDOW_HEIGHT - 4, 0);
    fill(WINDOW_X + 4, WINDOW_Y + 4, WINDOW_WIDTH - 8, 14, 3);
    text_at(WINDOW_X + 8, WINDOW_Y + 7, "CKLOS Terminal", 0);
    text_at(WINDOW_X + 278, WINDOW_Y + 7, "_ [] X", 8);

    fill(0, STATUS_Y, GUI_WIDTH, GUI_HEIGHT - STATUS_Y, 7);
    text_at(5, STATUS_Y + 3, "F1 HELP   CTRL+L CLEAR   PORT 22", 0);
}

void gui_init(void) {
    row = 0;
    column = 0;
    foreground = 7;
    background = 0;
    chrome();
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
