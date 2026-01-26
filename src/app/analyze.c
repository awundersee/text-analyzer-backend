// src/app/analyze.c
#include "app/analyze.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"
#include "core/aggregate.h"
#include "core/bigram_aggregate.h"
#include "core/topk.h"

#include "yyjson.h"

static double now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

static app_analyze_result_t fail(int status, const char *msg) {
    app_analyze_result_t r;
    r.status = status;
    r.message = msg;
    r.response_doc = NULL;
    return r;
}

static void json_add_word_list(yyjson_mut_doc *doc, yyjson_mut_val *arr, const WordCountList *list) {
    for (size_t i = 0; i < list->count; i++) {
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, obj, "word", list->items[i].word);
        yyjson_mut_obj_add_uint(doc, obj, "count", (uint64_t)list->items[i].count);
        yyjson_mut_arr_add_val(arr, obj);
    }
}

static void json_add_bigram_list(yyjson_mut_doc *doc, yyjson_mut_val *arr, const BigramCountList *list) {
    for (size_t i = 0; i < list->count; i++) {
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, obj, "w1", list->items[i].w1);
        yyjson_mut_obj_add_str(doc, obj, "w2", list->items[i].w2);
        yyjson_mut_obj_add_uint(doc, obj, "count", (uint64_t)list->items[i].count);
        yyjson_mut_arr_add_val(arr, obj);
    }
}

app_analyze_result_t app_analyze_texts(const char **texts, size_t n_texts, const app_analyze_opts_t *opts) {
    if (!texts || n_texts == 0) return fail(10, "No texts provided");
    const char *stop_path = (opts && opts->stopwords_path) ? opts->stopwords_path : "data/stopwords_de.txt";
    bool include_bigrams = (opts) ? opts->include_bigrams : true;
    bool per_page = (opts) ? opts->per_page_results : false;
    size_t topk = (opts && opts->top_k) ? opts->top_k : 10;

    // Arrays for per-page results and aggregation
    WordCountList *page_words = (WordCountList *)calloc(n_texts, sizeof(WordCountList));
    BigramCountList *page_bigrams = include_bigrams ? (BigramCountList *)calloc(n_texts, sizeof(BigramCountList)) : NULL;
    if (!page_words || (include_bigrams && !page_bigrams)) {
        free(page_words);
        free(page_bigrams);
        return fail(11, "Out of memory");
    }

    size_t chars_received = 0;
    double t0 = now_ms();

    for (size_t i = 0; i < n_texts; i++) {
        const char *t = texts[i] ? texts[i] : "";
        chars_received += strlen(t);

        TokenList tokens = tokenize(t);

        // stopwords (mutates tokens)
        int rc = filter_stopwords(&tokens, stop_path);
        if (rc != 0) {
            free_tokens(&tokens);
            // cleanup previous pages
            for (size_t k = 0; k < i; k++) free_word_counts(&page_words[k]);
            if (include_bigrams) for (size_t k = 0; k < i; k++) free_bigram_counts(&page_bigrams[k]);
            free(page_words);
            free(page_bigrams);
            return fail(20, "Stopwords filter failed (file missing or invalid?)");
        }

        page_words[i] = count_words(&tokens);
        if (include_bigrams) {
            page_bigrams[i] = count_bigrams(&tokens);
        }

        free_tokens(&tokens);
    }

    // aggregate domain
    WordCountList domain_words = aggregate_word_counts(page_words, n_texts);
    BigramCountList domain_bigrams = include_bigrams ? aggregate_bigram_counts(page_bigrams, n_texts) : (BigramCountList){0};

    // top-k
    WordCountList top_words = top_k_words(&domain_words, topk);
    BigramCountList top_bigs = include_bigrams ? top_k_bigrams(&domain_bigrams, topk) : (BigramCountList){0};

    double t1 = now_ms();

    // build response json
    yyjson_mut_doc *resp = yyjson_mut_doc_new(NULL);
    if (!resp) return fail(12, "Out of memory (response)");
    yyjson_mut_val *root = yyjson_mut_obj(resp);
    yyjson_mut_doc_set_root(resp, root);

    yyjson_mut_val *meta = yyjson_mut_obj(resp);
    yyjson_mut_obj_add_uint(resp, meta, "pagesReceived", (uint64_t)n_texts);
    yyjson_mut_obj_add_uint(resp, meta, "charsReceived", (uint64_t)chars_received);
    yyjson_mut_obj_add_real(resp, meta, "runtimeMs", (t1 - t0));
    yyjson_mut_obj_add_val(resp, root, "meta", meta);

    yyjson_mut_val *domain = yyjson_mut_obj(resp);
    yyjson_mut_obj_add_uint(resp, domain, "charCount", (uint64_t)chars_received);

    yyjson_mut_val *words_arr = yyjson_mut_arr(resp);
    json_add_word_list(resp, words_arr, &top_words);
    yyjson_mut_obj_add_val(resp, domain, "words", words_arr);

    if (include_bigrams) {
        yyjson_mut_val *bigrams_arr = yyjson_mut_arr(resp);
        json_add_bigram_list(resp, bigrams_arr, &top_bigs);
        yyjson_mut_obj_add_val(resp, domain, "bigrams", bigrams_arr);
    }

    yyjson_mut_obj_add_val(resp, root, "domainResult", domain);

    if (per_page) {
        yyjson_mut_val *pages = yyjson_mut_arr(resp);
        for (size_t i = 0; i < n_texts; i++) {
            yyjson_mut_val *p = yyjson_mut_obj(resp);
            yyjson_mut_obj_add_uint(resp, p, "index", (uint64_t)i);

            WordCountList pw_top = top_k_words(&page_words[i], topk);
            yyjson_mut_val *pw = yyjson_mut_arr(resp);
            json_add_word_list(resp, pw, &pw_top);
            yyjson_mut_obj_add_val(resp, p, "words", pw);
            free_top_k_words(&pw_top);

            if (include_bigrams) {
                BigramCountList pb_top = top_k_bigrams(&page_bigrams[i], topk);
                yyjson_mut_val *pb = yyjson_mut_arr(resp);
                json_add_bigram_list(resp, pb, &pb_top);
                yyjson_mut_obj_add_val(resp, p, "bigrams", pb);
                free_top_k_bigrams(&pb_top);
            }

            yyjson_mut_arr_add_val(pages, p);
        }
        yyjson_mut_obj_add_val(resp, root, "pageResults", pages);
    }

    // cleanup
    for (size_t i = 0; i < n_texts; i++) free_word_counts(&page_words[i]);
    free(page_words);

    if (include_bigrams) {
        for (size_t i = 0; i < n_texts; i++) free_bigram_counts(&page_bigrams[i]);
        free(page_bigrams);
    }

    free_aggregated_word_counts(&domain_words);
    free_top_k_words(&top_words);

    if (include_bigrams) {
        free_aggregated_bigram_counts(&domain_bigrams);
        free_top_k_bigrams(&top_bigs);
    }

    app_analyze_result_t ok = {0};
    ok.status = 0;
    ok.message = NULL;
    ok.response_doc = resp;
    return ok;
}
