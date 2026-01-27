#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t *counts;  // index id-1
  size_t cap;
} IdFreq;

int idfreq_init(IdFreq *f, size_t initial_ids);
void idfreq_free(IdFreq *f);
int idfreq_ensure(IdFreq *f, uint32_t id);
int idfreq_inc(IdFreq *f, uint32_t id);
uint32_t idfreq_get(const IdFreq *f, uint32_t id);
