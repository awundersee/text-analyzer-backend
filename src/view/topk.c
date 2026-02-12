#include "view/topk.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>

/* Perf probe for Top-K steps (enabled via PERF_TOPK env var). */
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* Deep-copy helper; returned lists own their strings. */
static char *dup_cstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

/* ---------- Word sorting ---------- */

/* Deterministic ordering:
 * - primary: count DESC
 * - tie: word ASC
 */
static int cmp_wordcount_desc_then_word_asc(const void *a, const void *b) {
    const WordCount *wa = (const WordCount *)a;
    const WordCount *wb = (const WordCount *)b;

    if (wa->count < wb->count) return 1;   // desc
    if (wa->count > wb->count) return -1;

    if (!wa->word && !wb->word) return 0;
    if (!wa->word) return 1;
    if (!wb->word) return -1;
    return strcmp(wa->word, wb->word);
}

WordCountList top_k_words(const WordCountList *list, size_t k) {
    WordCountList out = (WordCountList){0};
    if (!list || !list->items || list->count == 0 || k == 0) return out;

    /* Optional perf instrumentation:
     * copy -> sort -> move-to-output (ownership transfer).
     */
    const char *perf = getenv("PERF_TOPK");
    uint64_t t0=0, t_copy=0, t_sort=0, t_out=0;

    if (perf) t0 = now_ns();

    /* Copy all items so we can sort without mutating the input list. */
    WordCount *tmp = (WordCount *)calloc(list->count, sizeof(WordCount));
    if (!tmp) return out;

    for (size_t i = 0; i < list->count; i++) {
        tmp[i].word = dup_cstr(list->items[i].word);
        tmp[i].count = list->items[i].count;
        if (list->items[i].word && !tmp[i].word) {
            for (size_t j = 0; j < i; j++) free(tmp[j].word);
            free(tmp);
            return (WordCountList){0};
        }
    }

    if (perf) t_copy = now_ns();

    qsort(tmp, list->count, sizeof(WordCount), cmp_wordcount_desc_then_word_asc);

    if (perf) t_sort = now_ns();

    size_t n = list->count < k ? list->count : k;

    /* Output list takes ownership of the top-n copied strings. */
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

    for (size_t i = 0; i < list->count; i++) free(tmp[i].word);
    free(tmp);

    if (perf) {
        t_out = now_ns();
        fprintf(stderr,
            "PERF_TOPK words total_ns=%" PRIu64 " copy_ns=%" PRIu64 " sort_ns=%" PRIu64
            " out_ns=%" PRIu64 " n=%zu k=%zu\n",
            (t_out - t0),
            (t_copy - t0),
            (t_sort - t_copy),
            (t_out - t_sort),
            list->count, k
        );
    }

    return out;
}

/* Top-K outputs share the same free routine as regular WordCountList. */
void free_top_k_words(WordCountList *list) {
    free_word_counts(list);
}

/* ---------- Bigram sorting ---------- */

/* Deterministic ordering:
 * - primary: count DESC
 * - tie: w1 ASC, then w2 ASC
 */
static int cmp_bigram_desc_then_lex(const void *a, const void *b) {
    const BigramCount *ba = (const BigramCount *)a;
    const BigramCount *bb = (const BigramCount *)b;

    if (ba->count < bb->count) return 1;   // desc
    if (ba->count > bb->count) return -1;

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

    const char *perf = getenv("PERF_TOPK");
    uint64_t t0=0, t_copy=0, t_sort=0, t_out=0;

    if (perf) t0 = now_ns();

    /* Copy all items so we can sort without mutating the input list. */
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

    if (perf) t_copy = now_ns();

    qsort(tmp, list->count, sizeof(BigramCount), cmp_bigram_desc_then_lex);

    if (perf) t_sort = now_ns();

    size_t n = list->count < k ? list->count : k;

    /* Output list takes ownership of the top-n copied strings. */
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

    if (perf) {
        t_out = now_ns();
        fprintf(stderr,
            "PERF_TOPK bigrams total_ns=%" PRIu64 " copy_ns=%" PRIu64 " sort_ns=%" PRIu64
            " out_ns=%" PRIu64 " n=%zu k=%zu\n",
            (uint64_t)(t_out - t0),
            (uint64_t)(t_copy - t0),
            (uint64_t)(t_sort - t_copy),
            (uint64_t)(t_out - t_sort),
            list->count, k
        );
    }

    return out;
}

/* Top-K outputs share the same free routine as regular BigramCountList. */
void free_top_k_bigrams(BigramCountList *list) {
    free_bigram_counts(list);
}
