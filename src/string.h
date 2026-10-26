#ifndef CKLOS_STRING_H
#define CKLOS_STRING_H

#include "types.h"

size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
int memcmp(const void *left, const void *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strchr(const char *text, int character);
char *strstr(const char *text, const char *needle);
size_t strspn(const char *text, const char *accepted);
size_t strcspn(const char *text, const char *rejected);
void *memset(void *destination, int value, size_t count);
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
int32_t parse_int(const char *text, bool *ok);
void uint_to_string(uint32_t value, char *buffer, uint32_t base);

#endif
