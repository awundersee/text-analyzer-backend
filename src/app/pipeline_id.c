#include "app/pipeline_id.h"

#include <stdlib.h>
#include <string.h>

#include "core/dict.h"
#include "core/id_freq.h"
#include "core/id_bigrams.h"

// helper: duplicate
static char *dup_cstr(const char *s) {
  size_t n = strlen(s);
  char *out = (char*)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static int append_word(WordCountList *list, const char *word, uint32_t count) {
  size_t n = list->count + 1;
  WordCount *nw = (WordCount*)realloc(list->items, n * sizeof(WordCount));
  if (!nw) return 0;
  list->items = nw;
  list->items[list->count].word = dup_cstr(word);
  if (!list->items[list->count].word) return 0;
  list->items[list->count].count = (int)count;
  list->count = n;
  return 1;
}

static int append_bigram(BigramCountList *list, const char *w1, const char *w2, uint32_t count) {
  size_t n = list->count + 1;
  BigramCount *nb = (BigramCount*)realloc(list->items, n * sizeof(BigramCount));
  if (!nb) return 0;
  list->items = nb;
  list->items[list->count].w1 = dup_cstr(w1);
  list->items[list->count].w2 = dup_cstr(w2);
  if (!list->items[list->count].w1 || !list->items[list->count].w2) return 0;
  list->items[list->count].count = (int)count;
  list->count = n;
  return 1;
}

int analyze_id_pipeline(
  const TokenList *tokens,
  int include_bigrams,
  WordCountList *out_words,
  BigramCountList *out_bigrams
) {
  if (!tokens || !out_words) return 0;
  *out_words = (WordCountList){0};
  if (out_bigrams) *out_bigrams = (BigramCountList){0};

  Dict dict;
  if (!dict_init(&dict, tokens->count * 2 + 16)) return 0;

  IdFreq wf;
  if (!idfreq_init(&wf, 1024)) { dict_free(&dict); return 0; }

  IdBigrams bg;
  if (include_bigrams) {
    if (!idbigrams_init(&bg, tokens->count * 2 + 64)) {
      idfreq_free(&wf);
      dict_free(&dict);
      return 0;
    }
  }

  uint32_t prev = 0;
  for (size_t i = 0; i < tokens->count; i++) {
    const char *t = tokens->items[i];
    if (!t || !*t) continue;

    uint32_t id = dict_get_or_add(&dict, t);
    if (id == 0) { /* OOM */ goto fail; }
    if (!idfreq_inc(&wf, id)) goto fail;

    if (include_bigrams) {
      if (prev != 0) {
        if (!idbigrams_inc(&bg, prev, id)) goto fail;
      }
      prev = id;
    }
  }

  // export words
  for (uint32_t id = 1; id <= (uint32_t)dict_size(&dict); id++) {
    uint32_t c = idfreq_get(&wf, id);
    if (!c) continue;
    const char *w = dict_word(&dict, id);
    if (!append_word(out_words, w, c)) goto fail;
  }

  // export bigrams
  if (include_bigrams && out_bigrams) {
    for (size_t i = 0; i < bg.cap; i++) {
      if (!bg.entries[i].used) continue;
      uint64_t key = bg.entries[i].key;
      uint32_t id1 = (uint32_t)(key >> 32);
      uint32_t id2 = (uint32_t)(key & 0xffffffffu);
      const char *w1 = dict_word(&dict, id1);
      const char *w2 = dict_word(&dict, id2);
      if (!w1 || !w2) continue;
      if (!append_bigram(out_bigrams, w1, w2, bg.entries[i].count)) goto fail;
    }
  }

  if (include_bigrams) idbigrams_free(&bg);
  idfreq_free(&wf);
  dict_free(&dict);
  return 1;

fail:
  if (include_bigrams) idbigrams_free(&bg);
  idfreq_free(&wf);
  dict_free(&dict);
  free_word_counts(out_words);
  if (out_bigrams) free_bigram_counts(out_bigrams);
  return 0;
}
