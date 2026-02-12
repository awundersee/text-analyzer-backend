#ifndef FREQ_H
#define FREQ_H

#include <stddef.h>
#include "core/tokenizer.h"

/*
 * String-based word frequency representation.
 * Used in the basic (non-ID) pipeline.
 */
typedef struct {
    char *word;
    size_t count;
} WordCount;

typedef struct {
    WordCount *items;
    size_t count;
} WordCountList;

/*
 * Count word frequencies from tokens.
 * Tokens remain unchanged.
 * Implementation uses linear lookup (sufficient for small datasets).
 */
WordCountList count_words(const TokenList *tokens);

/* Release memory owned by WordCountList. */
void free_word_counts(WordCountList *list);

/* Retrieve count for a word (returns 0 if not present). */
size_t get_word_count(const WordCountList *list, const char *word);

#endif
