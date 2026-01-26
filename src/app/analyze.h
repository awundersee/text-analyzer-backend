// src/app/analyze.h
#pragma once
#include <stdbool.h>
#include "yyjson.h"

typedef struct {
    bool include_bigrams;
    bool per_page_results;
    const char *stopwords_path;
    size_t top_k;      
} app_analyze_opts_t;

typedef struct {
    int status;              // 0 = ok
    const char *message;     // optional error message (static string is ok)
    yyjson_mut_doc *response_doc;
} app_analyze_result_t;

app_analyze_result_t app_analyze_texts(const char **texts, size_t n_texts, const app_analyze_opts_t *opts);
