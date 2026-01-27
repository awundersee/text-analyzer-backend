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
#include "cli/batch.h"

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
    app_page_t *pages;        // malloc'd array
    size_t count;
    const char *domain;       // pointer into yyjson doc (ok while doc lives)
    bool include_bigrams;
    bool per_page_results;
} cli_input_t;

static cli_input_t parse_input(yyjson_doc *doc) {
    yyjson_val *root = yyjson_doc_get_root(doc);

    cli_input_t in = {0};
    in.include_bigrams = true;
    in.per_page_results = false;
    in.domain = NULL;

    yyjson_val *pages = NULL;

    if (yyjson_is_obj(root)) {
        // domain (optional)
        yyjson_val *d = yyjson_obj_get(root, "domain");
        if (d && yyjson_is_str(d)) in.domain = yyjson_get_str(d);

        // options (optional)
        yyjson_val *opt = yyjson_obj_get(root, "options");
        if (opt && yyjson_is_obj(opt)) {
            in.include_bigrams = json_get_bool(opt, "includeBigrams", in.include_bigrams);
            in.per_page_results = json_get_bool(opt, "perPageResults", in.per_page_results);
        }

        pages = yyjson_obj_get(root, "pages");
        if (!pages || !yyjson_is_arr(pages)) die("Input must contain 'pages'[]");
    } else if (yyjson_is_arr(root)) {
        // allow bare array of pages (test files)
        pages = root;
    } else {
        die("Root must be an object (with pages) or an array (pages)");
    }

    size_t n = yyjson_arr_size(pages);
    if (n == 0) die("'pages' must not be empty");

    in.pages = (app_page_t *)calloc(n, sizeof(app_page_t));
    if (!in.pages) die("Out of memory");

    size_t idx = 0;
    yyjson_val *p;
    yyjson_arr_iter iter = yyjson_arr_iter_with(pages);
    while ((p = yyjson_arr_iter_next(&iter))) {
        if (!yyjson_is_obj(p)) die("'pages' must be an array of objects");

        yyjson_val *t = yyjson_obj_get(p, "text");
        if (!t || !yyjson_is_str(t)) die("each page must contain a string field 'text'");

        yyjson_val *jid   = yyjson_obj_get(p, "id");
        yyjson_val *jname = yyjson_obj_get(p, "name");
        yyjson_val *jurl  = yyjson_obj_get(p, "url");

        in.pages[idx].id   = (jid && yyjson_is_int(jid)) ? (long long)yyjson_get_sint(jid) : 0;
        in.pages[idx].name = (jname && yyjson_is_str(jname)) ? yyjson_get_str(jname) : NULL;
        in.pages[idx].url  = (jurl && yyjson_is_str(jurl)) ? yyjson_get_str(jurl) : NULL;
        in.pages[idx].text = yyjson_get_str(t);

        idx++;
    }

    in.count = n;
    return in;
}

int main(int argc, char **argv) {

    // Subcommand: batch
    if (argc >= 2 && strcmp(argv[1], "batch") == 0) {
        const char *in_dir  = "data/batch_in";
        const char *out_dir = "data/batch_out";
        int cont = 1; // default: continue on error

        // optional simple args:
        // --in <dir> --out <dir> --no-continue
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
                in_dir = argv[++i];
            } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                out_dir = argv[++i];
            } else if (strcmp(argv[i], "--no-continue") == 0) {
                cont = 0;
            } else {
                fprintf(stderr, "Unknown arg: %s\n", argv[i]);
                fprintf(stderr, "Usage: %s batch [--in dir] [--out dir] [--no-continue]\n", argv[0]);
                return 2;
            }
        }

        return cli_run_batch(in_dir, out_dir, cont);
    }

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
    const char *sw = getenv("STOPWORDS_FILE");
    if (!sw) sw = "data/stopwords_de.txt";

    app_analyze_opts_t opts = {
    .include_bigrams   = in.include_bigrams,
    .per_page_results  = in.per_page_results,
    .stopwords_path    = sw,
    .top_k             = 0,          // CLI: FULL
    .domain            = in.domain   // optional
    };

    app_analyze_result_t res = app_analyze_pages(in.pages, in.count, &opts);

    double t1 = now_ms();

    if (res.status != 0) {
        fprintf(stderr, "Analysis failed: status=%d message=%s\n",
                res.status, res.message ? res.message : "(no message)");
        yyjson_doc_free(doc);
        free(in.pages);
        return 4;
    }

    // Print runtime to stdout (useful for perf scripts)
    fprintf(stderr, "runtime_ms=%.3f\n", (t1 - t0));

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
    free(in.pages);

    return 0;
}
