#include "unity.h"

#include "core/freq.h"
#include "core/aggregate.h"

void test_aggregate_g5_basic(void) {
    // list1: apfel(2), banane(1)
    WordCount list1_items[] = {
        {.word = "apfel",  .count = 2},
        {.word = "banane", .count = 1},
    };
    WordCountList list1 = {.items = list1_items, .count = 2};

    // list2: apfel(1), birne(3)
    WordCount list2_items[] = {
        {.word = "apfel", .count = 1},
        {.word = "birne", .count = 3},
    };
    WordCountList list2 = {.items = list2_items, .count = 2};

    WordCountList lists[] = {list1, list2};

    WordCountList agg = aggregate_word_counts(lists, 2);

    TEST_ASSERT_EQUAL_UINT((unsigned)3, (unsigned)get_word_count(&agg, "apfel"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_word_count(&agg, "banane"));
    TEST_ASSERT_EQUAL_UINT((unsigned)3, (unsigned)get_word_count(&agg, "birne"));

    free_aggregated_word_counts(&agg);
}
