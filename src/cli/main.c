// src/cli/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "yyjson.h"

// TODO: This header should expose the SAME analysis function used by /analyze.
// Create it by extracting the core "analyze request -> response json" logic
// from src/api/main.c into a shared module, e.g. src/app/analyze.c/.h.
#include "app/analyze.h"   // <- you will create this (see notes below)

/* ------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------ */

static void die(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (n != (size_t)sz) { free(buf); return NULL; }
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static double now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

static bool json_get_bool(yyjson_val *obj, const char *key, bool fallback) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!v) return fallback;
    if (yyjson_is_bool(v)) return yyjson_get_bool(v);
    return fallback;
}

/* ------------------------------------------------------------
 * Input parsing
 * Accepts either:
 *  A) { "texts": ["...", "..."], "options": {...} }
 *  B) { "pages": [{ "text": "..." }, ...], "options": {...} }
 * ------------------------------------------------------------ */
typedef struct {
    const char **texts;     // pointers into yyjson doc memory (valid while doc lives)
    size_t count;
    bool include_bigrams;
    bool per_page_results;
} cli_input_t;

static cli_input_t parse_input(yyjson_doc *doc) {
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) die("Root must be a JSON object");

    cli_input_t in = {0};
    in.include_bigrams = true;
    in.per_page_results = false;

    // options (optional)
    yyjson_val *opt = yyjson_obj_get(root, "options");
    if (opt && yyjson_is_obj(opt)) {
        in.include_bigrams = json_get_bool(opt, "includeBigrams", in.include_bigrams);
        in.per_page_results = json_get_bool(opt, "perPageResults", in.per_page_results);
    }

    // Prefer "texts" (perf format)
    yyjson_val *texts = yyjson_obj_get(root, "texts");
    if (texts && yyjson_is_arr(texts)) {
        size_t n = yyjson_arr_size(texts);
        if (n == 0) die("'texts' must not be empty");

        in.texts = (const char **)calloc(n, sizeof(const char *));
        if (!in.texts) die("Out of memory");

        size_t idx = 0;
        yyjson_val *it;
        yyjson_arr_iter iter = yyjson_arr_iter_with(texts);
        while ((it = yyjson_arr_iter_next(&iter))) {
            if (!yyjson_is_str(it)) die("'texts' must be an array of strings");
            in.texts[idx++] = yyjson_get_str(it);
        }
        in.count = n;
        return in;
    }

    // Otherwise: pages[].text (frontend format)
    yyjson_val *pages = yyjson_obj_get(root, "pages");
    if (pages && yyjson_is_arr(pages)) {
        size_t n = yyjson_arr_size(pages);
        if (n == 0) die("'pages' must not be empty");

        in.texts = (const char **)calloc(n, sizeof(const char *));
        if (!in.texts) die("Out of memory");

        size_t idx = 0;
        yyjson_val *p;
        yyjson_arr_iter iter = yyjson_arr_iter_with(pages);
        while ((p = yyjson_arr_iter_next(&iter))) {
            if (!yyjson_is_obj(p)) die("'pages' must be an array of objects");
            yyjson_val *t = yyjson_obj_get(p, "text");
            if (!t || !yyjson_is_str(t)) die("each page must contain a string field 'text'");
            in.texts[idx++] = yyjson_get_str(t);
        }
        in.count = n;
        return in;
    }

    die("Input must contain either 'texts'[] or 'pages'[] with 'text' fields");
    return in; // unreachable
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.json> [--out output.json]\n", argv[0]);
        return 2;
    }

    const char *in_path = argv[1];
    const char *out_path = NULL;

    if (argc >= 4 && strcmp(argv[2], "--out") == 0) {
        out_path = argv[3];
    }

    size_t json_len = 0;
    char *json = read_file_all(in_path, &json_len);
    if (!json) die("Could not read input file");

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts(json, json_len, 0, NULL, &err);
    free(json);

    if (!doc) {
        fprintf(stderr, "JSON parse error at pos %zu: %s\n", err.pos, err.msg);
        return 3;
    }

    cli_input_t in = parse_input(doc);

    // --- run analysis (shared core) ---
    double t0 = now_ms();

    // app_analyze_texts should:
    // - run the SAME pipeline as /analyze
    // - return a yyjson_mut_doc* response (or NULL on error)
    app_analyze_opts_t opts = {
    .include_bigrams = in.include_bigrams,
    .per_page_results = in.per_page_results,
    .stopwords_path = "data/stopwords_de.txt",
    .top_k = 10
    };

    app_analyze_result_t res = app_analyze_texts(in.texts, in.count, &opts);

    double t1 = now_ms();

    if (res.status != 0) {
        fprintf(stderr, "Analysis failed: status=%d message=%s\n",
                res.status, res.message ? res.message : "(no message)");
        yyjson_doc_free(doc);
        free(in.texts);
        return 4;
    }

    // Print runtime to stdout (useful for perf scripts)
    fprintf(stdout, "runtime_ms=%.3f\n", (t1 - t0));

    // Optional: write response JSON
    if (out_path && res.response_doc) {
        yyjson_write_err werr;
        size_t out_len = 0;

        char *out = yyjson_mut_write_opts(res.response_doc, YYJSON_WRITE_PRETTY, NULL, &out_len, &werr);
        if (!out) {
            fprintf(stderr, "JSON write error: %s\n", werr.msg);
        } else {
            FILE *fo = fopen(out_path, "wb");
            if (!fo) {
                fprintf(stderr, "Could not open output file for writing: %s\n", out_path);
            } else {
                fwrite(out, 1, out_len, fo);
                fclose(fo);
            }
            free(out);
        }

    }

    // cleanup
    if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
    yyjson_doc_free(doc);
    free(in.texts);

    return 0;
}
