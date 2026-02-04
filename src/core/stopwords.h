#ifndef STOPWORDS_H
#define STOPWORDS_H

#include <stddef.h>
#include "core/tokenizer.h"

// Einfache Stopword-Liste (linear search; reicht erstmal, später Hashset möglich)
typedef struct {
    char **items;
    size_t count;
} StopwordList;

// Lädt Stopwords aus Datei (1 Wort pro Zeile). Lowercasing wird intern gemacht.
int stopwords_load(StopwordList *out, const char *stopwords_file_path);

// Gibt den Speicher der StopwordList frei.
void stopwords_free(StopwordList *sw);

// True/False: Token ist Stopword?
int stopwords_contains(const StopwordList *sw, const char *token);

// [NO EXTERN]
// Removes tokens that are present in the stopword list file.
// The stopword file is expected to contain one word per line (lowercase recommended).
// This function modifies the TokenList in-place (frees removed tokens).
int filter_stopwords(TokenList *tokens, const char *stopwords_file_path);

// NEU: erzeugt eine *neue* TokenList (bereinigt), Input bleibt unverändert.
// Filtert Stopwords + zu kurze Tokens + digits-only, genau wie filter_stopwords().
TokenList filter_stopwords_copy(const TokenList *in, const char *stopwords_file_path);

#endif
