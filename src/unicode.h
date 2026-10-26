#ifndef CKLOS_UNICODE_H
#define CKLOS_UNICODE_H

#include "types.h"

bool unicode_init(const char *font_path);
const uint8_t *unicode_glyph(uint32_t codepoint);
uint32_t unicode_width(uint32_t codepoint);
uint32_t utf8_decode(const char *text, uint32_t *consumed);
uint32_t utf8_encode(uint32_t codepoint, uint8_t output[4]);
uint32_t utf8_previous(const char *text, uint32_t offset);

#endif
