// [EXTERN] CivetWeb: HTTP server (MIT)
// [EXTERN] yyjson: JSON parser/serializer (MIT)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "civetweb.h"      // from external/civetweb/include
#include "yyjson.h"        // from external/yyjson/src

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/aggregate.h"
#include "core/bigrams.h"
#include "core/bigram_aggregate.h"
#include "core/topk.h"

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

static void send_json(struct mg_connection *conn, int status_code, const char *json_body) {
    if (!conn || !json_body) return;
    mg_printf(conn,
              "HTTP/1.1 %d OK\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status_code, strlen(json_body), json_body);
}

static void send_json_error(struct mg_connection *conn, int status_code, const char *msg) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "error", msg ? msg : "unknown error");

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

static int parse_k(yyjson_val *root, int def_k) {
    yyjson_val *k = yyjson_obj_get(root, "k");
    if (k && yyjson_is_int(k)) {
        long long v = yyjson_get_sint(k);
        if (v < 1) v = 1;
        if (v > 100) v = 100; // safety cap
        return (int)v;
    }
    return def_k;
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
        send_json_error(conn, 400, "root must be a JSON object");
        return 400;
    }

    yyjson_val *texts = yyjson_obj_get(root, "texts");
    if (!texts || !yyjson_is_arr(texts)) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "field 'texts' must be an array of strings");
        return 400;
    }

    int k = parse_k(root, 10);

    // Collect per-text lists for aggregation
    size_t n_texts = (size_t)yyjson_arr_size(texts);
    if (n_texts == 0) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "texts array is empty");
        return 400;
    }
    if (n_texts > 2000) {
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 400, "too many texts (max 2000)");
        return 400;
    }

    WordCountList *per_words = (WordCountList *)calloc(n_texts, sizeof(WordCountList));
    BigramCountList *per_bigrams = (BigramCountList *)calloc(n_texts, sizeof(BigramCountList));
    if (!per_words || !per_bigrams) {
        free(per_words);
        free(per_bigrams);
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 500, "out of memory");
        return 500;
    }

    const char *stop_path = cfg && cfg->stopwords_path ? cfg->stopwords_path : "data/stopwords_de.txt";

    size_t idx = 0;
    yyjson_val *it = NULL;
    yyjson_arr_iter ai;
    yyjson_arr_iter_init(texts, &ai);
    while ((it = yyjson_arr_iter_next(&ai))) {
        if (!yyjson_is_str(it)) {
            // cleanup
            for (size_t j = 0; j < idx; j++) {
                free_word_counts(&per_words[j]);
                free_bigram_counts(&per_bigrams[j]);
            }
            free(per_words);
            free(per_bigrams);
            yyjson_doc_free(doc);
            free(body);
            send_json_error(conn, 400, "all entries in 'texts' must be strings");
            return 400;
        }

        const char *txt = yyjson_get_str(it);
        if (!txt) txt = "";

        TokenList tl = tokenize(txt);

        // stopwords are optional; if file missing we return error (strict)
        int rc = filter_stopwords(&tl, stop_path);
        if (rc != 0) {
            free_tokens(&tl);
            for (size_t j = 0; j < idx; j++) {
                free_word_counts(&per_words[j]);
                free_bigram_counts(&per_bigrams[j]);
            }
            free(per_words);
            free(per_bigrams);
            yyjson_doc_free(doc);
            free(body);
            send_json_error(conn, 500, "stopwords file could not be loaded (check STOPWORDS_FILE or data/stopwords_de.txt)");
            return 500;
        }

        per_words[idx] = count_words(&tl);
        per_bigrams[idx] = count_bigrams(&tl);

        free_tokens(&tl);
        idx++;
    }

    // Aggregate
    WordCountList agg_words = aggregate_word_counts(per_words, idx);
    BigramCountList agg_bigrams = aggregate_bigram_counts(per_bigrams, idx);

    for (size_t j = 0; j < idx; j++) {
        free_word_counts(&per_words[j]);
        free_bigram_counts(&per_bigrams[j]);
    }
    free(per_words);
    free(per_bigrams);

    // Top-K
    WordCountList top_words = top_k_words(&agg_words, (size_t)k);
    BigramCountList top_bi = top_k_bigrams(&agg_bigrams, (size_t)k);

    free_aggregated_word_counts(&agg_words);
    free_aggregated_bigram_counts(&agg_bigrams);

    // Build JSON output
    yyjson_mut_doc *odoc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *oroot = yyjson_mut_obj(odoc);
    yyjson_mut_doc_set_root(odoc, oroot);

    yyjson_mut_val *j_words = yyjson_mut_arr(odoc);
    for (size_t i = 0; i < top_words.count; i++) {
        yyjson_mut_val *o = yyjson_mut_obj(odoc);
        yyjson_mut_obj_add_strcpy(odoc, o, "word", top_words.items[i].word ? top_words.items[i].word : "");
        yyjson_mut_obj_add_int(odoc, o, "count", (long long)top_words.items[i].count);
        yyjson_mut_arr_add_val(j_words, o);
    }

    yyjson_mut_obj_add_val(odoc, oroot, "top_words", j_words);

    yyjson_mut_val *j_bi = yyjson_mut_arr(odoc);
    for (size_t i = 0; i < top_bi.count; i++) {
        yyjson_mut_val *o = yyjson_mut_obj(odoc);
        yyjson_mut_obj_add_strcpy(odoc, o, "w1", top_bi.items[i].w1 ? top_bi.items[i].w1 : "");
        yyjson_mut_obj_add_strcpy(odoc, o, "w2", top_bi.items[i].w2 ? top_bi.items[i].w2 : "");
        yyjson_mut_obj_add_int(odoc, o, "count", (long long)top_bi.items[i].count);
        yyjson_mut_arr_add_val(j_bi, o);
    }
    yyjson_mut_obj_add_val(odoc, oroot, "top_bigrams", j_bi);

    const char *out_json = yyjson_mut_write(odoc, 0, NULL);
    if (!out_json) {
        free_top_k_words(&top_words);
        free_top_k_bigrams(&top_bi);

        yyjson_mut_doc_free(odoc);
        yyjson_doc_free(doc);
        free(body);
        send_json_error(conn, 500, "failed to serialize JSON");
        return 500;
    }

    send_json(conn, 200, out_json);

    free_top_k_words(&top_words);
    free_top_k_bigrams(&top_bi);

    free((void *)out_json);
    yyjson_mut_doc_free(odoc);
    yyjson_doc_free(doc);
    free(body);
    return 200;


    free((void *)out_json);
    yyjson_mut_doc_free(odoc);
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
