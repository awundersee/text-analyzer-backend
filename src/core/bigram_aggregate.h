#ifndef BIGRAM_AGGREGATE_H
#define BIGRAM_AGGREGATE_H

#include <stddef.h>
#include "core/bigrams.h"

/*
 * Domain-level aggregation for bigram results.
 * Merges per-page BigramCountLists into a single combined list.
 */
BigramCountList aggregate_bigram_counts(
    const BigramCountList *lists,
    size_t list_count
);

/* Release memory of an aggregated BigramCountList. */
void free_aggregated_bigram_counts(BigramCountList *list);

#endif
