#pragma once

#include "kernel/boot/multiboot2.h"
#include "libc/stdint.h"

typedef struct {
    uintptr_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} fb_t;

void init_fb(mb2_t* boot);
fb_t get_fb();

extern fb_t fb;

typedef struct argb {
    uint8_t a, r, g, b;
} argb_t __attribute__((packed));

uint32_t argb_to_color(argb_t*);
uint32_t vga_to_color(uint8_t);

void draw_pixel(int x, int y, uint32_t c);
void draw_line_hor(int x, int y, int w, uint32_t c);
void draw_line_ver(int x, int y, int h, uint32_t c);
