#ifndef CKLOS_GUI_H
#define CKLOS_GUI_H

#include "types.h"
#include "memory.h"

void gui_configure(const boot_info *info);
void gui_init(void);
uint32_t gui_width(void);
uint32_t gui_height(void);
uint32_t gui_bpp(void);
bool gui_linear(void);
void gui_set_editor_mode(bool enabled);
void gui_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                   uint32_t height, uint8_t color);
void gui_draw_text(uint32_t x, uint32_t y, const char *text,
                   uint8_t foreground, uint8_t background);
void gui_draw_text_n(uint32_t x, uint32_t y, const char *text, uint32_t size,
                     uint8_t foreground, uint8_t background);
void gui_clear(void);
void gui_set_color(uint8_t foreground, uint8_t background);
uint8_t gui_color(void);
void gui_putc(char character);
void gui_tick(void);
void gui_invalidate_cursor(void);

#endif
