#include "core/id_freq.h"
#include <stdlib.h>
#include <string.h>

int idfreq_init(IdFreq *f, size_t initial_ids) {
  if (!f) return 0;
  memset(f, 0, sizeof(*f));
  f->cap = initial_ids < 16 ? 16 : initial_ids;
  f->counts = (uint32_t*)calloc(f->cap, sizeof(uint32_t));
  return f->counts != NULL;
}

void idfreq_free(IdFreq *f) {
  if (!f) return;
  free(f->counts);
  f->counts = NULL;
  f->cap = 0;
}

int idfreq_ensure(IdFreq *f, uint32_t id) {
  if (!f || id == 0) return 0;
  size_t need = (size_t)id;
  if (need <= f->cap) return 1;
  size_t new_cap = f->cap;
  while (new_cap < need) new_cap *= 2;

  uint32_t *nw = (uint32_t*)realloc(f->counts, new_cap * sizeof(uint32_t));
  if (!nw) return 0;
  // zero new region
  memset(nw + f->cap, 0, (new_cap - f->cap) * sizeof(uint32_t));
  f->counts = nw;
  f->cap = new_cap;
  return 1;
}

int idfreq_inc(IdFreq *f, uint32_t id) {
  if (!idfreq_ensure(f, id)) return 0;
  f->counts[id - 1]++;
  return 1;
}

uint32_t idfreq_get(const IdFreq *f, uint32_t id) {
  if (!f || id == 0) return 0;
  size_t idx = (size_t)id - 1;
  if (idx >= f->cap) return 0;
  return f->counts[idx];
}
