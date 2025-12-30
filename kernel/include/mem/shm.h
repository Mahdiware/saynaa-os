#pragma once

#include "libc/stdint.h"

// Simple shared-memory objects backed by physical pages.
// NOTE: This API is intentionally minimal for the userspace WM transition.

int shm_create(uint32_t size_bytes, uint32_t* out_id);
void* shm_map(uint32_t id);
int shm_close(uint32_t id);
