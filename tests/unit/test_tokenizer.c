// [EXTERN] Unity test framework
#include "unity.h"

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"

static void assert_tokens(TokenList tl, const char **expected, size_t n) {
    TEST_ASSERT_EQUAL_UINT((unsigned)n, (unsigned)tl.count);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_NOT_NULL(tl.items[i]);
        TEST_ASSERT_EQUAL_STRING(expected[i], tl.items[i]);
    }
}

void setUp(void) {}
void tearDown(void) {}

void test_tokenizer_g1_hallo_welt(void) {
    const char *text = "Hallo Welt";
    TokenList tl = tokenize(text);

    const char *exp[] = {"hallo", "welt"};
    assert_tokens(tl, exp, 2);

    free_tokens(&tl);
}

void test_tokenizer_g2_punctuation(void) {
    const char *text = "Hallo, Welt! Hallo... Welt? Ja: Hallo; Welt-okay.";
    TokenList tl = tokenize(text);

    const char *exp[] = {"hallo", "welt", "hallo", "welt", "ja", "hallo", "welt", "okay"};
    assert_tokens(tl, exp, 8);

    free_tokens(&tl);
}

void test_tokenizer_with_stats_counts_including_stopwords(void) {
    const char *text = "Das ist ein Test";
    TokenStats st;
    TokenList tl = tokenize_with_stats(text, &st);

    // Tokens: "das","ist","ein","test"
    TEST_ASSERT_EQUAL_UINT((unsigned)4, (unsigned)st.wordCount);

    // wordCharCount: 3 + 3 + 3 + 4 = 13
    TEST_ASSERT_EQUAL_UINT((unsigned)13, (unsigned)st.wordCharCount);

    free_tokens(&tl);
}

void test_stopwords_g3_basic(void) {
    const char *text = "Das ist ein Test und das ist nur ein Test";
    TokenList tl = tokenize(text);

    int rc = filter_stopwords(&tl, "data/stopwords_de.txt");
    TEST_ASSERT_EQUAL_INT(0, rc);


    const char *exp[] = {"test", "test"};
    assert_tokens(tl, exp, 2);

    free_tokens(&tl);
}

void test_freq_g4_basic_counts(void) {
    const char *text = "Apfel Banane Apfel Apfel Birne";
    TokenList tl = tokenize(text);

    WordCountList wl = count_words(&tl);

    TEST_ASSERT_EQUAL_UINT((unsigned)3, (unsigned)get_word_count(&wl, "apfel"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_word_count(&wl, "banane"));
    TEST_ASSERT_EQUAL_UINT((unsigned)1, (unsigned)get_word_count(&wl, "birne"));

    free_word_counts(&wl);
    free_tokens(&tl);
}

void test_aggregate_g5_basic(void);

void test_bigrams_basic(void);
void test_bigrams_with_stopwords_filtered(void);

void test_bigram_aggregate_basic(void);

void test_topk_words_order_and_truncate(void);
void test_topk_bigrams_order_and_truncate(void);

void test_parity_g1_short(void);
void test_parity_g2_punct(void);
void test_parity_g3_stopwords(void);
void test_parity_g4_repetitions(void);
void test_parity_g5_multi_page_like(void);

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tokenizer_g1_hallo_welt);
    RUN_TEST(test_tokenizer_g2_punctuation);
    RUN_TEST(test_tokenizer_with_stats_counts_including_stopwords);
    RUN_TEST(test_stopwords_g3_basic);
    RUN_TEST(test_freq_g4_basic_counts);
    RUN_TEST(test_aggregate_g5_basic);
    RUN_TEST(test_bigrams_basic);
    RUN_TEST(test_bigrams_with_stopwords_filtered);
    RUN_TEST(test_bigram_aggregate_basic);
    RUN_TEST(test_topk_words_order_and_truncate);
    RUN_TEST(test_topk_bigrams_order_and_truncate);
    RUN_TEST(test_parity_g1_short);
    RUN_TEST(test_parity_g2_punct);
    RUN_TEST(test_parity_g3_stopwords);
    RUN_TEST(test_parity_g4_repetitions);
    RUN_TEST(test_parity_g5_multi_page_like);
    return UNITY_END();
}
