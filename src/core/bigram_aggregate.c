#include "core/bigram_aggregate.h"

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

static int bigram_equals_parts(const BigramCount *b, const char *w1, const char *w2) {
    return b && b->w1 && b->w2 && w1 && w2 &&
           strcmp(b->w1, w1) == 0 && strcmp(b->w2, w2) == 0;
}

BigramCountList aggregate_bigram_counts(
    const BigramCountList *lists,
    size_t list_count
) {
    BigramCountList out = (BigramCountList){0};
    if (!lists || list_count == 0) return out;

    size_t cap = 16;
    out.items = (BigramCount *)calloc(cap, sizeof(BigramCount));
    if (!out.items) return (BigramCountList){0};

    for (size_t i = 0; i < list_count; i++) {
        const BigramCountList *src = &lists[i];
        for (size_t j = 0; j < src->count; j++) {
            const char *w1 = src->items[j].w1;
            const char *w2 = src->items[j].w2;
            size_t cnt = src->items[j].count;
            if (!w1 || !w2 || w1[0] == '\0' || w2[0] == '\0') continue;
            if (cnt == 0) continue;

            // linear search
            size_t found = (size_t)-1;
            for (size_t k = 0; k < out.count; k++) {
                if (bigram_equals_parts(&out.items[k], w1, w2)) {
                    found = k;
                    break;
                }
            }

            if (found != (size_t)-1) {
                out.items[found].count += cnt;
                continue;
            }

            // new bigram
            if (out.count == cap) {
                cap *= 2;
                BigramCount *tmp = (BigramCount *)realloc(out.items, cap * sizeof(BigramCount));
                if (!tmp) {
                    free_aggregated_bigram_counts(&out);
                    return (BigramCountList){0};
                }
                memset(tmp + out.count, 0, (cap - out.count) * sizeof(BigramCount));
                out.items = tmp;
            }

            out.items[out.count].w1 = dup_cstr(w1);
            out.items[out.count].w2 = dup_cstr(w2);
            if (!out.items[out.count].w1 || !out.items[out.count].w2) {
                free_aggregated_bigram_counts(&out);
                return (BigramCountList){0};
            }
            out.items[out.count].count = cnt;
            out.count++;
        }
    }

    return out;
}

void free_aggregated_bigram_counts(BigramCountList *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].w1);
        free(list->items[i].w2);
        list->items[i].w1 = NULL;
        list->items[i].w2 = NULL;
        list->items[i].count = 0;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
