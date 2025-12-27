#pragma once

// Small helpers for building simple shells/CLIs.

#include "libc/stdint.h"

// Splits 'line' in-place on ASCII whitespace into argv[].
// - Collapses multiple spaces/tabs.
// - Writes NUL terminators into 'line'.
// - Sets argv[argc] = NULL.
// Returns argc.
int split_args(char* line, char* argv[], int max_args);
