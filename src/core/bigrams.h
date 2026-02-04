#ifndef BIGRAMS_H
#define BIGRAMS_H

#include <stddef.h>
#include "core/tokenizer.h"
#include "core/stopwords.h"

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

// Original: nutzt Tokens wie sie sind (aber weiterhin minlen/digits Filter, wie in bigrams.c)
BigramCountList count_bigrams(const TokenList *tokens);

// NEU: Original-Adjazenz, aber ignoriert Paare mit Stopwords/short/digits-only (kein Bridging)
BigramCountList count_bigrams_excluding_stopwords(const TokenList *tokens,
                                                  const StopwordList *sw);

// Free bigram list
void free_bigram_counts(BigramCountList *list);

// Helper: get count for a bigram (0 if not found)
size_t get_bigram_count(const BigramCountList *list, const char *w1, const char *w2);

#endif
