#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

typedef struct {
    char **items;     // array of C strings
    size_t count;     // number of tokens
} TokenList;

// NEU: reine Textumfangs-Kennzahlen (inkl. Stoppwörtern!)
typedef struct {
    size_t wordCount;      // Anzahl Tokens (Wörter) inkl. Stoppwörter
    size_t wordCharCount;  // Summe der Token-Längen (ohne Leerzeichen/Trenner)

    // neu für Performanceanalyse
    size_t bytesScanned;
    size_t splitAsciiCount;
    size_t splitUtf8DashCount;
    size_t tokenAllocs;
    size_t tokenBytesAllocated;    
} TokenStats;

// [NO EXTERN] Tokenize ASCII-ish text into lowercase words.
// Rules (initial, for G1/G2 later):
// - split on whitespace
// - ignore empty tokens
// - lowercase A-Z
TokenList tokenize(const char *text);

// NEU: Tokenize + Stats (für meta/pageResults.wordCount/wordCharCount)
TokenList tokenize_with_stats(const char *text, TokenStats *stats);

// Free memory allocated by tokenize()
void free_tokens(TokenList *list);

#endif
