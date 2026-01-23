#ifndef BIGRAM_AGGREGATE_H
#define BIGRAM_AGGREGATE_H

#include <stddef.h>
#include "core/bigrams.h"

// [NO EXTERN]
// Aggregates multiple BigramCountLists into a single BigramCountList.
BigramCountList aggregate_bigram_counts(
    const BigramCountList *lists,
    size_t list_count
);

// Free helper for aggregated list
void free_aggregated_bigram_counts(BigramCountList *list);

#endif
