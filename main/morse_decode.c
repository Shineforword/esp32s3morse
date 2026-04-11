#include "morse_decode.h"

#include "app_config.h"

#include <string.h>

typedef struct {
    char ch;
    const char *morse;
} morse_entry_t;

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

    char work[MORSE_PATTERN_MAX + 4];
    strncpy(work, in, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char *w = work;
    while (*w == '/') {
        w++;
    }

    size_t olen = 0;
    const char *s = w;

    while (*s) {
        const char *word_break = strstr(s, "//");
        const char *word_end = word_break ? word_break : s + strlen(s);

        const char *t = s;
        while (t < word_end) {
            while (t < word_end && *t == '/') {
                t++;
            }
            if (t >= word_end) {
                break;
            }
            const char *slash = memchr(t, '/', (size_t)(word_end - t));
            const char *tok_end = slash ? slash : word_end;
            const size_t tok_len = (size_t)(tok_end - t);
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
