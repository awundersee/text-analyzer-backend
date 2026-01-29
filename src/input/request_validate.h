// src/input/request_validate.h
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "yyjson.h"
#include "app/analyze.h"   // app_page_t, app_pipeline_t

typedef struct {
    // Limits (0 = unlimited)
    size_t max_pages;    // API: 100, CLI: 0
    size_t max_bytes;    // API: 10*1024*1024, CLI: 0

    // Allowed input shapes / behavior
    bool allow_root_array;         // CLI: true, API: false
    bool allow_options_pipeline;   // API: true, CLI: false (keeps current behavior)

    // Defaults if options missing / wrong type
    bool default_include_bigrams;   // true
    bool default_per_page_results;  // true

    size_t max_total_chars;  // sum(strlen(page.text))
    size_t max_page_chars;   // strlen(page.text) per page    
} req_validate_cfg_t;

typedef struct {
    int status_code;          // suggested HTTP status for API (400/413)
    const char *message;      // points to static string (no free)
} req_error_t;

typedef struct {
    yyjson_doc *doc;       // keep alive while using strings
    app_page_t *pages;     // calloc'd, free with validated_request_free
    size_t page_count;

    const char *domain;    // pointer into doc (optional)
    bool include_bigrams;
    bool per_page_results;

    size_t chars_total;      // NEW: sum of page.text lengths

    bool has_pipeline_from_options;
    app_pipeline_t pipeline_from_options;
} validated_request_t;

// Parses JSON and validates structure according to cfg.
// NOTE: Caller must keep `json` buffer alive until validated_request_free() is called.
bool request_parse_and_validate(char *json,
                                 size_t json_len,
                                 const req_validate_cfg_t *cfg,
                                 validated_request_t *out,
                                 req_error_t *err);

void validated_request_free(validated_request_t *r);
