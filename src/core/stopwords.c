#include "core/stopwords.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rstrip_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void to_lower_ascii(char *s) {
    if (!s) return;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') *s = (char)(c - 'A' + 'a');
    }
}

// strdup ist POSIX; falls du strikt ISO-C willst, ersetze durch malloc+memcpy
static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static int is_all_digits(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return 0;
    }
    return 1;
}

static int is_too_short(const char *s, size_t min_len) {
    if (!s) return 1;
    return strlen(s) < min_len;
}

static int is_stopword_linear(const char *token, char **stopwords, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(token, stopwords[i]) == 0) return 1;
    }
    return 0;
}

static int should_drop_token(const char *tok, const StopwordList *sw) {
    const size_t MIN_TOKEN_LEN = 2;

    if (!tok || tok[0] == '\0') return 1;
    if (is_too_short(tok, MIN_TOKEN_LEN)) return 1;
    if (is_all_digits(tok)) return 1;
    if (sw && stopwords_contains(sw, tok)) return 1;

    return 0;
}

/* ---------- StopwordList API ---------- */

int stopwords_load(StopwordList *out, const char *stopwords_file_path) {
    if (!out) return -1;
    out->items = NULL;
    out->count = 0;

    if (!stopwords_file_path) return -2;

    FILE *f = fopen(stopwords_file_path, "r");
    if (!f) return -3;

    size_t cap = 64;
    size_t sw_count = 0;
    char **stopwords = (char **)calloc(cap, sizeof(char *));
    if (!stopwords) {
        fclose(f);
        return -4;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        rstrip_newline(line);
        to_lower_ascii(line);
        if (line[0] == '\0') continue;

        if (sw_count == cap) {
            cap *= 2;
            char **tmp = (char **)realloc(stopwords, cap * sizeof(char *));
            if (!tmp) {
                for (size_t i = 0; i < sw_count; i++) free(stopwords[i]);
                free(stopwords);
                fclose(f);
                return -5;
            }
            stopwords = tmp;
        }

        stopwords[sw_count] = dup_cstr(line);
        if (!stopwords[sw_count]) {
            for (size_t i = 0; i < sw_count; i++) free(stopwords[i]);
            free(stopwords);
            fclose(f);
            return -6;
        }
        sw_count++;
    }

    fclose(f);

    out->items = stopwords;
    out->count = sw_count;
    return 0;
}

void stopwords_free(StopwordList *sw) {
    if (!sw || !sw->items) return;
    for (size_t i = 0; i < sw->count; i++) {
        free(sw->items[i]);
    }
    free(sw->items);
    sw->items = NULL;
    sw->count = 0;
}

int stopwords_contains(const StopwordList *sw, const char *token) {
    if (!sw || !sw->items || sw->count == 0 || !token) return 0;
    return is_stopword_linear(token, sw->items, sw->count);
}

int filter_stopwords(TokenList *tokens, const char *stopwords_file_path) {
    if (!tokens || !tokens->items || tokens->count == 0) return 0;

    StopwordList sw = {0};
    int rc = stopwords_load(&sw, stopwords_file_path);
    if (rc != 0) return rc;

    size_t write = 0;
    for (size_t read = 0; read < tokens->count; read++) {
        char *tok = tokens->items[read];
        if (!tok) continue;

        if (should_drop_token(tok, &sw)) {
            free(tok);
            tokens->items[read] = NULL;
        } else {
            tokens->items[write++] = tok;
        }
    }

    for (size_t k = write; k < tokens->count; k++) {
        tokens->items[k] = NULL;
    }
    tokens->count = write;

    stopwords_free(&sw);
    return 0;
}

TokenList filter_stopwords_copy(const TokenList *in, const char *stopwords_file_path) {
    TokenList out = (TokenList){0};
    if (!in || !in->items || in->count == 0) return out;

    StopwordList sw = {0};
    if (stopwords_load(&sw, stopwords_file_path) != 0) {
        return (TokenList){0};
    }

    // 1) count allowed tokens
    size_t allowed = 0;
    for (size_t i = 0; i < in->count; i++) {
        const char *tok = in->items[i];
        if (!should_drop_token(tok, &sw)) allowed++;
    }

    if (allowed == 0) {
        stopwords_free(&sw);
        return out;
    }

    out.items = (char **)calloc(allowed, sizeof(char *));
    if (!out.items) {
        stopwords_free(&sw);
        return (TokenList){0};
    }

    // 2) duplicate allowed tokens
    size_t wi = 0;
    for (size_t i = 0; i < in->count; i++) {
        const char *tok = in->items[i];
        if (should_drop_token(tok, &sw)) continue;

        out.items[wi] = dup_cstr(tok);
        if (!out.items[wi]) {
            out.count = wi;
            free_tokens(&out);       // nutzt deinen tokenizer-free, passt hier
            stopwords_free(&sw);
            return (TokenList){0};
        }
        wi++;
    }

    out.count = wi;
    stopwords_free(&sw);
    return out;
}
