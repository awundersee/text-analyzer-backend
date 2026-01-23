#ifndef BIGRAMS_H
#define BIGRAMS_H

#include <stddef.h>
#include "core/tokenizer.h"

// [NO EXTERN] simple bigram list (linear lookup; ok for now)
typedef struct {
    char *w1;
    char *w2;
    size_t count;
} BigramCount;

typedef struct {
    BigramCount *items;
    size_t count;
} BigramCountList;

// Build bigram frequency list from tokens (tokens are not modified).
// Creates pairs (token[i], token[i+1]) for i = 0..count-2.
BigramCountList count_bigrams(const TokenList *tokens);

// Free bigram list
void free_bigram_counts(BigramCountList *list);

// Helper: get count for a bigram (0 if not found)
size_t get_bigram_count(const BigramCountList *list, const char *w1, const char *w2);

#endif
