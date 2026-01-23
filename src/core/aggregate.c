#include "core/aggregate.h"

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

WordCountList aggregate_word_counts(
    const WordCountList *lists,
    size_t list_count
) {
    WordCountList out = (WordCountList){0};
    if (!lists || list_count == 0) return out;

    size_t cap = 16;
    out.items = (WordCount *)calloc(cap, sizeof(WordCount));
    if (!out.items) return (WordCountList){0};

    for (size_t i = 0; i < list_count; i++) {
        const WordCountList *src = &lists[i];
        for (size_t j = 0; j < src->count; j++) {
            const char *word = src->items[j].word;
            size_t cnt = src->items[j].count;

            // linear lookup (G5)
            size_t found = (size_t)-1;
            for (size_t k = 0; k < out.count; k++) {
                if (strcmp(out.items[k].word, word) == 0) {
                    found = k;
                    break;
                }
            }

            if (found != (size_t)-1) {
                out.items[found].count += cnt;
                continue;
            }

            if (out.count == cap) {
                cap *= 2;
                WordCount *tmp =
                    (WordCount *)realloc(out.items, cap * sizeof(WordCount));
                if (!tmp) {
                    free_aggregated_word_counts(&out);
                    return (WordCountList){0};
                }
                memset(tmp + out.count, 0,
                       (cap - out.count) * sizeof(WordCount));
                out.items = tmp;
            }

            out.items[out.count].word = dup_cstr(word);
            out.items[out.count].count = cnt;
            out.count++;
        }
    }

    return out;
}

void free_aggregated_word_counts(WordCountList *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].word);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
