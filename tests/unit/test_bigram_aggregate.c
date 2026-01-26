#include "unity.h"

#include "core/bigrams.h"
#include "core/bigram_aggregate.h"

void test_bigram_aggregate_basic(void) {
    BigramCount l1_items[] = {
        {.w1 = "a", .w2 = "b", .count = 2},
        {.w1 = "b", .w2 = "c", .count = 1},
    };
    BigramCountList l1 = {.items = l1_items, .count = 2};

    BigramCount l2_items[] = {
        {.w1 = "a", .w2 = "b", .count = 3},
        {.w1 = "c", .w2 = "d", .count = 1},
    };
    BigramCountList l2 = {.items = l2_items, .count = 2};

    BigramCountList lists[] = {l1, l2};

    BigramCountList agg = aggregate_bigram_counts(lists, 2);

    TEST_ASSERT_EQUAL_UINT((unsigned)5, (unsigned)get_bigram_count(&agg, "a", "b"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&agg, "b", "c"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&agg, "c", "d"));

    free_aggregated_bigram_counts(&agg);
}
