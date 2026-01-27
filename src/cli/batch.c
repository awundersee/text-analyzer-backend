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

#include "yyjson.h"
#include "app/analyze.h"

static int ends_with_json(const char *name) {
    size_t n = strlen(name);
    return n >= 5 && strcmp(name + (n - 5), ".json") == 0;
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

static int ensure_dir_exists(const char *dir) {
    // mkdir returns 0 on success, -1 on error
    // If it already exists, errno == EEXIST is fine.
    if (mkdir(dir, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static bool json_get_bool(yyjson_val *obj, const char *key, bool fallback) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!v) return fallback;
    if (yyjson_is_bool(v)) return yyjson_get_bool(v);
    return fallback;
}

// Matches your CLI parsing style: {domain?, options?, pages:[{text,id,name,url}] }
typedef struct {
    app_page_t *pages;        // malloc'd array
    size_t count;
    const char *domain;       // pointer into yyjson doc
    bool include_bigrams;
    bool per_page_results;
} cli_input_t;

static int parse_input(yyjson_doc *doc, cli_input_t *out, char *errbuf, size_t errcap) {
    yyjson_val *root = yyjson_doc_get_root(doc);

    cli_input_t in = {0};
    in.include_bigrams = true;
    in.per_page_results = false;
    in.domain = NULL;

    yyjson_val *pages = NULL;

    if (yyjson_is_obj(root)) {
        yyjson_val *d = yyjson_obj_get(root, "domain");
        if (d && yyjson_is_str(d)) in.domain = yyjson_get_str(d);

        yyjson_val *opt = yyjson_obj_get(root, "options");
        if (opt && yyjson_is_obj(opt)) {
            in.include_bigrams = json_get_bool(opt, "includeBigrams", in.include_bigrams);
            in.per_page_results = json_get_bool(opt, "perPageResults", in.per_page_results);
        }

        pages = yyjson_obj_get(root, "pages");
        if (!pages || !yyjson_is_arr(pages)) {
            snprintf(errbuf, errcap, "Input must contain 'pages'[]");
            return -1;
        }
    } else if (yyjson_is_arr(root)) {
        pages = root;
    } else {
        snprintf(errbuf, errcap, "Root must be an object (with pages) or an array (pages)");
        return -1;
    }

    size_t n = yyjson_arr_size(pages);
    if (n == 0) {
        snprintf(errbuf, errcap, "'pages' must not be empty");
        return -1;
    }

    in.pages = (app_page_t *)calloc(n, sizeof(app_page_t));
    if (!in.pages) {
        snprintf(errbuf, errcap, "Out of memory");
        return -1;
    }

    size_t idx = 0;
    yyjson_val *p;
    yyjson_arr_iter iter = yyjson_arr_iter_with(pages);
    while ((p = yyjson_arr_iter_next(&iter))) {
        if (!yyjson_is_obj(p)) {
            snprintf(errbuf, errcap, "'pages' must be an array of objects");
            free(in.pages);
            return -1;
        }

        yyjson_val *t = yyjson_obj_get(p, "text");
        if (!t || !yyjson_is_str(t)) {
            snprintf(errbuf, errcap, "each page must contain a string field 'text'");
            free(in.pages);
            return -1;
        }

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
    *out = in;
    return 0;
}

static int write_error_json(const char *out_path, const char *input_name, const char *message) {
    // Minimal error JSON for batch mode
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

    const char *sw = getenv("STOPWORDS_FILE");
    if (!sw) sw = "data/stopwords_de.txt";

    int had_failure = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!ends_with_json(ent->d_name)) continue;

        char in_path[4096];
        snprintf(in_path, sizeof(in_path), "%s/%s", in_dir, ent->d_name);

        // output: <out_dir>/<file>.result.json
        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "%s/%s.result.json", out_dir, ent->d_name);

        size_t json_len = 0;
        char *json = read_file_all(in_path, &json_len);
        if (!json) {
            fprintf(stderr, "[ERR] read failed: %s\n", in_path);
            write_error_json(out_path, ent->d_name, "Could not read input file");
            had_failure = 1;
            if (!continue_on_error) break;
            continue;
        }

        yyjson_read_err rerr;
        yyjson_doc *doc = yyjson_read_opts(json, json_len, 0, NULL, &rerr);
        free(json);

        if (!doc) {
            char msg[256];
            snprintf(msg, sizeof(msg), "JSON parse error at pos %zu: %s", rerr.pos, rerr.msg);
            fprintf(stderr, "[ERR] %s\n", msg);
            write_error_json(out_path, ent->d_name, msg);
            had_failure = 1;
            if (!continue_on_error) break;
            continue;
        }

        cli_input_t in = {0};
        char perr[256];
        if (parse_input(doc, &in, perr, sizeof(perr)) != 0) {
            fprintf(stderr, "[ERR] %s: %s\n", ent->d_name, perr);
            write_error_json(out_path, ent->d_name, perr);
            yyjson_doc_free(doc);
            had_failure = 1;
            if (!continue_on_error) break;
            continue;
        }

        // FULL output for batch: top_k = 0
        app_analyze_opts_t opts = {
            .include_bigrams  = in.include_bigrams,
            .per_page_results = in.per_page_results,
            .stopwords_path   = sw,
            .top_k            = 0,
            .domain           = in.domain
        };

        app_analyze_result_t res = app_analyze_pages(in.pages, in.count, &opts);

        if (res.status != 0 || !res.response_doc) {
            const char *msg = res.message ? res.message : "Analysis failed";
            fprintf(stderr, "[ERR] analyze %s: %s\n", ent->d_name, msg);
            write_error_json(out_path, ent->d_name, msg);
            had_failure = 1;

            if (res.response_doc) yyjson_mut_doc_free(res.response_doc);
            yyjson_doc_free(doc);
            free(in.pages);

            if (!continue_on_error) break;
            continue;
        }

        if (write_response_doc(out_path, res.response_doc) != 0) {
            fprintf(stderr, "[ERR] write failed: %s\n", out_path);
            write_error_json(out_path, ent->d_name, "Could not write output file");
            had_failure = 1;

            yyjson_mut_doc_free(res.response_doc);
            yyjson_doc_free(doc);
            free(in.pages);

            if (!continue_on_error) break;
            continue;
        }

        printf("[OK] %s -> %s\n", in_path, out_path);

        yyjson_mut_doc_free(res.response_doc);
        yyjson_doc_free(doc);
        free(in.pages);
    }

    closedir(d);
    return had_failure ? 1 : 0;
}
