// src/cli/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "yyjson.h"

// TODO: This header should expose the SAME analysis function used by /analyze.
// Create it by extracting the core "analyze request -> response json" logic
// from src/api/main.c into a shared module, e.g. src/app/analyze.c/.h.
#include "app/analyze.h"   // <- you will create this (see notes below)
#include "cli/batch.h"
#include "input/request_validate.h"

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

static inline double round3(double v) {
    return round(v * 1000.0) / 1000.0;
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
        fprintf(stderr, "Usage: %s <input.json> [--out output.json] [--pipeline auto|string|id]\n", argv[0]);
        return 2;
    }

    const char *in_path = argv[1];
    const char *out_path = NULL;
    app_pipeline_t pipeline = APP_PIPELINE_AUTO;

    size_t top_k_cli = 0;  // default: FULL (kein TopK)

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--pipeline") == 0 && i + 1 < argc) {
            int ok = 1;
            pipeline = app_pipeline_from_str(argv[++i], &ok);
            if (!ok) {
                fprintf(stderr, "Unknown pipeline '%s' (use auto|string|id)\n", argv[i]);
                return 2;
            }
        } else if ((strcmp(argv[i], "--topk") == 0 || strcmp(argv[i], "--k") == 0) && i + 1 < argc) {
            const char *s = argv[++i];
            char *end = NULL;
            unsigned long v = strtoul(s, &end, 10);
            if (s[0] == '\0' || (end && *end != '\0')) {
                fprintf(stderr, "Invalid value for %s: '%s'\n", argv[i-1], s);
                fprintf(stderr,
                    "Usage: %s <input.json> [--out output.json] "
                    "[--pipeline auto|string|id] [--topk K]\n",
                    argv[0]);
                return 2;
            }
            top_k_cli = (size_t)v;  // 0 erlaubt (FULL)
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            fprintf(stderr,
                "Usage: %s <input.json> [--out output.json] "
                "[--pipeline auto|string|id] [--topk K]\n",
                argv[0]);
            return 2;
        }
    }


    // --- run analysis (total) ---
    double t0 = now_ms();

    size_t json_len = 0;
    char *json = read_file_all(in_path, &json_len);
    if (!json) die("Could not read input file");

    // --- shared parse + validate (CLI relaxed profile) ---
    req_validate_cfg_t vcfg = {
        .max_pages = 0,                 // unlimited
        .max_bytes = 0,                 // unlimited
        .allow_root_array = true,       // allow root = pages[]
        .allow_options_pipeline = false,// keep CLI behavior: pipeline only via --pipeline
        .default_include_bigrams = true,
        .default_per_page_results = true
    };

    validated_request_t req;
    req_error_t verr = {0};

    // IMPORTANT: keep `json` buffer alive until validated_request_free(&req)
    if (!request_parse_and_validate(json, json_len, &vcfg, &req, &verr)) {
        fprintf(stderr, "Invalid input: %s\n", verr.message ? verr.message : "invalid request");
        free(json);
        return 3;
    }

    // Stopwords file
    const char *sw = getenv("STOPWORDS_FILE");
    if (!sw) sw = "data/stopwords_de.txt";

    app_analyze_opts_t opts = {
        .include_bigrams   = req.include_bigrams,
        .per_page_results  = req.per_page_results,
        .stopwords_path    = sw,
        .top_k             = top_k_cli,   // default: 0
        .domain            = req.domain,  // optional
        .pipeline          = pipeline
    };

    app_analyze_result_t res = app_analyze_pages(req.pages, req.page_count, &opts);

    double t1 = now_ms();
    double runtime_total_ms = round3(t1 - t0);

    if (res.status != 0) {
        fprintf(stderr, "Analysis failed: status=%d message=%s\n",
                res.status, res.message ? res.message : "(no message)");
        if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
        validated_request_free(&req);
        free(json);
        return 4;
    }

    // Read runtimeMsAnalyze from meta
    double analyze_ms = -1.0;

    if (res.response_doc) {
        yyjson_mut_val *root = yyjson_mut_doc_get_root(res.response_doc);
        yyjson_mut_val *meta = root ? yyjson_mut_obj_get(root, "meta") : NULL;

        // ensure meta exists
        if (!meta || !yyjson_mut_is_obj(meta)) {
            meta = yyjson_mut_obj(res.response_doc);
            yyjson_mut_obj_add_val(res.response_doc, root, "meta", meta);
        }

        // read analyze runtime
        yyjson_mut_val *ja = yyjson_mut_obj_get(meta, "runtimeMsAnalyze");
        if (ja && yyjson_mut_is_num(ja)) {
            analyze_ms = yyjson_mut_get_real(ja);
        }

        // write total runtime
        yyjson_mut_obj_add_real(res.response_doc, meta, "runtimeMsTotal", runtime_total_ms);
    }

    if (analyze_ms < 0.0) {
        fprintf(stderr, "Could not read meta.runtimeMsAnalyze from response\n");
        if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
        validated_request_free(&req);
        free(json);
        return 4;
    }

    // CLI stdout (used by perf tests)
    fprintf(stdout, "runtime_ms_total=%.3f\n", runtime_total_ms);
    fprintf(stdout, "runtime_ms_analyze=%.3f\n", analyze_ms);

    // Print peakRss
    unsigned long long peak_kib = 0ULL;
    if (res.response_doc) {
        yyjson_mut_val *root = yyjson_mut_doc_get_root(res.response_doc);
        yyjson_mut_val *meta = root ? yyjson_mut_obj_get(root, "meta") : NULL;
        if (meta) {
            yyjson_mut_val *jp = yyjson_mut_obj_get(meta, "peakRssKiB");
            if (jp && yyjson_mut_is_uint(jp)) peak_kib = (unsigned long long)yyjson_mut_get_uint(jp);
            else if (jp && yyjson_mut_is_int(jp)) {
                long long v = yyjson_mut_get_sint(jp);
                if (v > 0) peak_kib = (unsigned long long)v;
            }
        }
    }
    fprintf(stdout, "peak_rss_kib=%llu\n", peak_kib);

    // Also print domain/page stats for stress CSV (key=value mode)
    long long pages_received = 0;
    const char *pipeline_used = NULL;
    long long word_count = -1, char_count = -1, word_char_count = -1;

    if (res.response_doc) {
        yyjson_mut_val *root = yyjson_mut_doc_get_root(res.response_doc);

        // meta
        yyjson_mut_val *meta = root ? yyjson_mut_obj_get(root, "meta") : NULL;
        if (meta && yyjson_mut_is_obj(meta)) {
            yyjson_mut_val *pr = yyjson_mut_obj_get(meta, "pagesReceived");
            if (pr && yyjson_mut_is_int(pr)) pages_received = (long long)yyjson_mut_get_sint(pr);

            yyjson_mut_val *pu = yyjson_mut_obj_get(meta, "pipelineUsed");
            if (pu && yyjson_mut_is_str(pu)) pipeline_used = yyjson_mut_get_str(pu);
        }

        // domainResult
        yyjson_mut_val *dr = root ? yyjson_mut_obj_get(root, "domainResult") : NULL;
        if (dr && yyjson_mut_is_obj(dr)) {
            yyjson_mut_val *wc = yyjson_mut_obj_get(dr, "wordCount");
            if (wc && yyjson_mut_is_int(wc)) word_count = (long long)yyjson_mut_get_sint(wc);

            yyjson_mut_val *cc = yyjson_mut_obj_get(dr, "charCount");
            if (cc && yyjson_mut_is_int(cc)) char_count = (long long)yyjson_mut_get_sint(cc);

            yyjson_mut_val *wcc = yyjson_mut_obj_get(dr, "wordCharCount");
            if (wcc && yyjson_mut_is_int(wcc)) word_char_count = (long long)yyjson_mut_get_sint(wcc);
        }
    }

    fprintf(stdout, "pages_received=%lld\n", pages_received);
    fprintf(stdout, "pipeline_used=%s\n", pipeline_used ? pipeline_used : "NA");
    fprintf(stdout, "word_count=%lld\n", word_count);
    fprintf(stdout, "char_count=%lld\n", char_count);
    fprintf(stdout, "word_char_count=%lld\n", word_char_count);

    // Optional: write response JSON
    if (out_path && res.response_doc) {
        yyjson_write_err werr;
        size_t out_len = 0;

        char *out = yyjson_mut_write_opts(res.response_doc, YYJSON_WRITE_PRETTY, NULL, &out_len, &werr);
        if (!out) {
            fprintf(stderr, "JSON write error: %s\n", werr.msg);
        } else {
            // immer auf stdout ausgeben
            fwrite(out, 1, out_len, stdout);
            fputc('\n', stdout);

            // optional: zusätzlich in Datei schreiben
            if (out_path) {
                FILE *fo = fopen(out_path, "wb");
                if (fo) {
                    fwrite(out, 1, out_len, fo);
                    fclose(fo);
                }
            }
            free(out);
        }
    }

    // cleanup (order matters: req.doc may reference `json`)
    if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
    validated_request_free(&req);
    free(json);

    return 0;
}
