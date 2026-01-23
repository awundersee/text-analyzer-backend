#ifndef FREQ_H
#define FREQ_H

#include <stddef.h>
#include "core/tokenizer.h"

// [NO EXTERN] simple word->count list (linear lookup; ok for G4)
typedef struct {
    char *word;
    size_t count;
} WordCount;

typedef struct {
    WordCount *items;
    size_t count;
} WordCountList;

// Build frequency list from tokens (tokens are not modified).
WordCountList count_words(const TokenList *tokens);

// Free frequency list.
void free_word_counts(WordCountList *list);

// Helper: get count for a word (0 if not found).
size_t get_word_count(const WordCountList *list, const char *word);

#endif
