#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>
#include "core/freq.h"

/*
 * Domain-level aggregation for word frequencies.
 * Merges per-page WordCountLists into a single combined list.
 */
WordCountList aggregate_word_counts(
    const WordCountList *lists,
    size_t list_count
);

/* Release memory of an aggregated WordCountList. */
void free_aggregated_word_counts(WordCountList *list);

#endif
