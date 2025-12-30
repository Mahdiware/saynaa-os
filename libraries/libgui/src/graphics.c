#include "libc/stdlib.h"
#include "libc/string.h"
#include "libgui/font.h"
#include "libgui/gui.h"

static inline uint32_t* pixel_offset(fb_t fb, uint32_t x, uint32_t y) {
    return (uint32_t*) (fb.address + (uintptr_t) y * fb.pitch + (uintptr_t) x * (fb.bpp / 8));
}

static void draw_line_low(fb_t fb, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int yi = 1;

    if (dy < 0) {
        yi = -1;
        dy = -dy;
    }

    int D = 2 * dy - dx;
    int y = y0;

    for (int x = x0; x < x1; x++) {
        draw_pixel(fb, x, y, col);

        if (D > 0) {
            y += yi;
            D -= 2 * dx;
        }

        D += 2 * dy;
    }
}

static void draw_line_high(fb_t fb, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int xi = 1;

    if (dx < 0) {
        xi = -1;
        dx = -dx;
    }

    int D = 2 * dx - dy;
    int x = x0;

    for (int y = y0; y < y1; y++) {
        draw_pixel(fb, x, y, col);

        if (D > 0) {
            x += xi;
            D -= 2 * dy;
        }

        D += 2 * dx;
    }
}

static void draw_line_horizontal(fb_t fb, int x0, int x1, int y, uint32_t col) {
    if (x0 > x1) {
        int x = x0;
        x0 = x1;
        x1 = x;
    }

    if (y < 0 || y >= (int) fb.height) {
        return;
    }

    if (x1 < 0 || x0 >= (int) fb.width) {
        return;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= (int) fb.width) {
        x1 = (int) fb.width - 1;
    }

    uint32_t* offset = pixel_offset(fb, (uint32_t) x0, (uint32_t) y);

    for (int i = 0; i <= x1 - x0; i++) {
        offset[i] = col;
    }
}

static void draw_line_vertical(fb_t fb, int x, int y0, int y1, uint32_t col) {
    if (y0 > y1) {
        int y = y0;
        y0 = y1;
        y1 = y;
    }

    if (x < 0 || x >= (int) fb.width) {
        return;
    }

    if (y1 < 0 || y0 >= (int) fb.height) {
        return;
    }

    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= (int) fb.height) {
        y1 = (int) fb.height - 1;
    }

    uint32_t* offset = pixel_offset(fb, (uint32_t) x, (uint32_t) y0);

    for (int i = 0; i <= y1 - y0; i++) {
        *offset = col;
        offset = (uint32_t*) ((uintptr_t) offset + fb.pitch);
    }
}

void draw_pixel(fb_t fb, int x, int y, uint32_t col) {
    if (x < 0 || y < 0 || x >= (int) fb.width || y >= (int) fb.height) {
        return;
    }

    uint32_t* offset = pixel_offset(fb, (uint32_t) x, (uint32_t) y);
    *offset = col;
}

void draw_rect(fb_t fb, int x, int y, int w, int h, uint32_t col) {
    if (w <= 0 || h <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x1 <= 0 || y1 <= 0 || x0 >= (int) fb.width || y0 >= (int) fb.height) {
        return;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int) fb.width) {
        x1 = (int) fb.width;
    }
    if (y1 > (int) fb.height) {
        y1 = (int) fb.height;
    }

    uint32_t* offset = pixel_offset(fb, (uint32_t) x0, (uint32_t) y0);
    int draw_w = x1 - x0;

    for (int i = 0; i < (y1 - y0); i++) {
        for (int j = 0; j < draw_w; j++) {
            offset[j] = col;
        }
        offset = (uint32_t*) ((uintptr_t) offset + fb.pitch);
    }
}

void draw_line(fb_t fb, int x0, int y0, int x1, int y1, uint32_t col) {
    if (y0 == y1) {
        draw_line_horizontal(fb, x0, x1, y0, col);
        return;
    }

    if (x0 == x1) {
        draw_line_vertical(fb, x0, y0, y1, col);
        return;
    }

    if (abs(y1 - y0) < abs(x1 - x0)) {
        if (x0 > x1) {
            draw_line_low(fb, x1, y1, x0, y0, col);
        } else {
            draw_line_low(fb, x0, y0, x1, y1, col);
        }
    } else {
        if (y0 > y1) {
            draw_line_high(fb, x1, y1, x0, y0, col);
        } else {
            draw_line_high(fb, x0, y0, x1, y1, col);
        }
    }
}

void draw_border(fb_t fb, int x, int y, int w, int h, uint32_t col) {
    if (w <= 0 || h <= 0) {
        return;
    }

    draw_line_horizontal(fb, x, x + w - 1, y, col);
    draw_line_horizontal(fb, x, x + w - 1, y + h - 1, col);
    draw_line_vertical(fb, x, y, y + h - 1, col);
    draw_line_vertical(fb, x + w - 1, y, y + h - 1, col);
}

void draw_character(fb_t fb, char c, int x, int y, uint32_t col) {
    const font_header_t* hdr = (const font_header_t*) font_psf;
    int h = hdr->height ? hdr->height : GLYPH_HEIGHT;

    uint8_t idx = (uint8_t) c;
    const uint8_t* glyph = font_psf + sizeof(font_header_t) + h * idx;

    for (int row = 0; row < h; row++) {
        uint8_t bits = glyph[row];
        for (int bit = 0; bit < GLYPH_WIDTH; bit++) {
            // PSF1 stores leftmost pixel in bit7.
            if (bits & (1u << (7 - bit))) {
                draw_pixel(fb, x + bit, y + row, col);
            }
        }
    }
}

void draw_string(fb_t fb, char* str, int x, int y, uint32_t col) {
    if (!str) {
        return;
    }

    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        draw_character(fb, str[i], x + GLYPH_WIDTH * (int) i, y, col);
    }
}

void draw_rgba(fb_t fb, uint32_t* rgba, int x, int y, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            draw_pixel(fb, x + j, y + i, rgba[i * w + j]);
        }
    }
}

void draw_rgb(fb_t fb, uint8_t* rgb, int x, int y, int w, int h) {
    if (!rgb || w <= 0 || h <= 0) {
        return;
    }

    for (int i = 0, c = 0; i < h; i++) {
        for (int j = 0; j < w; j++, c += 3) {
            uint32_t col = ((uint32_t) rgb[c] << 16) | ((uint32_t) rgb[c + 1] << 8) | (uint32_t) rgb[c + 2];
            draw_pixel(fb, x + j, y + i, col);
        }
    }
}

void draw_rgb_masked(fb_t fb, uint8_t* rgb, int x, int y, int w, int h, uint32_t mask) {
    if (!rgb || w <= 0 || h <= 0) {
        return;
    }

    for (int i = 0, c = 0; i < h; i++) {
        for (int j = 0; j < w; j++, c += 3) {
            uint32_t col = ((uint32_t) rgb[c] << 16) | ((uint32_t) rgb[c + 1] << 8) | (uint32_t) rgb[c + 2];
            if (col != mask) {
                draw_pixel(fb, x + j, y + i, col);
            }
        }
    }
}
