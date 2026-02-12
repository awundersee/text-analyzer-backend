#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/bigrams.h"
#include "core/dict.h"

/*
 * Single bigram entry in ID-based representation.
 * key encodes (id1, id2) into one 64-bit value.
 */
typedef struct {
  uint64_t key;   // (id1 << 32) | id2
  uint32_t count;
  int used;       // open addressing marker
} BigEntry;

/*
 * Hash table for ID-based bigram counting.
 * Power-of-two capacity enables efficient masking.
 */
typedef struct {
  BigEntry *entries;
  size_t cap;   // must remain power of two
  size_t size;  // number of used entries
} IdBigrams;

/* Initialize ID-based bigram table. */
int idbigrams_init(IdBigrams *b, size_t initial_cap);

/* Release bigram table memory. */
void idbigrams_free(IdBigrams *b);

/* Increment bigram frequency for (id1, id2). */
int idbigrams_inc(IdBigrams *b, uint32_t id1, uint32_t id2);

/*
 * ID-based bigram counting stage.
 *
 * raw tokens → stopword filtering → dict (token→id)
 * → ID bigram hash table → materialized BigramCountList
 *
 * Avoids string concatenation during counting.
 */
int id_count_bigrams_excluding_stopwords(const TokenList *raw,
                                        const StopwordList *sw,
                                        Dict *dict,
                                        BigramCountList *out_bigrams);
