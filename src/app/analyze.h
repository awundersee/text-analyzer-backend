// src/app/analyze.h
#pragma once
#include <stdbool.h>
#include <stddef.h>   // size_t
#include "yyjson.h"
#include <string.h>

typedef enum {
  APP_PIPELINE_AUTO = 0,
  APP_PIPELINE_STRING = 1,
  APP_PIPELINE_ID = 2
} app_pipeline_t;

static inline app_pipeline_t app_pipeline_from_str(const char *s, int *ok) {
    if (ok) *ok = 1;
    if (!s || s[0] == '\0') return APP_PIPELINE_AUTO;

    if (strcmp(s, "auto") == 0)   return APP_PIPELINE_AUTO;
    if (strcmp(s, "string") == 0) return APP_PIPELINE_STRING;
    if (strcmp(s, "id") == 0)     return APP_PIPELINE_ID;

    if (ok) *ok = 0;
    return APP_PIPELINE_AUTO;
}

static inline const char* app_pipeline_to_str(app_pipeline_t p) {
    switch (p) {
        case APP_PIPELINE_STRING: return "string";
        case APP_PIPELINE_ID:     return "id";
        case APP_PIPELINE_AUTO:
        default:                  return "auto";
    }
}

typedef struct {
    long long id;       // optional
    const char *name;   // optional
    const char *url;    // optional
    const char *text;   // required
} app_page_t;

typedef struct {
    bool include_bigrams;
    bool per_page_results;
    const char *stopwords_path;
    size_t top_k;       // 0 = FULL, >0 = TopK
    const char *domain; // optional
    app_pipeline_t pipeline;
} app_analyze_opts_t;

typedef struct {
    int status;              // 0 = ok
    const char *message;     // static string ok
    yyjson_mut_doc *response_doc;
    double analysis_runtime_ms;
} app_analyze_result_t;

typedef struct {
    size_t charCount;
    size_t wordCount;
    size_t wordCharCount;
} TextMetrics;

app_analyze_result_t app_analyze_pages(const app_page_t *pages,
                                       size_t n_pages,
                                       const app_analyze_opts_t *opts);
