#ifndef RKS_API_H
#define RKS_API_H

typedef unsigned int rks_u32;
typedef int rks_i32;

typedef struct {
    void (*print)(const char *text);
    void (*put_char)(char character);
    void (*clear)(void);
    rks_u32 (*ticks)(void);
    void (*sleep_ms)(rks_u32 milliseconds);
    rks_i32 (*mouse_x)(void);
    rks_i32 (*mouse_y)(void);
    rks_u32 (*mouse_buttons)(void);
} rks_api;

int rks_main(const rks_api *api);

#endif
