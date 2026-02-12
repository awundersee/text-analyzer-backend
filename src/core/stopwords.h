#ifndef STOPWORDS_H
#define STOPWORDS_H

#include <stddef.h>
#include "core/tokenizer.h"

/*
 * Stopword container used during the filtering stage.
 * Current implementation uses linear search (sufficient for small lists).
 * Can be replaced by a hash-based lookup without changing the interface.
 */
typedef struct {
    char **items;
    size_t count;
} StopwordList;

/*
 * Loads stopwords from file (one word per line).
 * Words are normalized to lowercase internally.
 */
int stopwords_load(StopwordList *out, const char *stopwords_file_path);

/*
 * Releases memory owned by the StopwordList.
 */
void stopwords_free(StopwordList *sw);

/*
 * Returns non-zero if token is contained in the stopword list.
 * Used during the filtering stage before frequency counting.
 */
int stopwords_contains(const StopwordList *sw, const char *token);

/*
 * In-place filtering stage.
 * Removes stopwords (and invalid tokens) from the TokenList.
 * Frees removed token memory.
 */
int filter_stopwords(TokenList *tokens, const char *stopwords_file_path);

/*
 * Non-destructive filtering variant.
 * Produces a new TokenList with stopwords and invalid tokens removed.
 * Required for pipelines where original tokens must remain intact.
 */
TokenList filter_stopwords_copy(const TokenList *in, const char *stopwords_file_path);

#endif
