#pragma once

#include <stddef.h>

/* 协议：字母用 / 分隔，单词用 // 分隔。例：.../---/... 为 SOS */
void morse_decode_pattern(const char *in, char *out, size_t out_len);
