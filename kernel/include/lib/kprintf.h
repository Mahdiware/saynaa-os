#pragma once

#include "libc/stdint.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"

void vbe_print_char(char);
void vbe_clear_screen(void);
void set_text_color(uint32_t fg, uint32_t bg);
void set_font_scale(int scale);
void put_string(char*);
void set_pos_text(int x, int y);
int kprintf(const char* fmt, ...);