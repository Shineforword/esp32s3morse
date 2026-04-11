/**
 * morse_decode.h —— 摩斯电码「字符串 → 可读文字」解码（纯逻辑，不涉及灯、网络）
 *
 * 与本工程网页约定一致：
 *   - 字母之间用单个 '/' 分隔（例：.../---/... 表示 S、O、S 三个字母）
 *   - 单词之间用 '//' 分隔（解析时会在输出里加空格）
 */
#pragma once

#include <stddef.h>

/**
 * 将一条摩斯 pattern 解码为人类可读字符串。
 *
 * @param in      输入，仅含 '.', '-', '/' 等；可为空串。
 * @param out     输出缓冲区，由调用方提供；会写入以 '\\0' 结尾的 C 字符串。
 * @param out_len out 的最大容量（含结尾 '\\0'）。超长输出会被截断在安全范围内。
 *
 * 无法识别的字母片段会输出为字符 '?'。
 */
void morse_decode_pattern(const char *in, char *out, size_t out_len);
