#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "yyjson.h"
#include "app/analyze.h"   // app_page_t, app_pipeline_t

/*
 * Configuration for request validation.
 *
 * Defines structural constraints and resource limits at the system boundary.
 * Used to enforce API vs. CLI behavior and protect memory usage before
 * entering the analysis pipeline.
 */
typedef struct {
    // Hard limits (0 = unlimited)
    size_t max_pages;    // API: 100, CLI: unlimited
    size_t max_bytes;    // API: 10 MB, CLI: unlimited

    // Allowed request shapes / feature flags
    bool allow_root_array;        // CLI mode
    bool allow_options_pipeline;  // API-controlled pipeline selection

    // Defaults if options missing or invalid
    bool default_include_bigrams;
    bool default_per_page_results;

    // Text size guards (prevents oversized allocations)
    size_t max_total_chars;  // sum of all page.text lengths
    size_t max_page_chars;   // per-page text limit
} req_validate_cfg_t;

/*
 * Lightweight error descriptor for boundary validation.
 * status_code is primarily relevant for HTTP integration.
 */
typedef struct {
    int status_code;        // e.g. 400 (bad request), 413 (payload too large)
    const char *message;    // static string, no ownership transfer
} req_error_t;

/*
 * Result of successful validation.
 *
 * Owns parsed JSON document and extracted page array.
 * Acts as the transition object between input layer and analysis pipeline.
 */
typedef struct {
    yyjson_doc *doc;     // must stay alive while using string pointers
    app_page_t *pages;   // allocated array, freed via validated_request_free
    size_t page_count;

    const char *domain;  // optional, pointer into JSON doc
    bool include_bigrams;
    bool per_page_results;

    size_t chars_total;  // total input size (used for pipeline switch decision)

    bool has_pipeline_from_options;
    app_pipeline_t pipeline_from_options; // optional override
} validated_request_t;

/*
 * Parses and validates incoming JSON according to cfg.
 *
 * Performs structural checks, size validation and option extraction.
 * No analysis is executed here.
 *
 * Caller must keep `json` buffer alive until validated_request_free().
 */
bool request_parse_and_validate(char *json,
                                 size_t json_len,
                                 const req_validate_cfg_t *cfg,
                                 validated_request_t *out,
                                 req_error_t *err);

/*
 * Releases memory owned by validated_request_t.
 * Must be called before leaving the request boundary.
 */
void validated_request_free(validated_request_t *r);
