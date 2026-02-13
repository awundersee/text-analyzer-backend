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

/* AUTO pipeline switch threshold (total input chars). */
#define PIPELINE_THRESHOLD_CHARS (1000 * 1024) // 1000 KB

/* Measurement point for meta.runtimeMsAnalyze (core analysis only). */
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

/* JSON view helper: serializes WordCountList into response schema. */
static void json_add_word_list(yyjson_mut_doc *doc, yyjson_mut_val *arr, const WordCountList *list) {
    for (size_t i = 0; i < list->count; i++) {
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        const char *w = list->items[i].word ? list->items[i].word : "";
        yyjson_mut_obj_add_strcpy(doc, obj, "word", w);
        yyjson_mut_obj_add_uint(doc, obj, "count", (uint64_t)list->items[i].count);
        yyjson_mut_arr_add_val(arr, obj);
    }
}

/* JSON view helper: serializes BigramCountList into response schema. */
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

/* Converts enum into a stable string for meta.pipelineRequested. */
static const char* pipeline_requested_str(const app_analyze_opts_t *opts) {
    if (!opts) return "auto";
    switch (opts->pipeline) {
        case APP_PIPELINE_STRING: return "string";
        case APP_PIPELINE_ID:     return "id";
        case APP_PIPELINE_AUTO:
        default:                  return "auto";
    }
}

/* UTF-8 length helper */
static size_t utf8_strlen(const char *s) {
    size_t count = 0;
    while (*s) {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            count++;  // Count only leading bytes (new codepoint)
        }
        s++;
    }
    return count;
}

/* Computes basic text metrics for meta/domain/page reporting.
 * Metrics are based on the raw token stream (includes stopwords).
 */
static TextMetrics compute_metrics(const char *text, TokenList tokens) {
    TextMetrics m = {0};

    if (text) m.charCount = utf8_strlen(text);

    m.wordCount = tokens.count;

    for (size_t i = 0; i < tokens.count; i++) {
        if (tokens.items[i]) m.wordCharCount += utf8_strlen(tokens.items[i]);
    }

    return m;
}

static inline int deadline_exceeded(const app_analyze_opts_t *opts) {
    return (opts && opts->deadline_ms > 0.0 && now_ms() > opts->deadline_ms);
}

typedef struct {
    bool include_bigrams;

    // Arrays (immer nur "Container" -> Inhalte separat freigeben)
    WordCountList   *page_words;
    BigramCountList *page_bigrams;
    TextMetrics     *page_metrics;

    // Wie viele Seiten sind in page_words/page_bigrams schon "gefüllt"?
    // -> genau diese Anzahl muss per free_word_counts/free_bigram_counts freigegeben werden.
    size_t pages_filled;

    // Stopwords
    StopwordList sw;
    bool sw_loaded;

    // Aktuelle Tokens (falls gerade in Bearbeitung)
    TokenList raw;
    bool raw_live;

    TokenList filtered;
    bool filtered_live;

    // Domain-Aggregate + TopK (optional, je nach Phase)
    WordCountList domain_words;
    bool domain_words_live;

    BigramCountList domain_bigrams;
    bool domain_bigrams_live;

    WordCountList top_words;
    bool top_words_live;

    BigramCountList top_bigs;
    bool top_bigs_live;
} CleanupCtx;

static void cleanup_ctx(CleanupCtx *c) {
    if (!c) return;

    if (c->filtered_live) {
        free_tokens(&c->filtered);
        c->filtered_live = false;
    }
    if (c->raw_live) {
        free_tokens(&c->raw);
        c->raw_live = false;
    }

    if (c->top_words_live) {
        free_top_k_words(&c->top_words);
        c->top_words_live = false;
    }
    if (c->top_bigs_live) {
        free_top_k_bigrams(&c->top_bigs);
        c->top_bigs_live = false;
    }

    if (c->domain_words_live) {
        free_aggregated_word_counts(&c->domain_words);
        c->domain_words_live = false;
    }
    if (c->domain_bigrams_live) {
        free_aggregated_bigram_counts(&c->domain_bigrams);
        c->domain_bigrams_live = false;
    }

    if (c->page_words) {
        for (size_t k = 0; k < c->pages_filled; k++) {
            free_word_counts(&c->page_words[k]);
        }
    }
    if (c->include_bigrams && c->page_bigrams) {
        for (size_t k = 0; k < c->pages_filled; k++) {
            free_bigram_counts(&c->page_bigrams[k]);
        }
    }

    free(c->page_words);
    c->page_words = NULL;

    free(c->page_bigrams);
    c->page_bigrams = NULL;

    free(c->page_metrics);
    c->page_metrics = NULL;

    if (c->sw_loaded) {
        stopwords_free(&c->sw);
        c->sw_loaded = false;
    }
}

app_analyze_result_t app_analyze_pages(const app_page_t *pages, size_t n_pages, const app_analyze_opts_t *opts) {
    if (!pages || n_pages == 0) return fail(10, "No pages provided");

    const char *stop_path = (opts && opts->stopwords_path) ? opts->stopwords_path : "data/stopwords_de.txt";
    const char *domain_str = (opts && opts->domain) ? opts->domain : NULL;

    bool include_bigrams = (opts) ? opts->include_bigrams : true;
    bool per_page = (opts) ? opts->per_page_results : true;

    /* Top-K policy: 0 means FULL output (used by CLI/batch). */
    size_t topk = opts ? opts->top_k : 20;

    CleanupCtx cx = {0};
    cx.include_bigrams = include_bigrams;

    /* Allocate per-page containers */
    cx.page_words = (WordCountList *)calloc(n_pages, sizeof(WordCountList));
    cx.page_bigrams = include_bigrams ? (BigramCountList *)calloc(n_pages, sizeof(BigramCountList)) : NULL;
    cx.page_metrics = (TextMetrics *)calloc(n_pages, sizeof(TextMetrics));
    cx.pages_filled = 0;

    if (!cx.page_words || !cx.page_metrics || (include_bigrams && !cx.page_bigrams)) {
        cleanup_ctx(&cx);
        return fail(11, "Out of memory");
    }

    /* Measurement point for AUTO pipeline decision (total chars received). */
    size_t chars_received = 0;
    for (size_t i = 0; i < n_pages; i++) {
        const char *t = pages[i].text ? pages[i].text : "";
        chars_received += strlen(t);
    }

    /* Pipeline switch:
     * - AUTO selects ID for larger inputs
     * - explicit opts->pipeline overrides AUTO decision
     */
    int use_id_pipeline = (chars_received >= PIPELINE_THRESHOLD_CHARS);
    if (opts) {
        if (opts->pipeline == APP_PIPELINE_STRING) use_id_pipeline = 0;
        else if (opts->pipeline == APP_PIPELINE_ID) use_id_pipeline = 1;
    }

    /* Metrics are reported both per-page and aggregated for domainResult. */
    TextMetrics domain_metrics = (TextMetrics){0};

    double t_analyze0 = now_ms();

    /* Load stopwords once */
    if (stopwords_load(&cx.sw, stop_path) != 0) {
        cleanup_ctx(&cx);
        return fail(20, "Stopwords load failed (file missing or invalid?)");
    }
    cx.sw_loaded = true;

    if (deadline_exceeded(opts)) {
        cleanup_ctx(&cx);
        return fail(503, "analysis timeout (>10s)");
    }

    for (size_t i = 0; i < n_pages; i++) {

        if (deadline_exceeded(opts)) {
            cleanup_ctx(&cx);
            return fail(503, "analysis timeout (>10s)");
        }

        const char *t = pages[i].text ? pages[i].text : "";

        cx.raw = tokenize(t);
        cx.raw_live = true;

        if (deadline_exceeded(opts)) {
            cleanup_ctx(&cx);
            return fail(503, "analysis timeout (>10s)");
        }   

        /* Words/metrics use filtered tokens (no stopwords, short, digits-only). */
        cx.filtered = filter_stopwords_copy(&cx.raw, stop_path);
        cx.filtered_live = true;

        if (deadline_exceeded(opts)) {
            cleanup_ctx(&cx);
            return fail(503, "analysis timeout (>10s)");
        }       

        /* Metrics are derived from the same token stream as word results. */
        cx.page_metrics[i] = compute_metrics(t, cx.raw);
        domain_metrics.charCount     += cx.page_metrics[i].charCount;
        domain_metrics.wordCount     += cx.page_metrics[i].wordCount;
        domain_metrics.wordCharCount += cx.page_metrics[i].wordCharCount;

        /* Core analysis stage: pipeline-specific implementation.
         * - words are based on filtered tokens
         * - bigrams (if enabled) are based on raw tokens + stopword rules (no bridging)
         */
        int ok = 0;
        if (use_id_pipeline) {
            ok = analyze_id_pipeline(&cx.filtered, &cx.raw, include_bigrams, &cx.sw,
                                    &cx.page_words[i], include_bigrams ? &cx.page_bigrams[i] : NULL);
            if (!ok) { cleanup_ctx(&cx); return fail(30, "ID pipeline failed (out of memory?)"); }
        } else {
            ok = analyze_string_pipeline(&cx.filtered, &cx.raw, include_bigrams, &cx.sw,
                                         &cx.page_words[i], include_bigrams ? &cx.page_bigrams[i] : NULL);
            if (!ok) { cleanup_ctx(&cx); return fail(31, "String pipeline failed (out of memory?)"); }
        }

        cx.pages_filled = i + 1;

        /* release current tokens (and clear flags!) */
        free_tokens(&cx.filtered);
        cx.filtered_live = false;

        free_tokens(&cx.raw);
        cx.raw_live = false;

    }

    if (deadline_exceeded(opts)) {
        cleanup_ctx(&cx);
        return fail(503, "analysis timeout (>10s)");
    }

    /* Aggregation */
    cx.domain_words = aggregate_word_counts(cx.page_words, n_pages);
    cx.domain_words_live = true;

    if (include_bigrams) {
        cx.domain_bigrams = aggregate_bigram_counts(cx.page_bigrams, n_pages);
        cx.domain_bigrams_live = true;
    } else {
        cx.domain_bigrams = (BigramCountList){0};
        cx.domain_bigrams_live = false;
    }

    if (deadline_exceeded(opts)) {
        cleanup_ctx(&cx);
        return fail(503, "analysis timeout (>10s)");
    }

    /* Apply Top-K after aggregation (0 means full lists). */
    size_t k_words = (topk == 0) ? cx.domain_words.count : topk;
    cx.top_words = top_k_words(&cx.domain_words, k_words);
    cx.top_words_live = true;

    if (include_bigrams) {
        size_t k_bigs = (topk == 0) ? cx.domain_bigrams.count : topk;
        cx.top_bigs = top_k_bigrams(&cx.domain_bigrams, k_bigs);
        cx.top_bigs_live = true;
    }

    if (deadline_exceeded(opts)) {
        cleanup_ctx(&cx);
        return fail(503, "analysis timeout (>10s)");
    }

    double runtime_analyze_ms = round3(now_ms() - t_analyze0);

    /* Build response JSON (schema aligned with response-analyse_example.json). */
    yyjson_mut_doc *resp = yyjson_mut_doc_new(NULL);
    if (!resp) {
        cleanup_ctx(&cx);
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

    /* Expose pipeline selection for perf comparisons and debugging. */
    const char *req = pipeline_requested_str(opts);
    const char *used = use_id_pipeline ? "id" : "string";
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineRequested", req);
    yyjson_mut_obj_add_strcpy(resp, meta, "pipelineUsed", used);

    /* Measurement point: peak RSS of whole process at end of analysis. */
    yyjson_mut_obj_add_uint(resp, meta, "peakRssKiB", ta_peak_rss_kib());

    yyjson_mut_obj_add_val(resp, root, "meta", meta);

    yyjson_mut_val *domain = yyjson_mut_obj(resp);
    yyjson_mut_obj_add_uint(resp, domain, "charCount", (uint64_t)domain_metrics.charCount);
    yyjson_mut_obj_add_uint(resp, domain, "wordCount", (uint64_t)domain_metrics.wordCount);
    yyjson_mut_obj_add_uint(resp, domain, "wordCharCount", (uint64_t)domain_metrics.wordCharCount);

    yyjson_mut_val *words_arr = yyjson_mut_arr(resp);
    json_add_word_list(resp, words_arr, &cx.top_words);
    yyjson_mut_obj_add_val(resp, domain, "words", words_arr);

    if (include_bigrams) {
        yyjson_mut_val *bigrams_arr = yyjson_mut_arr(resp);
        json_add_bigram_list(resp, bigrams_arr, &cx.top_bigs);
        yyjson_mut_obj_add_val(resp, domain, "bigrams", bigrams_arr);
    }

    yyjson_mut_obj_add_val(resp, root, "domainResult", domain);

    if (per_page) {
        yyjson_mut_val *pages_arr = yyjson_mut_arr(resp);
        for (size_t i = 0; i < n_pages; i++) {
            yyjson_mut_val *p = yyjson_mut_obj(resp);

            /* Preserve identifiers from input for client-side correlation. */
            yyjson_mut_obj_add_sint(resp, p, "id", (int64_t)pages[i].id);
            if (pages[i].name) yyjson_mut_obj_add_strcpy(resp, p, "name", pages[i].name);
            if (pages[i].url)  yyjson_mut_obj_add_strcpy(resp, p, "url", pages[i].url);

            yyjson_mut_obj_add_uint(resp, p, "charCount", (uint64_t)cx.page_metrics[i].charCount);
            yyjson_mut_obj_add_uint(resp, p, "wordCount", (uint64_t)cx.page_metrics[i].wordCount);
            yyjson_mut_obj_add_uint(resp, p, "wordCharCount", (uint64_t)cx.page_metrics[i].wordCharCount);

            /* Per-page Top-K (0 means full list) for debugging and comparisons. */
            size_t k_pw = (topk == 0) ? cx.page_words[i].count : topk;
            WordCountList pw_top = top_k_words(&cx.page_words[i], k_pw);
            yyjson_mut_val *pw = yyjson_mut_arr(resp);
            json_add_word_list(resp, pw, &pw_top);
            yyjson_mut_obj_add_val(resp, p, "words", pw);
            free_top_k_words(&pw_top);

            if (include_bigrams) {
                size_t k_pb = (topk == 0) ? cx.page_bigrams[i].count : topk;
                BigramCountList pb_top = top_k_bigrams(&cx.page_bigrams[i], k_pb);
                yyjson_mut_val *pb = yyjson_mut_arr(resp);
                json_add_bigram_list(resp, pb, &pb_top);
                yyjson_mut_obj_add_val(resp, p, "bigrams", pb);
                free_top_k_bigrams(&pb_top);
            }

            yyjson_mut_arr_add_val(pages_arr, p);
        }
        yyjson_mut_obj_add_val(resp, root, "pageResults", pages_arr);
    }

    /* Free everything except resp */
    cleanup_ctx(&cx);

    app_analyze_result_t ok = {0};
    ok.status = 0;
    ok.message = NULL;
    ok.response_doc = resp;
    return ok;
}
