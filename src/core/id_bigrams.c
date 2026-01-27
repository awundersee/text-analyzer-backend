#include "core/id_bigrams.h"
#include <stdlib.h>
#include <string.h>

static size_t next_pow2(size_t x) {
  size_t p = 1;
  while (p < x) p <<= 1;
  return p;
}

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
  if (b->size * 10 >= b->cap * 7) {
    if (!idbigrams_grow(b)) return 0;
  }

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
