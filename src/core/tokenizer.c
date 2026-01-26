#include "core/tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_split_char(unsigned char c) {
    // G2: split on whitespace OR ASCII punctuation
    return isspace(c) || ispunct(c);
}

static size_t utf8_dash_len(const unsigned char *s, size_t remaining) {
    if (!s || remaining < 3) return 0;
    if (s[0] == 0xE2 && s[1] == 0x80) {
        if (s[2] == 0x94 || s[2] == 0x93 || s[2] == 0x95) return 3;
    }
    return 0;
}

TokenList tokenize_with_stats(const char *text, TokenStats *stats) {
    TokenList out = (TokenList){0};
    if (stats) { stats->wordCount = 0; stats->wordCharCount = 0; }
    if (!text) return out;

    size_t len = strlen(text);

    // 1) first pass: count tokens (wie bisher)
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i], len - i);
            if (d > 0) i += d;
            else if (is_split_char((unsigned char)text[i])) i++;
            else break;
        }
        if (i >= len) break;

        count++;

        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i], len - i);
            if (d > 0) break;
            if (is_split_char((unsigned char)text[i])) break;
            i++;
        }
    }

    if (count == 0) return out;

    out.items = (char **)calloc(count, sizeof(char *));
    if (!out.items) return (TokenList){0};
    out.count = count;

    // 2) second pass: extract tokens + stats
    i = 0;
    size_t ti = 0;

    while (i < len && ti < count) {
        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i], len - i);
            if (d > 0) i += d;
            else if (is_split_char((unsigned char)text[i])) i++;
            else break;
        }
        if (i >= len) break;

        size_t start = i;

        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i], len - i);
            if (d > 0) break;
            if (is_split_char((unsigned char)text[i])) break;
            i++;
        }

        size_t end = i;
        size_t tlen = end - start;

        char *tok = (char *)malloc(tlen + 1);
        if (!tok) {
            out.count = ti;
            free_tokens(&out);
            return (TokenList){0};
        }

        for (size_t k = 0; k < tlen; k++) {
            unsigned char c = (unsigned char)text[start + k];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            tok[k] = (char)c;
        }
        tok[tlen] = '\0';

        out.items[ti++] = tok;

        if (stats) {
            stats->wordCount += 1;
            stats->wordCharCount += tlen;
        }
    }

    out.count = ti;
    return out;
}

TokenList tokenize(const char *text) {
    return tokenize_with_stats(text, NULL);
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
