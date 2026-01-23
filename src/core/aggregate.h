#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>
#include "core/freq.h"

// [NO EXTERN]
// Aggregates multiple WordCountLists into a single WordCountList.
WordCountList aggregate_word_counts(
    const WordCountList *lists,
    size_t list_count
);

// Free helper (optional, but symmetry is nice)
void free_aggregated_word_counts(WordCountList *list);

#endif
