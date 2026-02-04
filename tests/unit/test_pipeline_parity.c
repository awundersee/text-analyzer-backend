// tests/unit/test_pipeline_parity.c
#include "unity.h"

#include <string.h>
#include <stdlib.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"

#include "app/pipeline_id.h"

// Sortier-Vergleiche für deterministischen Vergleich
static int cmp_wc(const void *a, const void *b) {
    const WordCount *x = (const WordCount *)a;
    const WordCount *y = (const WordCount *)b;
    const char *wx = x->word ? x->word : "";
    const char *wy = y->word ? y->word : "";
    int c = strcmp(wx, wy);
    if (c != 0) return c;
    return (x->count > y->count) - (x->count < y->count);
}

static int cmp_bg(const void *a, const void *b) {
    const BigramCount *x = (const BigramCount *)a;
    const BigramCount *y = (const BigramCount *)b;
    const char *x1 = x->w1 ? x->w1 : "";
    const char *y1 = y->w1 ? y->w1 : "";
    int c = strcmp(x1, y1);
    if (c != 0) return c;
    const char *x2 = x->w2 ? x->w2 : "";
    const char *y2 = y->w2 ? y->w2 : "";
    c = strcmp(x2, y2);
    if (c != 0) return c;
    return (x->count > y->count) - (x->count < y->count);
}

static void sort_words(WordCountList *l) {
    if (l && l->items && l->count > 1) qsort(l->items, l->count, sizeof(WordCount), cmp_wc);
}

static void sort_bigrams(BigramCountList *l) {
    if (l && l->items && l->count > 1) qsort(l->items, l->count, sizeof(BigramCount), cmp_bg);
}

static void assert_words_equal(WordCountList *a, WordCountList *b) {
    sort_words(a);
    sort_words(b);

    TEST_ASSERT_EQUAL_UINT((unsigned)a->count, (unsigned)b->count);
    for (size_t i = 0; i < a->count; i++) {
        TEST_ASSERT_NOT_NULL(a->items[i].word);
        TEST_ASSERT_NOT_NULL(b->items[i].word);
        TEST_ASSERT_EQUAL_STRING(a->items[i].word, b->items[i].word);
        TEST_ASSERT_EQUAL_INT(a->items[i].count, b->items[i].count);
    }
}

static void assert_bigrams_equal(BigramCountList *a, BigramCountList *b) {
    sort_bigrams(a);
    sort_bigrams(b);

    TEST_ASSERT_EQUAL_UINT((unsigned)a->count, (unsigned)b->count);
    for (size_t i = 0; i < a->count; i++) {
        TEST_ASSERT_NOT_NULL(a->items[i].w1);
        TEST_ASSERT_NOT_NULL(a->items[i].w2);
        TEST_ASSERT_NOT_NULL(b->items[i].w1);
        TEST_ASSERT_NOT_NULL(b->items[i].w2);
        TEST_ASSERT_EQUAL_STRING(a->items[i].w1, b->items[i].w1);
        TEST_ASSERT_EQUAL_STRING(a->items[i].w2, b->items[i].w2);
        TEST_ASSERT_EQUAL_INT(a->items[i].count, b->items[i].count);
    }
}

static void run_parity_case(const char *text, int include_bigrams) {
    // raw bleibt unverändert für natürliche Bigrams
    TokenList raw = tokenize(text);

    // Stopwords einmal laden
    StopwordList sw = {0};
    int sw_rc = stopwords_load(&sw, "data/stopwords_de.txt");
    TEST_ASSERT_EQUAL_INT(0, sw_rc);

    // filtered ist Kopie (für Words)
    TokenList filtered = filter_stopwords_copy(&raw, "data/stopwords_de.txt");

    // String-Referenz (core string)
    WordCountList w_str = count_words(&filtered);
    BigramCountList b_str = (BigramCountList){0};
    if (include_bigrams) {
        b_str = count_bigrams_excluding_stopwords(&raw, &sw);
    }

    // ID-Pipeline
    WordCountList w_id = (WordCountList){0};
    BigramCountList b_id = (BigramCountList){0};

    int ok = analyze_id_pipeline(
        &filtered,
        &raw,
        include_bigrams ? true : false,
        &sw,
        &w_id,
        include_bigrams ? &b_id : NULL
    );
    TEST_ASSERT_TRUE(ok);

    // Vergleich
    assert_words_equal(&w_str, &w_id);
    if (include_bigrams) assert_bigrams_equal(&b_str, &b_id);

    // cleanup
    free_tokens(&filtered);
    free_tokens(&raw);
    stopwords_free(&sw);

    free_word_counts(&w_str);
    if (include_bigrams) free_bigram_counts(&b_str);

    free_word_counts(&w_id);
    if (include_bigrams) free_bigram_counts(&b_id);
}

// -------- G1–G5 Parity --------

void test_parity_g1_short(void) {
    run_parity_case("Hallo Welt", 1);
}

void test_parity_g2_punct(void) {
    run_parity_case("Hallo, Welt! Hallo... Welt? Ja: Hallo; Welt— okay.", 1);
}

void test_parity_g3_stopwords(void) {
    run_parity_case("Das ist ein Test und das ist nur ein Test in der kleinen Form.", 1);
}

void test_parity_g4_repetitions(void) {
    run_parity_case("Apfel Apfel Apfel Apfel Apfel Apfel Apfel Banane Banane Banane Banane Banane Kirsche Kirsche Kirsche", 1);
}

void test_parity_g5_multi_page_like(void) {
    run_parity_case(
        "Heute sagt Anna: \"Apfel, Banane & Kirsche\"—doch Apfel bleibt.\n"
        "Warum? Weil Banane (reif!) besser schmeckt; Kirsche aber selten ist.\n"
        "Am Ende: Apfel! Apfel? Banane... und dann: Kirsche, Kirsche, Kirsche.\n",
        1
    );
}
