#include "core/dict.h"
#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a64(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char*)s; *p; ++p) {
    h ^= (uint64_t)(*p);
    h *= 1099511628211ULL;
  }
  return h;
}

static char *dup_cstr(const char *s) {
  size_t n = strlen(s);
  char *out = (char*)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static size_t next_pow2(size_t x) {
  size_t p = 1;
  while (p < x) p <<= 1;
  return p;
}

static int dict_grow(Dict *d);

int dict_init(Dict *d, size_t initial_cap) {
  if (!d) return 0;
  memset(d, 0, sizeof(*d));
  d->cap = next_pow2(initial_cap < 16 ? 16 : initial_cap);
  d->entries = (DictEntry*)calloc(d->cap, sizeof(DictEntry));
  if (!d->entries) return 0;

  d->id_cap = 16;
  d->id_to_word = (char**)calloc(d->id_cap, sizeof(char*));
  if (!d->id_to_word) { free(d->entries); memset(d,0,sizeof(*d)); return 0; }
  return 1;
}

void dict_free(Dict *d) {
  if (!d) return;
  if (d->entries) {
    for (size_t i = 0; i < d->cap; i++) {
      if (d->entries[i].used) free(d->entries[i].key);
    }
    free(d->entries);
  }
  if (d->id_to_word) {
    // words already freed via entries[i].key (same pointers), do not double free
    free(d->id_to_word);
  }
  memset(d, 0, sizeof(*d));
}

size_t dict_size(const Dict *d) { return d ? d->size : 0; }

const char *dict_word(const Dict *d, uint32_t id) {
  if (!d || id == 0) return NULL;
  size_t idx = (size_t)id - 1;
  if (idx >= d->id_size) return NULL;
  return d->id_to_word[idx];
}

static int ensure_id_cap(Dict *d, size_t need) {
  if (need <= d->id_cap) return 1;
  size_t new_cap = d->id_cap;
  while (new_cap < need) new_cap *= 2;
  char **nw = (char**)realloc(d->id_to_word, new_cap * sizeof(char*));
  if (!nw) return 0;
  // zero new region
  for (size_t i = d->id_cap; i < new_cap; i++) nw[i] = NULL;
  d->id_to_word = nw;
  d->id_cap = new_cap;
  return 1;
}

static int dict_insert(Dict *d, const char *word, uint32_t *out_id) {
  if (d->size * 10 >= d->cap * 7) { // load factor ~0.7
    if (!dict_grow(d)) return 0;
  }
  uint64_t h = fnv1a64(word);
  size_t mask = d->cap - 1;
  size_t pos = (size_t)h & mask;

  while (d->entries[pos].used) {
    if (strcmp(d->entries[pos].key, word) == 0) {
      *out_id = d->entries[pos].id;
      return 1;
    }
    pos = (pos + 1) & mask;
  }

  char *k = dup_cstr(word);
  if (!k) return 0;

  uint32_t id = (uint32_t)(d->id_size + 1);
  if (!ensure_id_cap(d, d->id_size + 1)) { free(k); return 0; }

  d->entries[pos].key = k;
  d->entries[pos].id = id;
  d->entries[pos].used = 1;
  d->size++;

  d->id_to_word[d->id_size] = k;
  d->id_size++;

  *out_id = id;
  return 1;
}

uint32_t dict_get_or_add(Dict *d, const char *word) {
  if (!d || !word || !*word) return 0;
  uint32_t id = 0;
  if (!dict_insert(d, word, &id)) return 0;
  return id;
}

static int dict_grow(Dict *d) {
  size_t old_cap = d->cap;
  DictEntry *old = d->entries;

  size_t new_cap = old_cap * 2;
  DictEntry *ne = (DictEntry*)calloc(new_cap, sizeof(DictEntry));
  if (!ne) return 0;

  d->entries = ne;
  d->cap = new_cap;
  d->size = 0;

  // reinsert (reuse existing keys; do NOT duplicate)
  for (size_t i = 0; i < old_cap; i++) {
    if (!old[i].used) continue;

    uint64_t h = fnv1a64(old[i].key);
    size_t mask = d->cap - 1;
    size_t pos = (size_t)h & mask;

    while (d->entries[pos].used) pos = (pos + 1) & mask;

    d->entries[pos] = old[i];
    d->entries[pos].used = 1;
    d->size++;
  }

  free(old);
  return 1;
}
