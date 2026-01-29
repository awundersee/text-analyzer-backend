// tests/unit/test_request_validate.c
#include <string.h>
#include <stdlib.h>

#include "unity.h"
#include "input/request_validate.h"

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static req_validate_cfg_t api_cfg(void) {
    req_validate_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.max_pages = 100;
    c.max_bytes = 10 * 1024 * 1024;
    c.max_total_chars = 2 * 1024 * 1024;  // 2 MiB total text
    c.max_page_chars  = 512 * 1024;       // 512 KiB per page    
    c.allow_root_array = false;
    c.allow_options_pipeline = true;
    c.default_include_bigrams = true;
    c.default_per_page_results = true;
    return c;
}

static req_validate_cfg_t cli_cfg(void) {
    req_validate_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.max_pages = 0;
    c.max_bytes = 0;
    c.max_total_chars = 0;
    c.max_page_chars  = 0;    
    c.allow_root_array = true;
    c.allow_options_pipeline = false;
    c.default_include_bigrams = true;
    c.default_per_page_results = true;
    return c;
}

// NOTE: yyjson expects a mutable buffer (char*). We therefore copy const JSON to heap.
// IMPORTANT: Caller must keep the returned json_buf alive until validated_request_free(out).
static void assert_validate_ok(const char *json_const,
                               const req_validate_cfg_t *cfg,
                               validated_request_t *out,
                               char **json_buf_out) {
    req_error_t e = {0};

    size_t n = strlen(json_const);
    char *json = (char *)malloc(n + 1);
    TEST_ASSERT_NOT_NULL(json);
    memcpy(json, json_const, n + 1);

    bool ok = request_parse_and_validate(json, n, cfg, out, &e);
    if (!ok) {
        free(json);
        TEST_FAIL_MESSAGE(e.message ? e.message : "expected validate ok");
    }

    *json_buf_out = json;
}

static void assert_validate_fail(const char *json_const,
                                 const req_validate_cfg_t *cfg,
                                 int expect_status) {
    validated_request_t out;
    req_error_t e = {0};

    size_t n = strlen(json_const);
    char *json = (char *)malloc(n + 1);
    TEST_ASSERT_NOT_NULL(json);
    memcpy(json, json_const, n + 1);

    bool ok = request_parse_and_validate(json, n, cfg, &out, &e);
    if (ok) {
        validated_request_free(&out);
        free(json);
        TEST_FAIL_MESSAGE("expected validate fail");
    }

    free(json);

    if (expect_status != 0) {
        TEST_ASSERT_EQUAL_INT(expect_status, e.status_code);
    }
}

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------

void test_api_rejects_root_array(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json = "[{\"id\":1,\"url\":\"u\",\"text\":\"hi\"}]";
    assert_validate_fail(json, &cfg, 400);
}

void test_cli_accepts_root_array(void) {
    req_validate_cfg_t cfg = cli_cfg();
    const char *json = "[{\"id\":1,\"name\":\"n\",\"url\":\"u\",\"text\":\"hi\"}]";

    validated_request_t out;
    char *buf = NULL;
    assert_validate_ok(json, &cfg, &out, &buf);

    TEST_ASSERT_EQUAL_UINT64(1, (unsigned long long)out.page_count);
    TEST_ASSERT_TRUE(out.include_bigrams);
    TEST_ASSERT_TRUE(out.per_page_results);

    TEST_ASSERT_NOT_NULL(out.pages);
    TEST_ASSERT_EQUAL_INT64(1, out.pages[0].id);
    TEST_ASSERT_EQUAL_STRING("u", out.pages[0].url);
    TEST_ASSERT_EQUAL_STRING("hi", out.pages[0].text);

    validated_request_free(&out);
    free(buf);
}

void test_api_requires_pages_array(void) {
    req_validate_cfg_t cfg = api_cfg();

    // missing pages
    const char *json1 = "{\"domain\":\"x\",\"options\":{}}";
    assert_validate_fail(json1, &cfg, 400);

    // pages wrong type
    const char *json2 = "{\"pages\":{}}";
    assert_validate_fail(json2, &cfg, 400);
}

void test_api_rejects_empty_pages(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json = "{\"pages\":[]}";
    assert_validate_fail(json, &cfg, 400);
}

void test_api_rejects_too_many_pages(void) {
    req_validate_cfg_t cfg = api_cfg();

    // Build JSON with 101 pages: {"pages":[{"text":"x"}, ... ]}
    // Allocate enough space deterministically.
    // Each page entry is 12 chars + comma, plus overhead.
    size_t cap = 32 + (size_t)101 * 16;
    char *buf = (char *)malloc(cap);
    TEST_ASSERT_NOT_NULL(buf);

    strcpy(buf, "{\"pages\":[");
    for (int i = 0; i < 101; i++) {
        strcat(buf, "{\"text\":\"x\"}");
        if (i != 100) strcat(buf, ",");
    }
    strcat(buf, "]}");

    assert_validate_fail(buf, &cfg, 413);

    free(buf);
}

void test_page_requires_text_string(void) {
    req_validate_cfg_t cfgA = api_cfg();
    req_validate_cfg_t cfgC = cli_cfg();

    // page not object
    const char *json1 = "{\"pages\":[1]}";
    assert_validate_fail(json1, &cfgA, 400);
    assert_validate_fail(json1, &cfgC, 400);

    // text missing
    const char *json2 = "{\"pages\":[{\"id\":1}]}";
    assert_validate_fail(json2, &cfgA, 400);
    assert_validate_fail(json2, &cfgC, 400);

    // text wrong type
    const char *json3 = "{\"pages\":[{\"text\":123}]}";
    assert_validate_fail(json3, &cfgA, 400);
    assert_validate_fail(json3, &cfgC, 400);
}

void test_options_defaults_and_overrides(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json =
        "{"
        "  \"domain\":\"d\","
        "  \"pages\":[{\"text\":\"hi\"}],"
        "  \"options\":{\"includeBigrams\":false,\"perPageResults\":true}"
        "}";

    validated_request_t out;
    char *buf = NULL;
    assert_validate_ok(json, &cfg, &out, &buf);

    TEST_ASSERT_EQUAL_STRING("d", out.domain);
    TEST_ASSERT_FALSE(out.include_bigrams);
    TEST_ASSERT_TRUE(out.per_page_results);

    validated_request_free(&out);
    free(buf);
}

void test_api_accepts_valid_pipeline_option(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json =
        "{"
        "  \"pages\":[{\"text\":\"hi\"}],"
        "  \"options\":{\"pipeline\":\"id\"}"
        "}";

    validated_request_t out;
    char *buf = NULL;
    assert_validate_ok(json, &cfg, &out, &buf);

    TEST_ASSERT_TRUE(out.has_pipeline_from_options);
    TEST_ASSERT_EQUAL_INT((int)APP_PIPELINE_ID, (int)out.pipeline_from_options);

    validated_request_free(&out);
    free(buf);
}

void test_api_rejects_invalid_pipeline_option(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json =
        "{"
        "  \"pages\":[{\"text\":\"hi\"}],"
        "  \"options\":{\"pipeline\":\"nonsense\"}"
        "}";

    assert_validate_fail(json, &cfg, 400);
}

void test_cli_ignores_pipeline_option(void) {
    req_validate_cfg_t cfg = cli_cfg();
    const char *json =
        "{"
        "  \"pages\":[{\"text\":\"hi\"}],"
        "  \"options\":{\"pipeline\":\"id\"}"
        "}";

    validated_request_t out;
    char *buf = NULL;
    assert_validate_ok(json, &cfg, &out, &buf);

    TEST_ASSERT_FALSE(out.has_pipeline_from_options); // CLI: ignore
    TEST_ASSERT_EQUAL_INT((int)APP_PIPELINE_AUTO, (int)out.pipeline_from_options);

    validated_request_free(&out);
    free(buf);
}

void test_api_rejects_page_text_too_large(void) {
    req_validate_cfg_t cfg = api_cfg();
    cfg.max_page_chars = 3; // tiny limit for test

    // "abcd" = 4 chars -> should fail 413
    const char *json = "{\"pages\":[{\"text\":\"abcd\"}]}";
    assert_validate_fail(json, &cfg, 413);
}

void test_api_rejects_total_text_too_large(void) {
    req_validate_cfg_t cfg = api_cfg();
    cfg.max_total_chars = 5; // tiny limit for test

    // total = 3 + 3 = 6 -> should fail 413
    const char *json = "{\"pages\":[{\"text\":\"abc\"},{\"text\":\"def\"}]}";
    assert_validate_fail(json, &cfg, 413);
}

void test_api_rejects_invalid_json(void) {
    req_validate_cfg_t cfg = api_cfg();
    const char *json = "{";
    assert_validate_fail(json, &cfg, 400);
}

void test_api_rejects_payload_over_max_bytes(void) {
    req_validate_cfg_t cfg = api_cfg();
    cfg.max_bytes = 10; // tiny limit

    // length > 10 => 413 before parsing
    const char *json = "{\"pages\":[{\"text\":\"hi\"}]}";
    assert_validate_fail(json, &cfg, 413);
}
