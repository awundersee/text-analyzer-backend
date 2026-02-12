#ifndef BIGRAMS_H
#define BIGRAMS_H

#include <stddef.h>
#include "core/tokenizer.h"
#include "core/stopwords.h"

/*
 * String-based bigram frequency representation.
 * Baseline bigram counting in the string pipeline.
 */
typedef struct {
    char *w1;
    char *w2;
    size_t count;
} BigramCount;

typedef struct {
    BigramCount *items;
    size_t count;
} BigramCountList;

/*
 * Count bigrams from tokens (string-based pipeline).
 * Applies the same token validity rules as bigrams.c (minlen/digits).
 */
BigramCountList count_bigrams(const TokenList *tokens);

/*
 * Count bigrams while excluding stopwords and invalid tokens.
 * No bridging: dropped tokens break adjacency (keeps original runs intact).
 */
BigramCountList count_bigrams_excluding_stopwords(const TokenList *tokens,
                                                  const StopwordList *sw);

/* Release memory owned by BigramCountList. */
void free_bigram_counts(BigramCountList *list);

/* Lookup helper (returns 0 if not present). */
size_t get_bigram_count(const BigramCountList *list, const char *w1, const char *w2);

#endif
