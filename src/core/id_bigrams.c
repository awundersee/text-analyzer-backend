#include "core/id_bigrams.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core/stopwords.h"
#include "core/bigrams.h"
#include "core/dict.h"

/* Utility: ensure power-of-two capacity for mask-based probing. */
static size_t next_pow2(size_t x) {
  size_t p = 1;
  while (p < x) p <<= 1;
  return p;
}

/* 64-bit mix for stable hashing of (id1,id2) packed keys. */
static uint64_t mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

static int idbigrams_grow(IdBigrams *b);

int idbigrams_init(IdBigrams *b, size_t initial_cap) {
  if (!b) return 0;
  memset(b, 0, sizeof(*b));

  /* ID-bigram table: open addressing, power-of-two capacity. */
  b->cap = next_pow2(initial_cap < 64 ? 64 : initial_cap);
  b->entries = (BigEntry*)calloc(b->cap, sizeof(BigEntry));
  return b->entries != NULL;
}

void idbigrams_free(IdBigrams *b) {
  if (!b) return;
  free(b->entries);
  memset(b, 0, sizeof(*b));
}

int idbigrams_inc(IdBigrams *b, uint32_t id1, uint32_t id2) {
  if (!b || id1 == 0 || id2 == 0) return 0;

  /* Grow at ~0.7 load factor to keep probing cheap. */
  if (b->size * 10 >= b->cap * 7) {
    if (!idbigrams_grow(b)) return 0;
  }

  /* Pack (id1,id2) into one key to avoid string concatenation. */
  uint64_t key = ((uint64_t)id1 << 32) | (uint64_t)id2;
  uint64_t h = mix64(key);
  size_t mask = b->cap - 1;
  size_t pos = (size_t)h & mask;

  while (b->entries[pos].used) {
    if (b->entries[pos].key == key) {
      b->entries[pos].count++;
      return 1;
    }
    pos = (pos + 1) & mask;
  }

  b->entries[pos].used = 1;
  b->entries[pos].key = key;
  b->entries[pos].count = 1;
  b->size++;
  return 1;
}

/* Rehash into a table with doubled capacity (measurement point for peak RSS). */
static int idbigrams_grow(IdBigrams *b) {
  size_t old_cap = b->cap;
  BigEntry *old = b->entries;

  size_t new_cap = old_cap * 2;
  BigEntry *ne = (BigEntry*)calloc(new_cap, sizeof(BigEntry));
  if (!ne) return 0;

  b->entries = ne;
  b->cap = new_cap;
  b->size = 0;

  for (size_t i = 0; i < old_cap; i++) {
    if (!old[i].used) continue;

    uint64_t key = old[i].key;
    uint64_t h = mix64(key);
    size_t mask = b->cap - 1;
    size_t pos = (size_t)h & mask;

    while (b->entries[pos].used) pos = (pos + 1) & mask;
    b->entries[pos] = old[i];
    b->entries[pos].used = 1;
    b->size++;
  }

  free(old);
  return 1;
}

/* Local helper: duplicate dict words when materializing output. */
static char *dup_cstr2(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char*)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

/* Materialize (id1,id2,count) into string-based BigramCountList. */
static int append_bigram(BigramCountList *list, const char *w1, const char *w2, uint32_t count) {
  size_t n = list->count + 1;
  BigramCount *nb = (BigramCount*)realloc(list->items, n * sizeof(BigramCount));
  if (!nb) return 0;
  list->items = nb;
  list->items[list->count].w1 = dup_cstr2(w1);
  list->items[list->count].w2 = dup_cstr2(w2);
  if (!list->items[list->count].w1 || !list->items[list->count].w2) return 0;
  list->items[list->count].count = (size_t)count;
  list->count = n;
  return 1;
}

static int is_all_digits_local(const char *s) {
  if (!s || !*s) return 0;
  for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
  return 1;
}

/* Same drop rules as word filtering: short tokens, digits-only, stopwords. */
static int ignore_tok(const char *tok, const StopwordList *sw) {
  if (!tok || !*tok) return 1;
  if (strlen(tok) < 2) return 1;
  if (is_all_digits_local(tok)) return 1;
  if (sw && stopwords_contains(sw, tok)) return 1;
  return 0;
}

int id_count_bigrams_excluding_stopwords(const TokenList *raw,
                                        const StopwordList *sw,
                                        Dict *dict,
                                        BigramCountList *out_bigrams) {
  if (!raw || !sw || !dict || !out_bigrams) return 0;
  *out_bigrams = (BigramCountList){0};

  /* ID-based bigram counting stage (memory-optimized pipeline). */
  IdBigrams bg;
  if (!idbigrams_init(&bg, raw->count * 2 + 64)) return 0;

  uint32_t prev = 0;
  for (size_t i = 0; i < raw->count; i++) {
    const char *t = raw->items[i];

    /* No bridging across dropped tokens (keeps bigrams local to valid runs). */
    if (ignore_tok(t, sw)) { prev = 0; continue; }

    uint32_t id = dict_get_or_add(dict, t);
    if (id == 0) goto fail;

    if (prev != 0) {
      if (!idbigrams_inc(&bg, prev, id)) goto fail;
    }
    prev = id;
  }

  /* Materialize hash table into output list (string-based API contract). */
  for (size_t i = 0; i < bg.cap; i++) {
    if (!bg.entries[i].used) continue;
    uint64_t key = bg.entries[i].key;
    uint32_t id1 = (uint32_t)(key >> 32);
    uint32_t id2 = (uint32_t)(key & 0xffffffffu);
    const char *w1 = dict_word(dict, id1);
    const char *w2 = dict_word(dict, id2);
    if (!w1 || !w2) continue;
    if (!append_bigram(out_bigrams, w1, w2, bg.entries[i].count)) goto fail;
  }

  idbigrams_free(&bg);
  return 1;

fail:
  idbigrams_free(&bg);
  free_bigram_counts(out_bigrams);
  return 0;
}
