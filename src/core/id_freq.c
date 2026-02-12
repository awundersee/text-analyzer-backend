#include "core/id_freq.h"
#include <stdlib.h>
#include <string.h>

/* Initialize dense ID-indexed frequency table.
 * Used in the memory-optimized (ID-based) pipeline.
 */
int idfreq_init(IdFreq *f, size_t initial_ids) {
  if (!f) return 0;
  memset(f, 0, sizeof(*f));
  f->cap = initial_ids < 16 ? 16 : initial_ids;
  f->counts = (uint32_t*)calloc(f->cap, sizeof(uint32_t));
  return f->counts != NULL;
}

/* Release frequency table memory. */
void idfreq_free(IdFreq *f) {
  if (!f) return;
  free(f->counts);
  f->counts = NULL;
  f->cap = 0;
}

/* Ensure capacity for given ID.
 * Grows exponentially to keep amortized O(1) insert cost.
 */
int idfreq_ensure(IdFreq *f, uint32_t id) {
  if (!f || id == 0) return 0;
  size_t need = (size_t)id;
  if (need <= f->cap) return 1;

  size_t new_cap = f->cap;
  while (new_cap < need) new_cap *= 2;

  uint32_t *nw = (uint32_t*)realloc(f->counts, new_cap * sizeof(uint32_t));
  if (!nw) return 0;

  /* Zero newly allocated region to maintain correct counts. */
  memset(nw + f->cap, 0, (new_cap - f->cap) * sizeof(uint32_t));
  f->counts = nw;
  f->cap = new_cap;
  return 1;
}

/* Increment counter for ID (id >= 1). */
int idfreq_inc(IdFreq *f, uint32_t id) {
  if (!idfreq_ensure(f, id)) return 0;
  f->counts[id - 1]++;
  return 1;
}

/* Read frequency for ID (returns 0 if not present). */
uint32_t idfreq_get(const IdFreq *f, uint32_t id) {
  if (!f || id == 0) return 0;
  size_t idx = (size_t)id - 1;
  if (idx >= f->cap) return 0;
  return f->counts[idx];
}

#include "core/freq.h"
#include "core/dict.h"
#include "core/tokenizer.h"

/* Local helper to duplicate dictionary words
 * when converting ID-based counts back to string-based results.
 */
static char *dup_cstr(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char*)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

/* Append word/count pair to WordCountList.
 * Final materialization step of ID-based pipeline.
 */
static int append_word(WordCountList *list, const char *word, uint32_t count) {
  size_t n = list->count + 1;
  WordCount *nw = (WordCount*)realloc(list->items, n * sizeof(WordCount));
  if (!nw) return 0;
  list->items = nw;

  list->items[list->count].word = dup_cstr(word);
  if (!list->items[list->count].word) return 0;

  list->items[list->count].count = (size_t)count;
  list->count = n;
  return 1;
}

/*
 * ID-based word counting.
 *
 * Pipeline:
 *   filtered tokens → dict (token→id) → dense IdFreq table
 *   → materialize WordCountList (string-based output)
 *
 * Reduces memory overhead compared to string-keyed hash maps.
 */
int id_count_words(const TokenList *filtered, Dict *dict, WordCountList *out_words) {
  if (!filtered || !dict || !out_words) return 0;
  *out_words = (WordCountList){0};

  IdFreq wf;
  if (!idfreq_init(&wf, 1024)) return 0;

  /* Counting stage: token → id → increment dense table. */
  for (size_t i = 0; i < filtered->count; i++) {
    const char *t = filtered->items[i];
    if (!t || !*t) continue;

    uint32_t id = dict_get_or_add(dict, t);
    if (id == 0) goto fail;

    if (!idfreq_inc(&wf, id)) goto fail;
  }

  /* Materialization stage: rebuild string-based result list. */
  for (uint32_t id = 1; id <= (uint32_t)dict_size(dict); id++) {
    uint32_t c = idfreq_get(&wf, id);
    if (!c) continue;

    const char *w = dict_word(dict, id);
    if (!append_word(out_words, w, c)) goto fail;
  }

  idfreq_free(&wf);
  return 1;

fail:
  idfreq_free(&wf);
  free_word_counts(out_words);
  return 0;
}
