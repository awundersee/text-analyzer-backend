#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * Single dictionary entry for token → ID mapping.
 * Used in the ID-based analysis pipeline.
 */
typedef struct {
  char *key;        // owned string (malloc)
  uint32_t id;      // stable ID (>= 1)
  int used;         // open addressing marker
} DictEntry;

/*
 * Bidirectional dictionary:
 *   word → id   (hash table)
 *   id   → word (dense array, index = id - 1)
 *
 * Central component of the ID-based pipeline.
 */
typedef struct {
  DictEntry *entries;
  size_t cap;       // power-of-two capacity (hash table)
  size_t size;      // number of active entries

  char **id_to_word;  // index = id - 1
  size_t id_cap;
  size_t id_size;     // equals number of assigned IDs
} Dict;

/* Initialize dictionary (hash-based, power-of-two capacity). */
int dict_init(Dict *d, size_t initial_cap);

/* Release all allocated dictionary memory. */
void dict_free(Dict *d);

/*
 * Get existing ID or assign new one.
 * IDs are stable and start at 1.
 */
uint32_t dict_get_or_add(Dict *d, const char *word);

/* Resolve ID back to word (owned by dictionary). */
const char *dict_word(const Dict *d, uint32_t id);

/* Return number of distinct words (assigned IDs). */
size_t dict_size(const Dict *d);
