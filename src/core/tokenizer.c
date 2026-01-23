#include "core/tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_split_char(unsigned char c) {
    // G2: split on whitespace OR ASCII punctuation
    return isspace(c) || ispunct(c);
}

static size_t utf8_dash_len(const unsigned char *s) {
    // Recognize common Unicode dashes encoded as UTF-8:
    // — (U+2014) = E2 80 94
    // – (U+2013) = E2 80 93
    // ― (U+2015) = E2 80 95
    if (!s) return 0;
    if (s[0] == 0xE2 && s[1] == 0x80) {
        if (s[2] == 0x94 || s[2] == 0x93 || s[2] == 0x95) return 3;
    }
    return 0;
}

TokenList tokenize(const char *text) {
    TokenList out = (TokenList){0};
    if (!text) return out;

    size_t len = strlen(text);

    // First pass: count tokens
    size_t count = 0;
    size_t i = 0;

    while (i < len) {
        // Skip splits (ASCII + UTF-8 dashes)
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i]);
            if (d > 0) {
                i += d;
            } else if (is_split_char((unsigned char)text[i])) {
                i++;
            } else {
                break;
            }
        }

        if (i >= len) break;

        // Found start of a token
        count++;

        // Consume token until next split (ASCII + UTF-8 dashes)
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i]);
            if (d > 0) break;
            if (is_split_char((unsigned char)text[i])) break;
            i++;
        }
    }

    if (count == 0) return out;

    out.items = (char **)calloc(count, sizeof(char *));
    if (!out.items) {
        out.count = 0;
        return out;
    }
    out.count = count;

    // Second pass: extract tokens
    i = 0;
    size_t ti = 0;

    while (i < len && ti < count) {
        // Skip splits (ASCII + UTF-8 dashes)
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i]);
            if (d > 0) {
                i += d;
            } else if (is_split_char((unsigned char)text[i])) {
                i++;
            } else {
                break;
            }
        }

        if (i >= len) break;

        size_t start = i;

        // Consume token until next split (ASCII + UTF-8 dashes)
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i]);
            if (d > 0) break;
            if (is_split_char((unsigned char)text[i])) break;
            i++;
        }

        size_t end = i;
        size_t tlen = end - start;

        char *tok = (char *)malloc(tlen + 1);
        if (!tok) {
            // Clean up what we already allocated
            out.count = ti;
            free_tokens(&out);
            return (TokenList){0};
        }

        // Copy + lowercase (ASCII A-Z)
        for (size_t k = 0; k < tlen; k++) {
            unsigned char c = (unsigned char)text[start + k];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            tok[k] = (char)c;
        }
        tok[tlen] = '\0';

        out.items[ti++] = tok;
    }

    out.count = ti;
    return out;
}

void free_tokens(TokenList *list) {
    if (!list || !list->items) return;

    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);

    list->items = NULL;
    list->count = 0;
}
