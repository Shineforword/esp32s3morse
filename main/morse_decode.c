/**
 * morse_decode.c —— 解码实现：查表 + 按 '/'、'//' 切分
 */

#include "morse_decode.h"

#include "app_config.h"

#include <string.h>

/** 单条「字母 → 摩斯」映射，供查表使用 */
typedef struct {
    char ch;              /* 解码结果：如 'A' */
    const char *morse;    /* 标准摩斯串，用 . 与 - */
} morse_entry_t;

/** 本工程支持的字母 A–Z 与数字 0–9 */
static const morse_entry_t MORSE[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
    {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
    {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
};

/**
 * 在 MORSE 表里查找与 token 完全相同的摩斯串，返回对应字符；找不到返回 0。
 * @param token  只含 '.' '-' 的片段
 * @param len    token 有效长度
 */
static char char_for_morse(const char *token, size_t len)
{
    if (len == 0 || len >= 16) {
        return 0;
    }
    char buf[16];
    memcpy(buf, token, len);
    buf[len] = '\0';
    for (size_t i = 0; i < sizeof(MORSE) / sizeof(MORSE[0]); i++) {
        if (strcmp(MORSE[i].morse, buf) == 0) {
            return MORSE[i].ch;
        }
    }
    return 0;
}

void morse_decode_pattern(const char *in, char *out, size_t out_len)
{
    out[0] = '\0';
    if (!in || !*in) {
        return;
    }

    /* 在副本上操作，避免调用方 const 缓冲区问题；+4 预留一点余量 */
    char work[MORSE_PATTERN_MAX + 4];
    strncpy(work, in, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    /* 去掉开头多余的 '/'，解析更简单 */
    char *w = work;
    while (*w == '/') {
        w++;
    }

    size_t olen = 0;
    const char *s = w;

    /* 外层：按「单词」处理，单词之间用 "//" 分隔 */
    while (*s) {
        const char *word_break = strstr(s, "//");
        const char *word_end = word_break ? word_break : s + strlen(s);

        /* 内层：在一个单词内，按单个 '/' 分成多个「字母的摩斯串」 */
        const char *t = s;
        while (t < word_end) {
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t >= word_end) {
                break;
            }
            /* 找到本字母结束位置：下一个 '/' 或单词结束 */
            const char *slash = memchr(t, '/', (size_t)(word_end - t));
            const char *tok_end = slash ? slash : word_end;
            const size_t tok_len = (size_t)(tok_end - t);
            /* 从 token 里只抽出 '.' '-'，忽略其它字符 */
            size_t j = 0;
            char tok[16];
            for (size_t k = 0; k < tok_len && j < sizeof(tok) - 1; k++) {
                if (t[k] == '.' || t[k] == '-') {
                    tok[j++] = t[k];
                }
            }
            tok[j] = '\0';

            char ch = char_for_morse(tok, j);
            if (ch && olen + 1 < out_len) {
                out[olen++] = ch;
                out[olen] = '\0';
            } else if (j > 0 && olen + 1 < out_len) {
                /* 有摩斯点划但表里没有 → 用问号标未知 */
                out[olen++] = '?';
                out[olen] = '\0';
            }
            t = tok_end;
            while (t < word_end && *t == '/') {
                t++;
            }
        }

        if (!word_break) {
            break;
        }
        /* 跳过 "//"，进入下一单词；若还有内容则在输出里加空格 */
        s = word_break + 2;
        while (*s == '/') {
            s++;
        }
        if (*s && olen + 1 < out_len) {
            out[olen++] = ' ';
            out[olen] = '\0';
        }
    }
}
