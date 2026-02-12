#include "core/freq.h"

#include <stdlib.h>
#include <string.h>

/* Local helper to duplicate tokens for ownership in WordCountList. */
static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

WordCountList count_words(const TokenList *tokens) {
    WordCountList out = (WordCountList){0};
    if (!tokens || !tokens->items || tokens->count == 0) return out;

    /* String-based counting stage (baseline pipeline).
     * Uses linear lookup → O(n²) worst case, acceptable for small datasets (G4).
     */
    size_t cap = 16;
    out.items = (WordCount *)calloc(cap, sizeof(WordCount));
    if (!out.items) return (WordCountList){0};

    for (size_t i = 0; i < tokens->count; i++) {
        const char *tok = tokens->items[i];
        if (!tok || tok[0] == '\0') continue;

        /* Linear search for existing word entry. */
        size_t found = (size_t)-1;
        for (size_t j = 0; j < out.count; j++) {
            if (strcmp(out.items[j].word, tok) == 0) {
                found = j;
                break;
            }
        }

        if (found != (size_t)-1) {
            out.items[found].count++;
            continue;
        }

        /* Append new word entry (dynamic growth). */
        if (out.count == cap) {
            cap *= 2;
            WordCount *tmp = (WordCount *)realloc(out.items, cap * sizeof(WordCount));
            if (!tmp) {
                free_word_counts(&out);
                return (WordCountList){0};
            }
            /* Zero new region to keep structure consistent. */
            memset(tmp + out.count, 0, (cap - out.count) * sizeof(WordCount));
            out.items = tmp;
        }

        out.items[out.count].word = dup_cstr(tok);
        if (!out.items[out.count].word) {
            free_word_counts(&out);
            return (WordCountList){0};
        }
        out.items[out.count].count = 1;
        out.count++;
    }

    return out;
}

/* Lookup helper (linear scan). */
size_t get_word_count(const WordCountList *list, const char *word) {
    if (!list || !list->items || !word) return 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].word && strcmp(list->items[i].word, word) == 0) {
            return list->items[i].count;
        }
    }
    return 0;
}

/* Release all allocated word entries. */
void free_word_counts(WordCountList *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].word);
        list->items[i].word = NULL;
        list->items[i].count = 0;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
