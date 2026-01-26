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

void test_bigrams_with_stopwords_filtered(void) {
    TokenList tl = tokenize("Das ist ein Test und das ist nur ein Test");

    int rc = filter_stopwords(&tl, "data/stopwords_de.txt");
    TEST_ASSERT_EQUAL_INT(0, rc);

    // after stopwords: ["test", "test"] -> one bigram ("test","test")
    BigramCountList bl = count_bigrams(&tl);

    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_bigram_count(&bl, "test", "test"));

    free_bigram_counts(&bl);
    free_tokens(&tl);
}
