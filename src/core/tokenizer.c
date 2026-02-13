#include "core/tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Defines token boundaries for the string-based pipeline.
 * Splits on whitespace and ASCII punctuation (G2 requirement).
 */
static int is_split_char(unsigned char c) {
    return isspace(c) || ispunct(c);
}

/* Detects common UTF-8 dash variants (– — ―).
 * Treated as hard token boundaries to avoid merged tokens.
 */
static size_t utf8_dash_len(const unsigned char *s, size_t remaining) {
    if (!s || remaining < 3) return 0;
    if (s[0] == 0xE2 && s[1] == 0x80) {
        if (s[2] == 0x94 || s[2] == 0x93 || s[2] == 0x95) return 3;
    }
    return 0;
}

static size_t utf8_strlen_n(const char *s, size_t n) {
    size_t count = 0;
    for (size_t i = 0; s && i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) != 0x80) count++;
    }
    return count;
}

TokenList tokenize_with_stats(const char *text, TokenStats *stats) {
    TokenList out = (TokenList){0};

    /* Tokenizer is the first processing stage.
     * Stats include stopwords (filtering happens later).
     */
    if (stats) { stats->wordCount = 0; stats->wordCharCount = 0; }
    if (!text) return out;

    size_t len = strlen(text);

    /* Pass 1: count tokens (no allocations yet).
     * Allows exact-sized allocation for token array.
     */
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

        size_t start = i;

        while (i < len) {
            size_t d = utf8_dash_len((const unsigned char *)&text[i], len - i);
            if (d > 0) break;
            if (is_split_char((unsigned char)text[i])) break;
            i++;
        }

        size_t tlen = i - start;
        if (utf8_strlen_n(&text[start], tlen) >= 2) count++;

    }

    if (count == 0) return out;

    out.items = (char **)calloc(count, sizeof(char *));
    if (!out.items) return (TokenList){0};
    out.count = count;

    /* Pass 2: extract tokens and normalize to lowercase.
     * Allocation happens per token.
     */
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

        size_t ulen = utf8_strlen_n(&text[start], tlen);
        if (ulen < 2) continue;

        char *tok = (char *)malloc(tlen + 1);
        if (!tok) {
            /* Allocation failure aborts tokenization safely. */
            out.count = ti;
            free_tokens(&out);
            return (TokenList){0};
        }

        for (size_t k = 0; k < tlen; k++) {
            unsigned char c = (unsigned char)text[start + k];
            /* ASCII-only lowercase normalization. */
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            tok[k] = (char)c;
        }
        tok[tlen] = '\0';

        out.items[ti++] = tok;

        if (stats) {
            stats->wordCount += 1;
            stats->wordCharCount += ulen;
        }
    }

    out.count = ti;
    return out;
}

/* Convenience wrapper for the default string-based pipeline. */
TokenList tokenize(const char *text) {
    return tokenize_with_stats(text, NULL);
}

/* Releases all memory allocated during tokenization. */
void free_tokens(TokenList *list) {
    if (!list || !list->items) return;

    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);

    list->items = NULL;
    list->count = 0;
}
