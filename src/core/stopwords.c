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

int filter_stopwords(TokenList *tokens, const char *stopwords_file_path) {
    if (!tokens || !tokens->items || tokens->count == 0) return 0;
    if (!stopwords_file_path) return -1;

    FILE *f = fopen(stopwords_file_path, "r");
    if (!f) return -2;

    // Load stopwords into dynamic array (simple, good enough for G3)
    size_t cap = 64;
    size_t sw_count = 0;
    char **stopwords = (char **)calloc(cap, sizeof(char *));
    if (!stopwords) {
        fclose(f);
        return -3;
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
                // cleanup
                for (size_t i = 0; i < sw_count; i++) free(stopwords[i]);
                free(stopwords);
                fclose(f);
                return -4;
            }
            stopwords = tmp;
        }

        stopwords[sw_count] = strdup(line);
        if (!stopwords[sw_count]) {
            for (size_t i = 0; i < sw_count; i++) free(stopwords[i]);
            free(stopwords);
            fclose(f);
            return -5;
        }
        sw_count++;
    }
    fclose(f);

    // Filter tokens in-place
    size_t write = 0;
    for (size_t read = 0; read < tokens->count; read++) {
        char *tok = tokens->items[read];
        if (!tok) continue;

        // token is already lowercase from tokenizer for ASCII letters, but:
        // - drop very short tokens (e.g. "e")
        // - drop numeric-only tokens (e.g. "1", "2025")
        const size_t MIN_TOKEN_LEN = 2;

        if (is_too_short(tok, MIN_TOKEN_LEN) || is_all_digits(tok) ||
            is_stopword_linear(tok, stopwords, sw_count)) {
            free(tok);
            tokens->items[read] = NULL;
        } else {
            tokens->items[write++] = tok;
        }

    }

    // Null out remaining slots
    for (size_t k = write; k < tokens->count; k++) {
        tokens->items[k] = NULL;
    }
    tokens->count = write;

    // cleanup stopwords
    for (size_t i = 0; i < sw_count; i++) free(stopwords[i]);
    free(stopwords);

    return 0;
}
