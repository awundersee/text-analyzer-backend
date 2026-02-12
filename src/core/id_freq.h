#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/dict.h"
#include "core/tokenizer.h"
#include "core/freq.h"

/*
 * ID-based word counting stage.
 *
 * Uses Dict to map tokens -> numeric IDs and stores frequencies
 * in an index-based structure instead of string-keyed maps.
 * This is the memory-optimized alternative pipeline.
 */
int id_count_words(const TokenList *filtered, Dict *dict, WordCountList *out_words);

/*
 * Dense frequency table indexed by (id - 1).
 * Eliminates string lookups during counting.
 */
typedef struct {
  uint32_t *counts;  // index = id - 1
  size_t cap;        // allocated capacity (number of IDs supported)
} IdFreq;

/* Initialize frequency table for ID-based pipeline. */
int idfreq_init(IdFreq *f, size_t initial_ids);

/* Release memory of frequency table. */
void idfreq_free(IdFreq *f);

/* Ensure internal capacity can store the given ID. */
int idfreq_ensure(IdFreq *f, uint32_t id);

/* Increment frequency for a given ID. */
int idfreq_inc(IdFreq *f, uint32_t id);

/* Read frequency for a given ID (0 if out of range). */
uint32_t idfreq_get(const IdFreq *f, uint32_t id);
