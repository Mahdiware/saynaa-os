#include "kernel/lib/kprintf.h"

#include "kernel/cpu/serial.h"
#include "kernel/kernel.h"
#include "kernel/lib/fb.h"
#include "kernel/lib/font.h"
#include "kernel/utils/spinlock.h"
#include "libc/string.h"

int font_scale = 1;
int pos_x = 0, pos_y = 0;
int pos_x2 = 0;

uint32_t fore_color = 0xffffffff;
uint32_t back_color = 0xff000000;

static spinlock_t kprintf_lock = SPINLOCK_INIT;
static char kprintf_buffer[4096];

#define print_char(c) vbe_print_char(c)

static void clear_screen_internal(void) {
    // Fill the framebuffer with the current background color
    uint32_t* pixels = (uint32_t*) fb.address;
    uint32_t pitch_words = fb.pitch / 4;
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            pixels[y * pitch_words + x] = back_color;
        }
    }

    // Reset cursor to top-left
    set_pos_text(0, 0);
}

void vbe_clear_screen(void) {
    clear_screen_internal();
}

void set_pos_text(int x, int y) {
    pos_x = x;
    pos_x2 = pos_x;
    pos_y = y;
}

void set_text_color(uint32_t fg, uint32_t bg) {
    fore_color = fg;
    back_color = bg;
}

void set_font_scale(int scale) {
    if (scale < 1)
        scale = 1;
    font_scale = scale;
}

void print_ch(char c) {
    int lx, ly, sx, sy;
    uint8_t* bitmap = (uint8_t*) font8x8_basic[c % 128];
    for (ly = 0; ly < GLYPH_HEIGHT; ly++) {
        uint8_t row = bitmap[ly];
        for (lx = 0; lx < GLYPH_WIDTH; lx++) {
            uint32_t color = ((row >> lx) & 1) ? fore_color : back_color;

            // scale each pixel into a block of pixels
            for (sy = 0; sy < font_scale; sy++) {
                for (sx = 0; sx < font_scale; sx++) {
                    draw_pixel(pos_x + lx * font_scale + sx, pos_y + ly * font_scale + sy, color);
                }
            }
        }
    }
}

void vbe_print_char(char c) {
    write_serial(c); // optional serial output

    if (c == '\f') { // form feed = clear screen
        clear_screen_internal();
        return;
    }

    if (c == '\n') {
        pos_y += GLYPH_HEIGHT * font_scale;
        pos_x = pos_x2;
    } else if (c == '\r') {
        pos_x = pos_x2;
    } else if (c == '\b') {
        // move back and erase previous character
        if (pos_x >= GLYPH_WIDTH * font_scale) {
            pos_x -= GLYPH_WIDTH * font_scale;

            // erase glyph block
            int lx, ly, sx, sy;
            for (ly = 0; ly < GLYPH_HEIGHT; ly++) {
                for (lx = 0; lx < GLYPH_WIDTH; lx++) {
                    for (sy = 0; sy < font_scale; sy++) {
                        for (sx = 0; sx < font_scale; sx++) {
                            draw_pixel(pos_x + lx * font_scale + sx, pos_y + ly * font_scale + sy, back_color);
                        }
                    }
                }
            }
        }
    } else {
        print_ch(c);
        pos_x += GLYPH_WIDTH * font_scale;

        // optional: automatic line wrap
        if (pos_x + GLYPH_WIDTH * font_scale >= fb.width) {
            pos_x = pos_x2; // move to start of line
            pos_y += GLYPH_HEIGHT * font_scale;

            // optional: stop at bottom of screen
            if (pos_y + GLYPH_HEIGHT * font_scale > fb.height) {
                pos_y = 0; // or scroll screen
            }
        }
    }
}

void put_string(char* s) {
    uint32_t l = strlen(s);
    for (uint32_t i = 0; i < l; i++) {
        char c = s[i];
        vbe_print_char(c);
    }
}

int kprintf(const char* fmt, ...) {
    uint32_t flags = spinlock_lock_irqsave(&kprintf_lock);

    va_list args;
    va_start(args, fmt);

    vsnprintf(kprintf_buffer, sizeof(kprintf_buffer), fmt, args);
    put_string(kprintf_buffer);

    va_end(args);
    spinlock_unlock_irqrestore(&kprintf_lock, flags);
    return 0;
}

int kserialf(const char* fmt, ...) {
    uint32_t flags = spinlock_lock_irqsave(&kprintf_lock);

    va_list args;
    va_start(args, fmt);

    vsnprintf(kprintf_buffer, sizeof(kprintf_buffer), fmt, args);
    va_end(args);

    uint32_t l = strlen(kprintf_buffer);
    for (uint32_t i = 0; i < l; i++) {
        write_serial(kprintf_buffer[i]);
    }

    spinlock_unlock_irqrestore(&kprintf_lock, flags);
    return 0;
}