#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

/*
 * Token container produced by the tokenizer stage.
 * Represents the first transformation step in the analysis pipeline.
 */
typedef struct {
    char **items;     // dynamically allocated token array
    size_t count;     // number of tokens
} TokenList;

/*
 * Raw text metrics collected during tokenization.
 *
 * Includes stopwords (no filtering at this stage).
 * Used for meta information and performance instrumentation.
 */
typedef struct {
    size_t wordCount;      // total tokens (including stopwords)
    size_t wordCharCount;  // sum of token lengths (no separators)

    /* Performance instrumentation (Phase 3 evaluation). */
    size_t bytesScanned;          // total input bytes processed
    size_t splitAsciiCount;       // ASCII-based splits
    size_t splitUtf8DashCount;    // UTF-8 dash splits
    size_t tokenAllocs;           // number of token allocations
    size_t tokenBytesAllocated;   // total allocated token bytes
} TokenStats;

/*
 * Basic tokenizer (string-based pipeline entry).
 * - splits on whitespace
 * - ignores empty tokens
 * - normalizes ASCII A-Z to lowercase
 */
TokenList tokenize(const char *text);

/*
 * Tokenizer with instrumentation.
 * Used to populate meta.wordCount / meta.wordCharCount and
 * to collect performance data.
 */
TokenList tokenize_with_stats(const char *text, TokenStats *stats);

/*
 * Releases memory owned by a TokenList.
 */
void free_tokens(TokenList *list);

#endif
