#include "libc/stdio.h"
#include "libc/syscall.h"

#define SCANF_INPUT_MAX 256u

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static uint32_t hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return (uint32_t) (c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return 10u + (uint32_t) (c - 'a');
    }
    return 10u + (uint32_t) (c - 'A');
}

static void skip_spaces(const char** input) {
    while (**input && is_space(**input)) {
        (*input)++;
    }
}

static int scan_int(const char** input, int* out) {
    skip_spaces(input);

    int sign = 1;
    if (**input == '+' || **input == '-') {
        if (**input == '-') {
            sign = -1;
        }
        (*input)++;
    }

    if (!is_digit(**input)) {
        return 0;
    }

    int value = 0;
    while (is_digit(**input)) {
        value = value * 10 + (**input - '0');
        (*input)++;
    }

    *out = value * sign;
    return 1;
}

static int scan_uint(const char** input, unsigned int* out) {
    skip_spaces(input);

    if (!is_digit(**input)) {
        return 0;
    }

    unsigned int value = 0;
    while (is_digit(**input)) {
        value = value * 10u + (unsigned int) (**input - '0');
        (*input)++;
    }

    *out = value;
    return 1;
}

static int scan_hex(const char** input, unsigned int* out) {
    skip_spaces(input);

    if (**input == '0' && ((*input)[1] == 'x' || (*input)[1] == 'X')) {
        *input += 2;
    }

    if (!is_hex_digit(**input)) {
        return 0;
    }

    unsigned int value = 0;
    while (is_hex_digit(**input)) {
        value = (value << 4) + hex_value(**input);
        (*input)++;
    }

    *out = value;
    return 1;
}

static int scan_string(const char** input, char* out) {
    skip_spaces(input);
    if (**input == '\0') {
        return 0;
    }

    uint32_t n = 0;
    while (**input && !is_space(**input)) {
        out[n++] = **input;
        (*input)++;
    }
    out[n] = '\0';
    return 1;
}

static int scan_char(const char** input, char* out) {
    if (**input == '\0') {
        return 0;
    }
    *out = **input;
    (*input)++;
    return 1;
}

static int read_line(char* buf, size_t size) {
    if (!buf || size == 0) {
        return 0;
    }

    size_t len = 0;
    while (len + 1 < size) {
        char ch = 0;
        int ret = sys_read(0, &ch, 1);
        if (ret != 1) {
            syscall1(SYS_SLEEP, 1);
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            sys_write(1, "\n", 1);
            break;
        }

        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }

        if (ch >= ' ' && ch < 127) {
            buf[len++] = ch;
            sys_write(1, &ch, 1);
        }
    }

    buf[len] = '\0';
    return (int) len;
}

int vsscanf(const char* restrict str, const char* restrict format, va_list list) {
    if (!str || !format) {
        return -1;
    }

    const char* input = str;
    int assigned = 0;

    while (*format) {
        if (is_space(*format)) {
            while (is_space(*format)) {
                format++;
            }
            skip_spaces(&input);
            continue;
        }

        if (*format != '%') {
            if (*input != *format) {
                break;
            }
            format++;
            if (*input) {
                input++;
            }
            continue;
        }

        format++;
        if (*format == '\0') {
            break;
        }
        if (*format == '%') {
            if (*input != '%') {
                break;
            }
            format++;
            input++;
            continue;
        }

        switch (*format) {
        case 'd': {
            int* out = va_arg(list, int*);
            if (!scan_int(&input, out)) {
                return assigned;
            }
            assigned++;
            break;
        }
        case 'u': {
            unsigned int* out = va_arg(list, unsigned int*);
            if (!scan_uint(&input, out)) {
                return assigned;
            }
            assigned++;
            break;
        }
        case 'x':
        case 'X': {
            unsigned int* out = va_arg(list, unsigned int*);
            if (!scan_hex(&input, out)) {
                return assigned;
            }
            assigned++;
            break;
        }
        case 's': {
            char* out = va_arg(list, char*);
            if (!scan_string(&input, out)) {
                return assigned;
            }
            assigned++;
            break;
        }
        case 'c': {
            char* out = va_arg(list, char*);
            if (!scan_char(&input, out)) {
                return assigned;
            }
            assigned++;
            break;
        }
        default:
            return assigned;
        }

        format++;
    }

    return assigned;
}

int sscanf(const char* restrict str, const char* restrict format, ...) {
    va_list args;
    va_start(args, format);
    int r = vsscanf(str, format, args);
    va_end(args);
    return r;
}

int vscanf(const char* restrict format, va_list list) {
    char buf[SCANF_INPUT_MAX];
    (void) read_line(buf, sizeof(buf));
    return vsscanf(buf, format, list);
}

int scanf(const char* restrict format, ...) {
    va_list args;
    va_start(args, format);
    int r = vscanf(format, args);
    va_end(args);
    return r;
}
