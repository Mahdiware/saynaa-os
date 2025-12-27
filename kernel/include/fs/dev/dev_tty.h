#pragma once

#include "kernel/fs/device.h"
#include "libc/stdint.h"

#define TTYDEV_VIRTUAL_CONSOLES 4

void init_dev_tty(void);
ssize_t ttydev_read_line(uint32_t line, uint32_t offset, uint32_t size, uint8_t* buffer);
ssize_t ttydev_write_line(uint32_t line, uint32_t offset, uint32_t size, const uint8_t* buffer);
ssize_t ttydev_read_active(uint32_t offset, uint32_t size, uint8_t* buffer);
ssize_t ttydev_write_active(uint32_t offset, uint32_t size, const uint8_t* buffer);
void ttydev_set_active_line(uint32_t line);
uint32_t ttydev_active_line(void);

