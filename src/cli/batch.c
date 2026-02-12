// src/cli/batch.c
#define _POSIX_C_SOURCE 200809L
#include "cli/batch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

#include "yyjson.h"
#include "input/request_validate.h"

static int ends_with_json(const char *name) {
    size_t n = strlen(name);
    return n >= 5 && strcmp(name + (n - 5), ".json") == 0;
}

/* Reads full JSON input (batch uses file-based requests). */
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

/* Measurement point for batch perf runs (end-to-end per file). */
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

static int ensure_dir_exists(const char *dir) {
    /* mkdir returns 0 on success, -1 on error; EEXIST is fine. */
    if (mkdir(dir, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/* Writes a minimal error response so batch consumers can still parse JSON. */
static int write_error_json(const char *out_path, const char *input_name, const char *message) {
    FILE *f = fopen(out_path, "wb");
    if (!f) return -1;
    fprintf(f,
        "{\n"
        "  \"meta\": { \"status\": \"error\" },\n"
        "  \"input\": \"%s\",\n"
        "  \"error\": \"%s\"\n"
        "}\n",
        input_name ? input_name : "",
        message ? message : "unknown error"
    );
    fclose(f);
    return 0;
}

/* Writes pretty response JSON (batch artifacts are meant to be inspected). */
static int write_response_doc(const char *out_path, yyjson_mut_doc *doc) {
    yyjson_write_err werr;
    size_t out_len = 0;
    char *out = yyjson_mut_write_opts(doc, YYJSON_WRITE_PRETTY, NULL, &out_len, &werr);
    if (!out) {
        return -1;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) { free(out); return -1; }

    fwrite(out, 1, out_len, f);
    fclose(f);
    free(out);
    return 0;
}

int cli_run_batch(const char *in_dir, const char *out_dir, int continue_on_error) {
    if (!in_dir || !out_dir) return 2;

    if (ensure_dir_exists(out_dir) != 0) {
        fprintf(stderr, "[FATAL] cannot create/access out dir '%s': %s\n", out_dir, strerror(errno));
        return 2;
    }

    DIR *d = opendir(in_dir);
    if (!d) {
        fprintf(stderr, "[FATAL] cannot open in dir '%s': %s\n", in_dir, strerror(errno));
        return 2;
    }

    /* Stopwords path is injected for repeatable batch runs. */
    const char *sw = getenv("STOPWORDS_FILE");
    if (!sw) sw = "data/stopwords_de.txt";

    /* Shared boundary validation with CLI/batch relaxed limits. */
    req_validate_cfg_t vcfg = {
        .max_pages = 0,
        .max_bytes = 0,
        .allow_root_array = true,
        .allow_options_pipeline = false, // CLI selects pipeline externally
        .default_include_bigrams = true,
        .default_per_page_results = true
    };

    int had_failure = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!ends_with_json(ent->d_name)) continue;

        char in_path[4096];
        snprintf(in_path, sizeof(in_path), "%s/%s", in_dir, ent->d_name);

        /* Output naming: <out_dir>/<file>.result.json */
        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "%s/%s.result.json", out_dir, ent->d_name);

        /* Measurement point: per-file total runtime. */
        double t0 = now_ms();

        size_t json_len = 0;
        char *json = read_file_all(in_path, &json_len);
        if (!json) {
            fprintf(stderr, "[ERR] read failed: %s\n", in_path);
            write_error_json(out_path, ent->d_name, "Could not read input file");
            had_failure = 1;
            if (!continue_on_error) break;
            continue;
        }

        validated_request_t req;
        req_error_t verr = {0};

        /* IMPORTANT: keep `json` buffer alive until validated_request_free(&req). */
        if (!request_parse_and_validate(json, json_len, &vcfg, &req, &verr)) {
            const char *msg = verr.message ? verr.message : "invalid request";
            fprintf(stderr, "[ERR] %s: %s\n", ent->d_name, msg);
            write_error_json(out_path, ent->d_name, msg);
            free(json);
            had_failure = 1;
            if (!continue_on_error) break;
            continue;
        }

        /* Batch uses full output (top_k=0) for later inspection/aggregation. */
        app_analyze_opts_t opts = {
            .include_bigrams  = req.include_bigrams,
            .per_page_results = req.per_page_results,
            .stopwords_path   = sw,
            .top_k            = 0,
            .domain           = req.domain,
            .pipeline         = APP_PIPELINE_AUTO
        };

        app_analyze_result_t res = app_analyze_pages(req.pages, req.page_count, &opts);

        double t1 = now_ms();
        double runtime_total_ms = round3(t1 - t0);

        /* Inject per-file end-to-end runtime into meta (batch-only metric). */
        if (res.response_doc) {
            yyjson_mut_val *root = yyjson_mut_doc_get_root(res.response_doc);
            yyjson_mut_val *meta = root ? yyjson_mut_obj_get(root, "meta") : NULL;

            if (!meta || !yyjson_mut_is_obj(meta)) {
                meta = yyjson_mut_obj(res.response_doc);
                yyjson_mut_obj_add_val(res.response_doc, root, "meta", meta);
            }

            yyjson_mut_obj_add_real(res.response_doc, meta, "runtimeMsTotal", runtime_total_ms);
        }

        if (res.status != 0 || !res.response_doc) {
            const char *msg = res.message ? res.message : "Analysis failed";
            fprintf(stderr, "[ERR] analyze %s: %s\n", ent->d_name, msg);
            write_error_json(out_path, ent->d_name, msg);
            had_failure = 1;

            if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
            validated_request_free(&req);
            free(json);

            if (!continue_on_error) break;
            continue;
        }

        if (write_response_doc(out_path, res.response_doc) != 0) {
            fprintf(stderr, "[ERR] write failed: %s\n", out_path);
            write_error_json(out_path, ent->d_name, "Could not write output file");
            had_failure = 1;

            yyjson_mut_doc_free(res.response_doc);
            validated_request_free(&req);
            free(json);

            if (!continue_on_error) break;
            continue;
        }

        printf("[OK] %s -> %s\n", in_path, out_path);

        /* Cleanup order matters: validated_request_t may reference json/doc strings. */
        yyjson_mut_doc_free(res.response_doc);
        validated_request_free(&req);
        free(json);
    }

    closedir(d);
    return had_failure ? 1 : 0;
}
