#ifndef TOPK_H
#define TOPK_H

#include <stddef.h>
#include "core/freq.h"
#include "core/bigrams.h"

// [NO EXTERN]
// Returns a NEW list containing the top-k words (deep copy).
// Sorting: count desc, then word asc (tie-break).
WordCountList top_k_words(const WordCountList *list, size_t k);

// [NO EXTERN]
// Returns a NEW list containing the top-k bigrams (deep copy).
// Sorting: count desc, then w1 asc, then w2 asc (tie-break).
BigramCountList top_k_bigrams(const BigramCountList *list, size_t k);

// Free helpers (symmetry with existing free_* functions)
void free_top_k_words(WordCountList *list);
void free_top_k_bigrams(BigramCountList *list);

#endif
