#ifndef TOPK_H
#define TOPK_H

#include <stddef.h>

#include "core/freq.h"
#include "core/bigrams.h"

/*
 * Top-K view layer.
 *
 * These functions:
 * - take already aggregated lists (domain or per-page),
 * - sort them by primary metric (count DESC),
 * - apply deterministic tie-breaking (lexicographic ASC),
 * - return a NEW deep-copied list limited to k elements.
 *
 * Important:
 * - k == 0 is interpreted by the caller as "FULL" and therefore
 *   typically passed as list->count.
 * - Original input lists remain unchanged.
 */

// Words: sort by count desc, then word asc.
WordCountList top_k_words(const WordCountList *list, size_t k);

// Bigrams: sort by count desc, then w1 asc, then w2 asc.
BigramCountList top_k_bigrams(const BigramCountList *list, size_t k);

// Free helpers (mirror free_word_counts / free_bigram_counts).
void free_top_k_words(WordCountList *list);
void free_top_k_bigrams(BigramCountList *list);

#endif
