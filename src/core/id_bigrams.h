#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t key;   // (id1<<32)|id2
  uint32_t count;
  int used;
} BigEntry;

typedef struct {
  BigEntry *entries;
  size_t cap;   // power of two
  size_t size;
} IdBigrams;

int idbigrams_init(IdBigrams *b, size_t initial_cap);
void idbigrams_free(IdBigrams *b);
int idbigrams_inc(IdBigrams *b, uint32_t id1, uint32_t id2);
