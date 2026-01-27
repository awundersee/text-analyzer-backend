// src/app/analyze.c
#include "app/analyze.h"
#include "app/pipeline_id.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"
#include "core/aggregate.h"
#include "core/bigram_aggregate.h"
#include "view/topk.h"
#include "core/metrics.h"

#include "yyjson.h"

#define PIPELINE_THRESHOLD_CHARS (2u * 1024u * 1024u)  // 2 MB, Phase 2 Fixwert

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
        const char *w = list->items[i].word ? list->items[i].word : "";
        yyjson_mut_obj_add_strcpy(doc, obj, "word", w);
        yyjson_mut_obj_add_uint(doc, obj, "count", (uint64_t)list->items[i].count);
        yyjson_mut_arr_add_val(arr, obj);
    }
}

static void json_add_bigram_list(yyjson_mut_doc *doc, yyjson_mut_val *arr, const BigramCountList *list) {
    for (size_t i = 0; i < list->count; i++) {
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        const char *w1 = list->items[i].w1 ? list->items[i].w1 : "";
        const char *w2 = list->items[i].w2 ? list->items[i].w2 : "";
        yyjson_mut_obj_add_strcpy(doc, obj, "w1", w1);
        yyjson_mut_obj_add_strcpy(doc, obj, "w2", w2);
        yyjson_mut_obj_add_uint(doc, obj, "count", (uint64_t)list->items[i].count);
        yyjson_mut_arr_add_val(arr, obj);
    }
}

static inline double round3(double v) {
    return round(v * 1000.0) / 1000.0;
}

static const char* pipeline_requested_str(const app_analyze_opts_t *opts) {
    if (!opts) return "auto";
    switch (opts->pipeline) {
        case APP_PIPELINE_STRING: return "string";
        case APP_PIPELINE_ID:     return "id";
        case APP_PIPELINE_AUTO:
        default:                  return "auto";
    }
}

app_analyze_result_t app_analyze_pages(const app_page_t *pages, size_t n_pages, const app_analyze_opts_t *opts) {
    if (!pages || n_pages == 0) return fail(10, "No pages provided");

    const char *stop_path = (opts && opts->stopwords_path) ? opts->stopwords_path : "data/stopwords_de.txt";
    const char *domain_str = (opts && opts->domain) ? opts->domain : NULL;

    bool include_bigrams = (opts) ? opts->include_bigrams : true;
    bool per_page = (opts) ? opts->per_page_results : true;

    // Wichtig: 0 = FULL zulassen
    size_t topk = opts ? opts->top_k : 20;

    WordCountList *page_words = (WordCountList *)calloc(n_pages, sizeof(WordCountList));
    BigramCountList *page_bigrams = include_bigrams ? (BigramCountList *)calloc(n_pages, sizeof(BigramCountList)) : NULL;
    if (!page_words || (include_bigrams && !page_bigrams)) {
        free(page_words);
        free(page_bigrams);
        return fail(11, "Out of memory");
    }

    size_t chars_received = 0;
    for (size_t i = 0; i < n_pages; i++) {
        const char *t = pages[i].text ? pages[i].text : "";
        chars_received += strlen(t);
    }

    int use_id_pipeline = (chars_received >= PIPELINE_THRESHOLD_CHARS);
    if (opts) {
        if (opts->pipeline == APP_PIPELINE_STRING) use_id_pipeline = 0;
        else if (opts->pipeline == APP_PIPELINE_ID) use_id_pipeline = 1;
    }

    double t0 = now_ms();

    for (size_t i = 0; i < n_pages; i++) {
        const char *t = pages[i].text ? pages[i].text : "";

        TokenList tokens = tokenize(t);

        int rc = filter_stopwords(&tokens, stop_path);
        if (rc != 0) {
            free_tokens(&tokens);
            for (size_t k = 0; k < i; k++) free_word_counts(&page_words[k]);
            if (include_bigrams) for (size_t k = 0; k < i; k++) free_bigram_counts(&page_bigrams[k]);
            free(page_words);
            free(page_bigrams);
            return fail(20, "Stopwords filter failed (file missing or invalid?)");
        }

        if (use_id_pipeline) {
            int ok = analyze_id_pipeline(
                &tokens,
                include_bigrams,
                &page_words[i],
                include_bigrams ? &page_bigrams[i] : NULL
            );
            if (!ok) {
                free_tokens(&tokens);
                for (size_t k = 0; k < i; k++) free_word_counts(&page_words[k]);
                if (include_bigrams) for (size_t k = 0; k < i; k++) free_bigram_counts(&page_bigrams[k]);
                free(page_words);
                free(page_bigrams);
                return fail(30, "ID pipeline failed (out of memory?)");
            }
        } else {
            page_words[i] = count_words(&tokens);
            if (include_bigrams) {
                page_bigrams[i] = count_bigrams(&tokens);
            }
        }


        free_tokens(&tokens);
    }

    WordCountList domain_words = aggregate_word_counts(page_words, n_pages);
    BigramCountList domain_bigrams = include_bigrams
        ? aggregate_bigram_counts(page_bigrams, n_pages)
        : (BigramCountList){0};

    // Effective K (0 = FULL)
    size_t k_words = (topk == 0) ? domain_words.count : topk;
    WordCountList top_words = top_k_words(&domain_words, k_words);

    BigramCountList top_bigs = (BigramCountList){0};
    if (include_bigrams) {
        size_t k_bigs = (topk == 0) ? domain_bigrams.count : topk;
        top_bigs = top_k_bigrams(&domain_bigrams, k_bigs);
    }

    double t1 = now_ms();

    // build response json (Schema wie response-analyse_example.json)
    yyjson_mut_doc *resp = yyjson_mut_doc_new(NULL);
    if (!resp) return fail(12, "Out of memory (response)");
    yyjson_mut_val *root = yyjson_mut_obj(resp);
    yyjson_mut_doc_set_root(resp, root);

    yyjson_mut_val *meta = yyjson_mut_obj(resp);
    if (domain_str) {
        yyjson_mut_obj_add_strcpy(resp, meta, "domain", domain_str);
    }
    yyjson_mut_obj_add_uint(resp, meta, "pagesReceived", (uint64_t)n_pages);
    yyjson_mut_obj_add_uint(resp, meta, "charsReceived", (uint64_t)chars_received);
    double runtime_ms = round3(t1 - t0);
    yyjson_mut_obj_add_real(resp, meta, "runtimeMs", runtime_ms);

    const char *req = pipeline_requested_str(opts);
    const char *used = use_id_pipeline ? "id" : "string";
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineRequested", req);
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineUsed", used);
    yyjson_mut_obj_add_strcpy(resp, meta, "pipeline", used);

    yyjson_mut_obj_add_uint(resp, meta, "peakRssKiB", ta_peak_rss_kib());

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
        yyjson_mut_val *pages_arr = yyjson_mut_arr(resp);
        for (size_t i = 0; i < n_pages; i++) {
            yyjson_mut_val *p = yyjson_mut_obj(resp);

            // id/name/url aus Input übernehmen (mindestens id+url wie im Beispiel)
            yyjson_mut_obj_add_sint(resp, p, "id", (int64_t)pages[i].id);
            if (pages[i].name) yyjson_mut_obj_add_strcpy(resp, p, "name", pages[i].name);
            if (pages[i].url)  yyjson_mut_obj_add_strcpy(resp, p, "url", pages[i].url);

            const char *txt = pages[i].text ? pages[i].text : "";
            yyjson_mut_obj_add_uint(resp, p, "charCount", (uint64_t)strlen(txt));

            // per-page top/full
            size_t k_pw = (topk == 0) ? page_words[i].count : topk;
            WordCountList pw_top = top_k_words(&page_words[i], k_pw);
            yyjson_mut_val *pw = yyjson_mut_arr(resp);
            json_add_word_list(resp, pw, &pw_top);
            yyjson_mut_obj_add_val(resp, p, "words", pw);
            free_top_k_words(&pw_top);

            if (include_bigrams) {
                size_t k_pb = (topk == 0) ? page_bigrams[i].count : topk;
                BigramCountList pb_top = top_k_bigrams(&page_bigrams[i], k_pb);
                yyjson_mut_val *pb = yyjson_mut_arr(resp);
                json_add_bigram_list(resp, pb, &pb_top);
                yyjson_mut_obj_add_val(resp, p, "bigrams", pb);
                free_top_k_bigrams(&pb_top);
            }

            yyjson_mut_arr_add_val(pages_arr, p);
        }
        yyjson_mut_obj_add_val(resp, root, "pageResults", pages_arr);
    }

    // cleanup
    for (size_t i = 0; i < n_pages; i++) free_word_counts(&page_words[i]);
    free(page_words);

    if (include_bigrams) {
        for (size_t i = 0; i < n_pages; i++) free_bigram_counts(&page_bigrams[i]);
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
