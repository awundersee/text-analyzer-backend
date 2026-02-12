// src/app/analyze.c
#include "app/analyze.h"
#include "app/pipeline_id.h"
#include "app/pipeline_string.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/aggregate.h"
#include "core/bigram_aggregate.h"
#include "view/topk.h"
#include "metrics/metrics.h"

#include "yyjson.h"

#define PIPELINE_THRESHOLD_CHARS (256 * 1024) // 256 KB

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

static TextMetrics compute_metrics(const char *text, TokenList tokens) {
    TextMetrics m = {0};

    if (text) {
        m.charCount = strlen(text);
    }

    m.wordCount = tokens.count;

    for (size_t i = 0; i < tokens.count; i++) {
        if (tokens.items[i]) {
            m.wordCharCount += strlen(tokens.items[i]);
        }
    }

    return m;
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

    // --- Metrics (wordCount, wordCharCount, charCount) ---
    TextMetrics domain_metrics = (TextMetrics){0};
    TextMetrics *page_metrics = (TextMetrics *)calloc(n_pages, sizeof(TextMetrics));
    if (!page_metrics) {
        free(page_words);
        free(page_bigrams);
        return fail(11, "Out of memory (page_metrics)");
    }

    double t_analyze0 = now_ms();

    StopwordList sw = {0};
    int sw_rc = stopwords_load(&sw, stop_path);
    if (sw_rc != 0) {
        free(page_words);
        free(page_bigrams);
        free(page_metrics);
        return fail(20, "Stopwords load failed (file missing or invalid?)");
    }

    for (size_t i = 0; i < n_pages; i++) {
        const char *t = pages[i].text ? pages[i].text : "";

        TokenList raw = tokenize(t);

        // gefilterte Kopie für Words/TopK
        TokenList filtered = filter_stopwords_copy(&raw, stop_path);

        // Optional: Wenn du Fehler sauber unterscheiden willst, wäre es besser,
        // filter_stopwords_copy() so zu bauen, dass es einen rc zurückgibt.
        // Für jetzt: stopwords_load wurde oben validiert, daher ist leer = ok.
        page_metrics[i] = compute_metrics(t, filtered);

        // Domain metrics wie gehabt
        domain_metrics.charCount     += page_metrics[i].charCount;
        domain_metrics.wordCount     += page_metrics[i].wordCount;
        domain_metrics.wordCharCount += page_metrics[i].wordCharCount;

        if (use_id_pipeline) {
            int ok = analyze_id_pipeline(
                &filtered,          // Words basieren auf filtered
                &raw,               // Bigrams basieren auf raw
                include_bigrams,
                &sw,                // Stopwords für bigrams-excluding
                &page_words[i],
                include_bigrams ? &page_bigrams[i] : NULL
            );
            if (!ok) {
                free_tokens(&filtered);
                free_tokens(&raw);
                for (size_t k = 0; k < i; k++) free_word_counts(&page_words[k]);
                if (include_bigrams) for (size_t k = 0; k < i; k++) free_bigram_counts(&page_bigrams[k]);
                free(page_words);
                free(page_bigrams);
                free(page_metrics);
                stopwords_free(&sw);
                return fail(30, "ID pipeline failed (out of memory?)");
            }
        } else {
            int ok = analyze_string_pipeline(
                &filtered,
                &raw,
                include_bigrams,
                &sw,
                &page_words[i],
                include_bigrams ? &page_bigrams[i] : NULL
            );      
            if (!ok) {
                free_tokens(&filtered);
                free_tokens(&raw);
                for (size_t k = 0; k < i; k++) free_word_counts(&page_words[k]);
                if (include_bigrams) for (size_t k = 0; k < i; k++) free_bigram_counts(&page_bigrams[k]);
                free(page_words);
                free(page_bigrams);
                free(page_metrics);
                stopwords_free(&sw);
                return fail(31, "String pipeline failed (out of memory?)");
            }
        }


        free_tokens(&filtered);
        free_tokens(&raw);
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

    double t_analyze1 = now_ms();
    double runtime_analyze_ms = round3(t_analyze1 - t_analyze0);

    // build response json (Schema wie response-analyse_example.json)
    yyjson_mut_doc *resp = yyjson_mut_doc_new(NULL);
    if (!resp) {
        for (size_t i = 0; i < n_pages; i++) free_word_counts(&page_words[i]);
        free(page_words);

        if (include_bigrams) {
            for (size_t i = 0; i < n_pages; i++) free_bigram_counts(&page_bigrams[i]);
            free(page_bigrams);
            free_aggregated_bigram_counts(&domain_bigrams);
            free_top_k_bigrams(&top_bigs);
        }

        free_aggregated_word_counts(&domain_words);
        free_top_k_words(&top_words);

        free(page_metrics);
        stopwords_free(&sw);
        return fail(12, "Out of memory (response)");
    }

    yyjson_mut_val *root = yyjson_mut_obj(resp);
    yyjson_mut_doc_set_root(resp, root);

    yyjson_mut_val *meta = yyjson_mut_obj(resp);
    if (domain_str) {
        yyjson_mut_obj_add_strcpy(resp, meta, "domain", domain_str);
    }
    yyjson_mut_obj_add_uint(resp, meta, "pagesReceived", (uint64_t)n_pages);
    yyjson_mut_obj_add_real(resp, meta, "runtimeMsAnalyze", runtime_analyze_ms);

    const char *req = pipeline_requested_str(opts);
    const char *used = use_id_pipeline ? "id" : "string";
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineRequested", req);
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineUsed", used);

    yyjson_mut_obj_add_uint(resp, meta, "peakRssKiB", ta_peak_rss_kib());

    yyjson_mut_obj_add_val(resp, root, "meta", meta);

    yyjson_mut_val *domain = yyjson_mut_obj(resp);
    yyjson_mut_obj_add_uint(resp, domain, "charCount", (uint64_t)domain_metrics.charCount);
    yyjson_mut_obj_add_uint(resp, domain, "wordCount", (uint64_t)domain_metrics.wordCount);
    yyjson_mut_obj_add_uint(resp, domain, "wordCharCount", (uint64_t)domain_metrics.wordCharCount);

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

            yyjson_mut_obj_add_uint(resp, p, "charCount", (uint64_t)page_metrics[i].charCount);
            yyjson_mut_obj_add_uint(resp, p, "wordCount", (uint64_t)page_metrics[i].wordCount);
            yyjson_mut_obj_add_uint(resp, p, "wordCharCount", (uint64_t)page_metrics[i].wordCharCount);

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

    stopwords_free(&sw);

    app_analyze_result_t ok = {0};
    ok.status = 0;
    ok.message = NULL;
    ok.response_doc = resp;
    free(page_metrics);
    return ok;
}
