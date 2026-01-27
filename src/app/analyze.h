// src/app/analyze.h
#pragma once
#include <stdbool.h>
#include <stddef.h>   // size_t
#include "yyjson.h"

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
} app_analyze_opts_t;

typedef struct {
    int status;              // 0 = ok
    const char *message;     // static string ok
    yyjson_mut_doc *response_doc;
} app_analyze_result_t;

app_analyze_result_t app_analyze_pages(const app_page_t *pages,
                                       size_t n_pages,
                                       const app_analyze_opts_t *opts);
