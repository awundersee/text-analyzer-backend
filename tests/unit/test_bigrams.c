#include "unity.h"

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/bigrams.h"

void test_bigrams_basic(void) {
    TokenList tl = tokenize("Apfel Banane Apfel Apfel");

    BigramCountList bl = count_bigrams(&tl);

    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&bl, "apfel", "banane"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&bl, "banane", "apfel"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&bl, "apfel", "apfel"));

    free_bigram_counts(&bl);
    free_tokens(&tl);
}

void test_bigrams_do_not_bridge_over_stopwords(void) {
    TokenList raw = tokenize("Das ist ein Test und das ist nur ein Test");

    StopwordList sw = {0};
    int rc = stopwords_load(&sw, "data/stopwords_de.txt");
    TEST_ASSERT_EQUAL_INT(0, rc);

    BigramCountList bl = count_bigrams_excluding_stopwords(&raw, &sw);

    // wichtig: "test test" darf nicht entstehen, wenn Adjazenz aus Originaltext kommen soll
    TEST_ASSERT_EQUAL_UINT((unsigned)0, (unsigned)get_bigram_count(&bl, "test", "test"));

    free_bigram_counts(&bl);
    stopwords_free(&sw);
    free_tokens(&raw);
}

