#include "core/bigrams.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Local helper: output owns its own copies of token strings. */
static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

/* Compares a stored bigram entry against (w1,w2). */
static int bigram_equals(const BigramCount *b, const char *w1, const char *w2) {
    return b && b->w1 && b->w2 && w1 && w2 &&
           strcmp(b->w1, w1) == 0 && strcmp(b->w2, w2) == 0;
}

static int is_all_digits(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return 0;
    }
    return 1;
}

/* Shared token validity rules for bigram counting.
 * Matches the filtering stage (minlen, digits-only, stopwords).
 */
static int token_should_be_ignored(const char *tok, const StopwordList *sw) {
    if (!tok || tok[0] == '\0') return 1;

    /* Min length guard to avoid noisy tokens ("e", ...). */
    if (strlen(tok) < 2) return 1;

    /* Drop numbers-only tokens ("2025", "6", ...). */
    if (is_all_digits(tok)) return 1;

    /* Stopword filtering is optional depending on pipeline stage. */
    if (sw && stopwords_contains(sw, tok)) return 1;

    return 0;
}

BigramCountList count_bigrams(const TokenList *tokens) {
    BigramCountList out = (BigramCountList){0};
    if (!tokens || !tokens->items || tokens->count < 2) return out;

    /* Baseline bigram stage (string pipeline): adjacency over raw tokens. */
    size_t cap = 16;
    out.items = (BigramCount *)calloc(cap, sizeof(BigramCount));
    if (!out.items) return (BigramCountList){0};

    for (size_t i = 0; i + 1 < tokens->count; i++) {
        const char *w1 = tokens->items[i];
        const char *w2 = tokens->items[i + 1];
        if (!w1 || !w2 || w1[0] == '\0' || w2[0] == '\0') continue;

        /* Keep token-quality consistent with later stages. */
        if (strlen(w1) < 2 || strlen(w2) < 2) continue;
        if (is_all_digits(w1) || is_all_digits(w2)) continue;

        /* Linear lookup → OK for small datasets; ID pipeline replaces this. */
        size_t found = (size_t)-1;
        for (size_t j = 0; j < out.count; j++) {
            if (bigram_equals(&out.items[j], w1, w2)) {
                found = j;
                break;
            }
        }

        if (found != (size_t)-1) {
            out.items[found].count++;
            continue;
        }

        /* Append new bigram entry (dynamic growth). */
        if (out.count == cap) {
            cap *= 2;
            BigramCount *tmp = (BigramCount *)realloc(out.items, cap * sizeof(BigramCount));
            if (!tmp) {
                free_bigram_counts(&out);
                return (BigramCountList){0};
            }
            memset(tmp + out.count, 0, (cap - out.count) * sizeof(BigramCount));
            out.items = tmp;
        }

        out.items[out.count].w1 = dup_cstr(w1);
        out.items[out.count].w2 = dup_cstr(w2);
        if (!out.items[out.count].w1 || !out.items[out.count].w2) {
            free_bigram_counts(&out);
            return (BigramCountList){0};
        }
        out.items[out.count].count = 1;
        out.count++;
    }

    return out;
}

BigramCountList count_bigrams_excluding_stopwords(const TokenList *tokens,
                                                  const StopwordList *sw) {
    BigramCountList out = (BigramCountList){0};
    if (!tokens || !tokens->items || tokens->count < 2) return out;

    /* Stopword-aware bigrams: maintains original adjacency.
     * No bridging: pairs containing dropped tokens are skipped.
     */
    size_t cap = 16;
    out.items = (BigramCount *)calloc(cap, sizeof(BigramCount));
    if (!out.items) return (BigramCountList){0};

    for (size_t i = 0; i + 1 < tokens->count; i++) {
        const char *w1 = tokens->items[i];
        const char *w2 = tokens->items[i + 1];

        if (token_should_be_ignored(w1, sw) || token_should_be_ignored(w2, sw)) {
            continue;
        }

        /* Linear lookup → OK for small datasets; ID pipeline replaces this. */
        size_t found = (size_t)-1;
        for (size_t j = 0; j < out.count; j++) {
            if (bigram_equals(&out.items[j], w1, w2)) {
                found = j;
                break;
            }
        }

        if (found != (size_t)-1) {
            out.items[found].count++;
            continue;
        }

        if (out.count == cap) {
            cap *= 2;
            BigramCount *tmp = (BigramCount *)realloc(out.items, cap * sizeof(BigramCount));
            if (!tmp) {
                free_bigram_counts(&out);
                return (BigramCountList){0};
            }
            memset(tmp + out.count, 0, (cap - out.count) * sizeof(BigramCount));
            out.items = tmp;
        }

        out.items[out.count].w1 = dup_cstr(w1);
        out.items[out.count].w2 = dup_cstr(w2);
        if (!out.items[out.count].w1 || !out.items[out.count].w2) {
            free_bigram_counts(&out);
            return (BigramCountList){0};
        }

        out.items[out.count].count = 1;
        out.count++;
    }

    return out;
}

/* Lookup helper (linear scan). */
size_t get_bigram_count(const BigramCountList *list, const char *w1, const char *w2) {
    if (!list || !list->items || !w1 || !w2) return 0;
    for (size_t i = 0; i < list->count; i++) {
        if (bigram_equals(&list->items[i], w1, w2)) return list->items[i].count;
    }
    return 0;
}

/* Release all allocated bigram entries. */
void free_bigram_counts(BigramCountList *list) {
    if (!list || !list->items) return;

    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].w1);
        free(list->items[i].w2);
        list->items[i].w1 = NULL;
        list->items[i].w2 = NULL;
        list->items[i].count = 0;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
