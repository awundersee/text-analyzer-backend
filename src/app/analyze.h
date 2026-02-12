// src/app/analyze.h
#pragma once
#include <stdbool.h>
#include <stddef.h>   // size_t
#include "yyjson.h"
#include <string.h>

/* Pipeline selection:
 * - AUTO: choose based on input size/threshold
 * - STRING: baseline string-based counting
 * - ID: dictionary/ID-based counting (better for large inputs)
 */
typedef enum {
  APP_PIPELINE_AUTO = 0,
  APP_PIPELINE_STRING = 1,
  APP_PIPELINE_ID = 2
} app_pipeline_t;

/* Parses pipeline selector from CLI/API ("auto"|"string"|"id").
 * ok is set to 0 on invalid input.
 */
static inline app_pipeline_t app_pipeline_from_str(const char *s, int *ok) {
    if (ok) *ok = 1;
    if (!s || s[0] == '\0') return APP_PIPELINE_AUTO;

    if (strcmp(s, "auto") == 0)   return APP_PIPELINE_AUTO;
    if (strcmp(s, "string") == 0) return APP_PIPELINE_STRING;
    if (strcmp(s, "id") == 0)     return APP_PIPELINE_ID;

    if (ok) *ok = 0;
    return APP_PIPELINE_AUTO;
}

/* Stable string representation for meta.pipelineRequested/pipelineUsed. */
static inline const char* app_pipeline_to_str(app_pipeline_t p) {
    switch (p) {
        case APP_PIPELINE_STRING: return "string";
        case APP_PIPELINE_ID:     return "id";
        case APP_PIPELINE_AUTO:
        default:                  return "auto";
    }
}

/* Input page unit shared by CLI/API. */
typedef struct {
    long long id;       // optional (client correlation)
    const char *name;   // optional
    const char *url;    // optional
    const char *text;   // required
} app_page_t;

/* Analysis options provided by API/CLI.
 * top_k: 0 => full lists, >0 => truncate to top_k after aggregation.
 */
typedef struct {
    bool include_bigrams;
    bool per_page_results;
    const char *stopwords_path;
    size_t top_k;       // 0 = FULL, >0 = TopK
    const char *domain; // optional (echoed into meta)
    app_pipeline_t pipeline;
} app_analyze_opts_t;

/* Result container for API/CLI.
 * response_doc is owned by the caller and must be freed with yyjson_mut_doc_free().
 */
typedef struct {
    int status;              // 0 = ok, otherwise HTTP-ish error code
    const char *message;     // static string ok
    yyjson_mut_doc *response_doc;
    double analysis_runtime_ms; // currently unused; runtime lives in response meta
} app_analyze_result_t;

/* Basic text metrics reported in domainResult and per page. */
typedef struct {
    size_t charCount;
    size_t wordCount;
    size_t wordCharCount;
} TextMetrics;

/* Core entrypoint used by both API and CLI. */
app_analyze_result_t app_analyze_pages(const app_page_t *pages,
                                       size_t n_pages,
                                       const app_analyze_opts_t *opts);
