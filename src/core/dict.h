#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char *key;        // owned (malloc)
  uint32_t id;      // >= 1
  int used;         // 0 = empty, 1 = used
} DictEntry;

typedef struct {
  DictEntry *entries;
  size_t cap;       // power of two
  size_t size;      // number of used entries

  // id -> word (index id-1)
  char **id_to_word;
  size_t id_cap;
  size_t id_size;   // equals dict size
} Dict;

int dict_init(Dict *d, size_t initial_cap);
void dict_free(Dict *d);

uint32_t dict_get_or_add(Dict *d, const char *word);  // returns id >= 1
const char *dict_word(const Dict *d, uint32_t id);    // returns ptr owned by dict
size_t dict_size(const Dict *d);
