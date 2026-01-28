// tests/unit/test_pipeline_force.c
#include "unity.h"

#include <string.h>
#include <stdlib.h>

#include "app/analyze.h"
#include "yyjson.h"

// Helper: meta string holen
static const char* meta_get_str_mut(yyjson_mut_doc *doc, const char *key) {
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *meta = yyjson_mut_obj_get(root, "meta");
    if (!meta) return NULL;
    yyjson_mut_val *v = yyjson_mut_obj_get(meta, key);
    if (!v || !yyjson_mut_is_str(v)) return NULL;
    return yyjson_mut_get_str(v);
}

static void analyze_one_page_and_assert(app_pipeline_t requested, const char *expect_used) {
    app_page_t page = {0};
    page.id = 1;
    page.name = "p1";
    page.url = "https://example.test/p1";
    page.text = "Hallo Welt Hallo Welt"; // klein => AUTO wäre string

    app_analyze_opts_t opts = {0};
    opts.stopwords_path = "data/stopwords_de.txt";
    opts.domain = "example.test";
    opts.include_bigrams = 0;
    opts.per_page_results = 0;
    opts.top_k = 20;
    opts.pipeline = requested;

    app_analyze_result_t r = app_analyze_pages(&page, 1, &opts);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.status, r.message ? r.message : "app_analyze_pages failed");
    TEST_ASSERT_NOT_NULL(r.response_doc);

    const char *req  = meta_get_str_mut(r.response_doc, "pipelineRequested");
    const char *used = meta_get_str_mut(r.response_doc, "pipelineUsed");

    TEST_ASSERT_NOT_NULL(req);
    TEST_ASSERT_NOT_NULL(used);

    if (requested == APP_PIPELINE_STRING) TEST_ASSERT_EQUAL_STRING("string", req);
    else if (requested == APP_PIPELINE_ID) TEST_ASSERT_EQUAL_STRING("id", req);
    else TEST_ASSERT_EQUAL_STRING("auto", req);

    TEST_ASSERT_EQUAL_STRING(expect_used, used);

    yyjson_mut_doc_free(r.response_doc);
    
}

void test_pipeline_force_string(void) {
    analyze_one_page_and_assert(APP_PIPELINE_STRING, "string");
}

void test_pipeline_force_id(void) {
    analyze_one_page_and_assert(APP_PIPELINE_ID, "id");
}

void test_pipeline_auto_small_uses_string(void) {
    analyze_one_page_and_assert(APP_PIPELINE_AUTO, "string");
}
