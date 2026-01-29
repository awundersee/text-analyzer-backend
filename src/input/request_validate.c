// src/input/request_validate.c
#include "input/request_validate.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static bool json_get_bool(yyjson_val *obj, const char *key, bool def) {
    if (!obj || !yyjson_is_obj(obj)) return def;
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!v) return def;
    if (yyjson_is_bool(v)) return yyjson_get_bool(v);
    return def;
}

static void set_err(req_error_t *e, int status, const char *msg) {
    if (!e) return;
    e->status_code = status;
    e->message = msg;
}

bool request_parse_and_validate(char *json,
                                 size_t json_len,
                                 const req_validate_cfg_t *cfg,
                                 validated_request_t *out,
                                 req_error_t *err) {
    if (!json || json_len == 0 || !cfg || !out) {
        set_err(err, 400, "missing request body");
        return false;
    }

    memset(out, 0, sizeof(*out));

    if (cfg->max_bytes > 0 && json_len > cfg->max_bytes) {
        set_err(err, 413, "payload too large");
        return false;
    }

    yyjson_read_err rerr;
    yyjson_doc *doc = yyjson_read_opts(json, json_len, 0, NULL, &rerr);
    if (!doc) {
        set_err(err, 400, "invalid JSON");
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *pages = NULL;

    // defaults
    out->include_bigrams  = cfg->default_include_bigrams;
    out->per_page_results = cfg->default_per_page_results;
    out->domain = NULL;
    out->has_pipeline_from_options = false;
    out->pipeline_from_options = APP_PIPELINE_AUTO;

    if (yyjson_is_obj(root)) {
        // domain (optional)
        yyjson_val *d = yyjson_obj_get(root, "domain");
        if (d && yyjson_is_str(d)) out->domain = yyjson_get_str(d);

        // options
        yyjson_val *opt = yyjson_obj_get(root, "options");
        out->include_bigrams  = json_get_bool(opt, "includeBigrams", out->include_bigrams);
        out->per_page_results = json_get_bool(opt, "perPageResults", out->per_page_results);

        if (cfg->allow_options_pipeline && opt && yyjson_is_obj(opt)) {
            yyjson_val *p = yyjson_obj_get(opt, "pipeline");
            if (p && yyjson_is_str(p)) {
                int ok = 1;
                app_pipeline_t pl = app_pipeline_from_str(yyjson_get_str(p), &ok);
                if (!ok) {
                    yyjson_doc_free(doc);
                    set_err(err, 400, "invalid options.pipeline (use auto|string|id)");
                    return false;
                }
                out->has_pipeline_from_options = true;
                out->pipeline_from_options = pl;
            }
        }

        pages = yyjson_obj_get(root, "pages");
        if (!pages || !yyjson_is_arr(pages)) {
            yyjson_doc_free(doc);
            set_err(err, 400, "field 'pages' must be an array");
            return false;
        }
    } else if (yyjson_is_arr(root)) {
        if (!cfg->allow_root_array) {
            yyjson_doc_free(doc);
            set_err(err, 400, "root must be an object");
            return false;
        }
        pages = root;
    } else {
        yyjson_doc_free(doc);
        set_err(err, 400, "root must be an object (with pages) or an array (pages)");
        return false;
    }

    size_t n_pages = yyjson_arr_size(pages);
    if (n_pages == 0) {
        yyjson_doc_free(doc);
        set_err(err, 400, "'pages' must not be empty");
        return false;
    }

    if (cfg->max_pages > 0 && n_pages > cfg->max_pages) {
        yyjson_doc_free(doc);
        set_err(err, 413, "too many pages");
        return false;
    }

    app_page_t *pp = (app_page_t *)calloc(n_pages, sizeof(app_page_t));
    if (!pp) {
        yyjson_doc_free(doc);
        set_err(err, 500, "out of memory");
        return false;
    }

    size_t idx = 0;
    yyjson_val *page;
    yyjson_arr_iter it = yyjson_arr_iter_with(pages);
    while ((page = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(page)) {
            free(pp);
            yyjson_doc_free(doc);
            set_err(err, 400, "each page must be an object");
            return false;
        }

        yyjson_val *t = yyjson_obj_get(page, "text");
        if (!t || !yyjson_is_str(t)) {
            free(pp);
            yyjson_doc_free(doc);
            set_err(err, 400, "each page must have field 'text' (string)");
            return false;
        }

        const char *txt = yyjson_get_str(t);
        size_t len = strlen(txt);

        yyjson_val *jid   = yyjson_obj_get(page, "id");
        yyjson_val *jname = yyjson_obj_get(page, "name");
        yyjson_val *jurl  = yyjson_obj_get(page, "url");

        /* NEW: per-page limit */
        if (cfg->max_page_chars > 0 && len > cfg->max_page_chars) {
            free(pp);
            yyjson_doc_free(doc);
            set_err(err, 413, "page text too large");
            return false;
        }

        /* NEW: total chars limit */
        if (cfg->max_total_chars > 0) {
            if (len > SIZE_MAX - out->chars_total) {
                free(pp);
                yyjson_doc_free(doc);
                set_err(err, 413, "payload too large");
                return false;
            }
            out->chars_total += len;
            if (out->chars_total > cfg->max_total_chars) {
                free(pp);
                yyjson_doc_free(doc);
                set_err(err, 413, "payload too large");
                return false;
            }
        }

        pp[idx].id   = (jid && yyjson_is_int(jid)) ? (long long)yyjson_get_sint(jid) : 0;
        pp[idx].name = (jname && yyjson_is_str(jname)) ? yyjson_get_str(jname) : NULL;
        pp[idx].url  = (jurl  && yyjson_is_str(jurl))  ? yyjson_get_str(jurl)  : NULL;
        pp[idx].text = txt;
        idx++;
    }

    out->doc = doc;
    out->pages = pp;
    out->page_count = n_pages;
    return true;
}

void validated_request_free(validated_request_t *r) {
    if (!r) return;
    if (r->pages) free(r->pages);
    if (r->doc) yyjson_doc_free(r->doc);
    memset(r, 0, sizeof(*r));
}
