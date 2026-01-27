#include "unity.h"

#include "core/freq.h"
#include "core/bigrams.h"
#include "view/topk.h"

void test_topk_words_order_and_truncate(void) {
    WordCount items[] = {
        {.word = "banane", .count = 2},
        {.word = "apfel",  .count = 5},
        {.word = "birne",  .count = 2},
        {.word = "zitrone",.count = 1},
    };
    WordCountList wl = {.items = items, .count = 4};

    WordCountList top = top_k_words(&wl, 3);

    TEST_ASSERT_EQUAL_UINT((unsigned)3, (unsigned)top.count);

    // count desc:
    TEST_ASSERT_EQUAL_STRING("apfel", top.items[0].word);
    TEST_ASSERT_EQUAL_UINT((unsigned)5, (unsigned)top.items[0].count);

    // tie-break for count=2: word asc => "banane" before "birne"
    TEST_ASSERT_EQUAL_STRING("banane", top.items[1].word);
    TEST_ASSERT_EQUAL_UINT((unsigned)2, (unsigned)top.items[1].count);

    TEST_ASSERT_EQUAL_STRING("birne", top.items[2].word);
    TEST_ASSERT_EQUAL_UINT((unsigned)2, (unsigned)top.items[2].count);

    free_top_k_words(&top);
}

void test_topk_bigrams_order_and_truncate(void) {
    BigramCount items[] = {
        {.w1 = "a", .w2 = "b", .count = 2},
        {.w1 = "a", .w2 = "c", .count = 2},
        {.w1 = "b", .w2 = "a", .count = 5},
        {.w1 = "x", .w2 = "y", .count = 1},
    };
    BigramCountList bl = {.items = items, .count = 4};

    BigramCountList top = top_k_bigrams(&bl, 2);

    TEST_ASSERT_EQUAL_UINT((unsigned)2, (unsigned)top.count);

    // highest first
    TEST_ASSERT_EQUAL_STRING("b", top.items[0].w1);
    TEST_ASSERT_EQUAL_STRING("a", top.items[0].w2);
    TEST_ASSERT_EQUAL_UINT((unsigned)5, (unsigned)top.items[0].count);

    // tie-break for count=2: ("a","b") before ("a","c")
    TEST_ASSERT_EQUAL_STRING("a", top.items[1].w1);
    TEST_ASSERT_EQUAL_STRING("b", top.items[1].w2);
    TEST_ASSERT_EQUAL_UINT((unsigned)2, (unsigned)top.items[1].count);

    free_top_k_bigrams(&top);
}
