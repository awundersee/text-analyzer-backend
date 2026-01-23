#include "core/topk.h"

#include <stdlib.h>
#include <string.h>

static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

/* ---------- Word sorting ---------- */

static int cmp_wordcount_desc_then_word_asc(const void *a, const void *b) {
    const WordCount *wa = (const WordCount *)a;
    const WordCount *wb = (const WordCount *)b;

    if (wa->count < wb->count) return 1;   // desc
    if (wa->count > wb->count) return -1;

    // tie-break: word asc
    if (!wa->word && !wb->word) return 0;
    if (!wa->word) return 1;
    if (!wb->word) return -1;
    return strcmp(wa->word, wb->word);
}

WordCountList top_k_words(const WordCountList *list, size_t k) {
    WordCountList out = (WordCountList){0};
    if (!list || !list->items || list->count == 0 || k == 0) return out;

    // copy all
    WordCount *tmp = (WordCount *)calloc(list->count, sizeof(WordCount));
    if (!tmp) return out;

    for (size_t i = 0; i < list->count; i++) {
        tmp[i].word = dup_cstr(list->items[i].word);
        tmp[i].count = list->items[i].count;
        if (list->items[i].word && !tmp[i].word) {
            // cleanup
            for (size_t j = 0; j < i; j++) free(tmp[j].word);
            free(tmp);
            return (WordCountList){0};
        }
    }

    qsort(tmp, list->count, sizeof(WordCount), cmp_wordcount_desc_then_word_asc);

    size_t n = list->count < k ? list->count : k;

    out.items = (WordCount *)calloc(n, sizeof(WordCount));
    if (!out.items) {
        for (size_t i = 0; i < list->count; i++) free(tmp[i].word);
        free(tmp);
        return (WordCountList){0};
    }
    out.count = n;

    for (size_t i = 0; i < n; i++) {
        out.items[i].word = tmp[i].word;  // take ownership
        out.items[i].count = tmp[i].count;
        tmp[i].word = NULL;
    }

    // cleanup remaining temp
    for (size_t i = 0; i < list->count; i++) free(tmp[i].word);
    free(tmp);

    return out;
}

void free_top_k_words(WordCountList *list) {
    free_word_counts(list);
}

/* ---------- Bigram sorting ---------- */

static int cmp_bigram_desc_then_lex(const void *a, const void *b) {
    const BigramCount *ba = (const BigramCount *)a;
    const BigramCount *bb = (const BigramCount *)b;

    if (ba->count < bb->count) return 1;   // desc
    if (ba->count > bb->count) return -1;

    // tie-break: w1 asc, then w2 asc
    const char *a1 = ba->w1 ? ba->w1 : "";
    const char *b1 = bb->w1 ? bb->w1 : "";
    int c1 = strcmp(a1, b1);
    if (c1 != 0) return c1;

    const char *a2 = ba->w2 ? ba->w2 : "";
    const char *b2 = bb->w2 ? bb->w2 : "";
    return strcmp(a2, b2);
}

BigramCountList top_k_bigrams(const BigramCountList *list, size_t k) {
    BigramCountList out = (BigramCountList){0};
    if (!list || !list->items || list->count == 0 || k == 0) return out;

    BigramCount *tmp = (BigramCount *)calloc(list->count, sizeof(BigramCount));
    if (!tmp) return out;

    for (size_t i = 0; i < list->count; i++) {
        tmp[i].w1 = dup_cstr(list->items[i].w1);
        tmp[i].w2 = dup_cstr(list->items[i].w2);
        tmp[i].count = list->items[i].count;

        if ((list->items[i].w1 && !tmp[i].w1) || (list->items[i].w2 && !tmp[i].w2)) {
            for (size_t j = 0; j <= i; j++) {
                free(tmp[j].w1);
                free(tmp[j].w2);
            }
            free(tmp);
            return (BigramCountList){0};
        }
    }

    qsort(tmp, list->count, sizeof(BigramCount), cmp_bigram_desc_then_lex);

    size_t n = list->count < k ? list->count : k;

    out.items = (BigramCount *)calloc(n, sizeof(BigramCount));
    if (!out.items) {
        for (size_t i = 0; i < list->count; i++) {
            free(tmp[i].w1);
            free(tmp[i].w2);
        }
        free(tmp);
        return (BigramCountList){0};
    }
    out.count = n;

    for (size_t i = 0; i < n; i++) {
        out.items[i].w1 = tmp[i].w1; tmp[i].w1 = NULL;
        out.items[i].w2 = tmp[i].w2; tmp[i].w2 = NULL;
        out.items[i].count = tmp[i].count;
    }

    for (size_t i = 0; i < list->count; i++) {
        free(tmp[i].w1);
        free(tmp[i].w2);
    }
    free(tmp);

    return out;
}

void free_top_k_bigrams(BigramCountList *list) {
    free_bigram_counts(list);
}
