// [EXTERN] CivetWeb: HTTP server (MIT)
// [EXTERN] yyjson: JSON parser/serializer (MIT)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "civetweb.h"      // from external/civetweb/include
#include "yyjson.h"        // from external/yyjson/src

#include "app/analyze.h"

typedef struct {
    const char *stopwords_path;
} AppConfig;

static const char *get_env_or_default(const char *key, const char *def) {
    const char *v = getenv(key);
    return (v && v[0] != '\0') ? v : def;
}

static int read_request_body(struct mg_connection *conn, char **out_buf, size_t *out_len) {
    if (!conn || !out_buf || !out_len) return 0;

    const struct mg_request_info *ri = mg_get_request_info(conn);
    long long cl = ri ? ri->content_length : -1;

    if (cl < 0) {
        // no content-length provided; read in chunks until EOF (best effort)
        size_t cap = 8192;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf) return 0;

        for (;;) {
            char tmp[4096];
            int n = mg_read(conn, tmp, (int)sizeof(tmp));
            if (n <= 0) break;
            if (len + (size_t)n + 1 > cap) {
                size_t ncap = cap * 2;
                while (ncap < len + (size_t)n + 1) ncap *= 2;
                char *nbuf = (char *)realloc(buf, ncap);
                if (!nbuf) {
                    free(buf);
                    return 0;
                }
                buf = nbuf;
                cap = ncap;
            }
            memcpy(buf + len, tmp, (size_t)n);
            len += (size_t)n;
        }
        buf[len] = '\0';
        *out_buf = buf;
        *out_len = len;
        return 1;
    }

    if (cl > 10 * 1024 * 1024) {
        // 10MB safety limit
        return 0;
    }

    size_t len = (size_t)cl;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;

    size_t off = 0;
    while (off < len) {
        int n = mg_read(conn, buf + off, (int)(len - off));
        if (n <= 0) break;
        off += (size_t)n;
    }
    buf[off] = '\0';

    *out_buf = buf;
    *out_len = off;
    return 1;
}

static const char *reason_phrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

static void send_json(struct mg_connection *conn, int status_code, const char *json_body) {
    if (!conn || !json_body) return;
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status_code, reason_phrase(status_code), strlen(json_body), json_body);
}

static void send_json_error(struct mg_connection *conn, int status_code, const char *msg) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "message", msg ? msg : "unknown error");

    const char *json = yyjson_mut_write(doc, 0, NULL);
    if (json) {
        send_json(conn, status_code, json);
        free((void *)json);
    } else {
        send_json(conn, status_code, "{\"error\":\"serialization failed\"}");
    }
    yyjson_mut_doc_free(doc);
}

static int handle_health(struct mg_connection *conn, void *cbdata) {
    (void)cbdata;
    send_json(conn, 200, "{\"status\":\"ok\"}");
    return 200;
}

static bool json_get_bool(yyjson_val *obj, const char *key, bool def) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!v) return def;
    if (yyjson_is_bool(v)) return yyjson_get_bool(v);
    return def;
}

static int handle_analyze(struct mg_connection *conn, void *cbdata) {
    const AppConfig *cfg = (const AppConfig *)cbdata;

    char *body = NULL;
    size_t body_len = 0;
    if (!read_request_body(conn, &body, &body_len) || !body || body_len == 0) {
        free(body);
        send_json_error(conn, 400, "missing request body");
        return 400;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts(body, body_len, 0, NULL, &err);
    if (!doc) {
        free(body);
        send_json_error(conn, 400, "invalid JSON");
        return 400;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "root must be an object");
        return 400;
    }

    // pages[]
    yyjson_val *pages = yyjson_obj_get(root, "pages");
    if (!pages || !yyjson_is_arr(pages)) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "field 'pages' must be an array");
        return 400;
    }

    size_t n_pages = yyjson_arr_size(pages);
    if (n_pages == 0) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "'pages' must not be empty");
        return 400;
    }
    if (n_pages > 100) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 413, "too many pages (max 100)");
        return 413;
    }

    // options
    bool include_bigrams = true;
    bool per_page_results = false;
    yyjson_val *opt = yyjson_obj_get(root, "options");
    if (opt && yyjson_is_obj(opt)) {
        include_bigrams = json_get_bool(opt, "includeBigrams", include_bigrams);
        per_page_results = json_get_bool(opt, "perPageResults", per_page_results);
    }

    // k (TopK) – API default 20
    int k = 20;

    // domain (optional)
    const char *domain = NULL;
    yyjson_val *jd = yyjson_obj_get(root, "domain");
    if (jd && yyjson_is_str(jd)) domain = yyjson_get_str(jd);

    // build app_page_t[]
    app_page_t *pp = (app_page_t *)calloc(n_pages, sizeof(app_page_t));
    if (!pp) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 500, "out of memory");
        return 500;
    }

    size_t idx = 0;
    yyjson_val *page;
    yyjson_arr_iter it = yyjson_arr_iter_with(pages);
    while ((page = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(page)) {
            free(pp);
            yyjson_doc_free(doc);
            free(body);
            send_json_error(conn, 400, "each page must be an object");
            return 400;
        }

        yyjson_val *t = yyjson_obj_get(page, "text");
        if (!t || !yyjson_is_str(t)) {
            free(pp);
            yyjson_doc_free(doc);
            free(body);
            send_json_error(conn, 400, "each page must have field 'text' (string)");
            return 400;
        }

        yyjson_val *jid   = yyjson_obj_get(page, "id");
        yyjson_val *jname = yyjson_obj_get(page, "name");
        yyjson_val *jurl  = yyjson_obj_get(page, "url");

        pp[idx].id   = (jid && yyjson_is_int(jid)) ? (long long)yyjson_get_sint(jid) : 0;
        pp[idx].name = (jname && yyjson_is_str(jname)) ? yyjson_get_str(jname) : NULL;
        pp[idx].url  = (jurl  && yyjson_is_str(jurl))  ? yyjson_get_str(jurl)  : NULL;
        pp[idx].text = yyjson_get_str(t);
        idx++;
    }

    // call app layer
    app_analyze_opts_t aopts = {
        .include_bigrams  = include_bigrams,
        .per_page_results = per_page_results,
        .stopwords_path   = cfg->stopwords_path,
        .top_k            = (size_t)k,   // API: TopK
        .domain           = domain
    };

    app_analyze_result_t res = app_analyze_pages(pp, idx, &aopts);

    free(pp);

    if (res.status != 0 || !res.response_doc) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 500, res.message ? res.message : "analysis failed");
        return 500;
    }

    const char *out = yyjson_mut_write(res.response_doc, 0, NULL);
    if (!out) {
        yyjson_mut_doc_free(res.response_doc);
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 500, "failed to serialize response");
        return 500;
    }

    send_json(conn, 200, out);

    free((void *)out);
    yyjson_mut_doc_free(res.response_doc);
    yyjson_doc_free(doc);
    free(body);
    return 200;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *port = get_env_or_default("PORT", "8080");
    const char *stopwords = get_env_or_default("STOPWORDS_FILE", "data/stopwords_de.txt");

    AppConfig cfg = { stopwords };

    const char *options[] = {
        "listening_ports", port,
        "num_threads", "4",
        "request_timeout_ms", "10000",
        "enable_keep_alive", "yes",
        0
    };

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    struct mg_context *ctx = mg_start(&callbacks, NULL, options);
    if (!ctx) {
        fprintf(stderr, "Failed to start server on port %s\n", port);
        return 1;
    }

    mg_set_request_handler(ctx, "/health", handle_health, &cfg);
    mg_set_request_handler(ctx, "/analyze", handle_analyze, &cfg);

    printf("API server running on http://localhost:%s\n", port);
    printf("Stopwords file: %s\n", stopwords);
    printf("Endpoints: GET /health, POST /analyze\n");
    fflush(stdout);

    // Run forever
    for (;;) {
        sleep(1);
    }

    // not reached
    // mg_stop(ctx);
    // return 0;
}
