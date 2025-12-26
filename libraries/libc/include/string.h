#pragma once

#include "libc/stdint.h"

// Sets 'size' bytes of memory area 'ptr' to the given 'value'
void memset(void* ptr, int value, uint32_t size);

// Copies 'n' bytes from 'src' to 'dst'
void memcpy(void* dst, const void* src, uint32_t n);

// Returns the length of string 's' (excluding null terminator)
int strlen(const char* s);

// Compares two strings 's1' and 's2'
// Returns <0 if s1<s2, 0 if equal, >0 if s1>s2
int strcmp(const char* s1, const char* s2);

// Compares up to 'n' characters of 's1' and 's2'
int strncmp(const char* s1, const char* s2, size_t n);

// Finds the last occurrence of character 'c' in string 's'
// Returns pointer to found char or NULL if not found
char* strrchr(const char* s, int c);

// Finds the first occurrence of character 'c' in string 's'
// Returns pointer to found char or NULL if not found
char* strchr(const char* s, int c);

// Returns the length of 'string' up to 'max_len'
size_t strnlen(const char* string, size_t max_len);

// Copies string from 'src' to 'dest'
int strcpy(char* dest, const char* src);

// Copies up to 'n' characters from 'src' to 'dest'
char* strncpy(char* dest, const char* src, size_t n);

// Appends src to dest
char* strcat(char* dest, const char* src);
