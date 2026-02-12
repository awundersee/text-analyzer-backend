#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include "civetweb.h"      // from external/civetweb/include
#include "yyjson.h"        // from external/yyjson/src

#include "app/analyze.h"
#include "input/request_validate.h"

typedef struct {
    const char *stopwords_path;
} AppConfig;

/* Request-level timer used to compute runtimeMsTotal. */
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

static const char *get_env_or_default(const char *key, const char *def) {
    const char *v = getenv(key);
    return (v && v[0] != '\0') ? v : def;
}

/* Reads request body into a NUL-terminated buffer.
 * Enforces a hard 10MB limit to protect memory usage.
 */
static int read_request_body(struct mg_connection *conn, char **out_buf, size_t *out_len) {
    if (!conn || !out_buf || !out_len) return 0;

    const struct mg_request_info *ri = mg_get_request_info(conn);
    long long cl = ri ? ri->content_length : -1;

    if (cl < 0) {
        /* No Content-Length: best-effort chunked read (still bounded by allocation growth). */
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
        /* Hard safety limit (API boundary). */
        errno = EFBIG;
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

/* Sends pre-serialized JSON with minimal headers (no caching). */
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

/* Error response helper (keeps API output JSON-shaped). */
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

static int handle_analyze(struct mg_connection *conn, void *cbdata) {
    const AppConfig *cfg = (const AppConfig *)cbdata;

    /* Measurement point: total request runtime (includes parse/validate/analyze/serialize). */
    double t_req0 = now_ms();

    char *body = NULL;
    size_t body_len = 0;

    if (!read_request_body(conn, &body, &body_len) || !body || body_len == 0) {
        free(body);
        if (errno == EFBIG) {
            send_json_error(conn, 413, "payload too large (max 10MB)");
            return 413;
        }
        send_json_error(conn, 400, "missing request body");
        return 400;
    }

    /* Shared boundary validation (API strict profile). */
    req_validate_cfg_t vcfg = {
        .max_pages = 100,
        .max_bytes = 10 * 1024 * 1024,  // optional (read_request_body already enforces 10MB)
        .allow_root_array = false,
        .allow_options_pipeline = true, // request may override pipeline selection
        .default_include_bigrams = true,
        .default_per_page_results = true
    };

    validated_request_t req;
    req_error_t verr = {0};

    /* IMPORTANT: keep `body` buffer alive until validated_request_free(&req). */
    if (!request_parse_and_validate(body, body_len, &vcfg, &req, &verr)) {
        int sc = (verr.status_code >= 100 && verr.status_code <= 599) ? verr.status_code : 400;
        send_json_error(conn, sc, verr.message ? verr.message : "invalid request");
        free(body);
        return sc;
    }

    /* API policy: default Top-K (kept small to limit response size). */
    size_t k = 20;

    /* Pipeline selection: request override if present, otherwise AUTO. */
    app_pipeline_t pipeline = req.has_pipeline_from_options
                                ? req.pipeline_from_options
                                : APP_PIPELINE_AUTO;

    app_analyze_opts_t aopts = {
        .include_bigrams  = req.include_bigrams,
        .per_page_results = req.per_page_results,
        .stopwords_path   = cfg->stopwords_path,
        .top_k            = k,
        .domain           = req.domain,   // pointer into req.doc
        .pipeline         = pipeline,
    };

    /* Core analysis stage (pipeline switch happens in app layer). */
    app_analyze_result_t res = app_analyze_pages(req.pages, req.page_count, &aopts);

    int code = (res.status == 0) ? 200 : res.status;
    if (code < 100 || code > 599) code = 500;

    if (res.status != 0 || !res.response_doc) {
        validated_request_free(&req);
        free(body);

        send_json_error(conn, code, res.message ? res.message : "analysis failed");
        return code;
    }

    /* Inject end-to-end request time into meta (runtimeMsTotal). */
    double t_req1 = now_ms();
    double runtime_total_ms = round3(t_req1 - t_req0);

    yyjson_mut_val *out_root = yyjson_mut_doc_get_root(res.response_doc);
    yyjson_mut_val *out_meta = out_root ? yyjson_mut_obj_get(out_root, "meta") : NULL;

    if (!out_meta || !yyjson_mut_is_obj(out_meta)) {
        out_meta = yyjson_mut_obj(res.response_doc);
        yyjson_mut_obj_add_val(res.response_doc, out_root, "meta", out_meta);
    }

    yyjson_mut_obj_add_real(res.response_doc, out_meta, "runtimeMsTotal", runtime_total_ms);

    const char *out = yyjson_mut_write(res.response_doc, 0, NULL);
    if (!out) {
        yyjson_mut_doc_free(res.response_doc);
        validated_request_free(&req);
        free(body);

        send_json_error(conn, 500, "failed to serialize response");
        return 500;
    }

    send_json(conn, 200, out);

    free((void *)out);
    yyjson_mut_doc_free(res.response_doc);

    /* Cleanup order matters: req.doc may reference body. */
    validated_request_free(&req);
    free(body);

    return 200;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Configuration via env to keep container deployments simple. */
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

    /* Minimal API surface: health + analyze. */
    mg_set_request_handler(ctx, "/health", handle_health, &cfg);
    mg_set_request_handler(ctx, "/analyze", handle_analyze, &cfg);

    printf("API server running on http://localhost:%s\n", port);
    printf("Stopwords file: %s\n", stopwords);
    printf("Endpoints: GET /health, POST /analyze\n");
    fflush(stdout);

    /* Run forever (container-style). */
    for (;;) {
        sleep(1);
    }

    // not reached
    // mg_stop(ctx);
    // return 0;
}
