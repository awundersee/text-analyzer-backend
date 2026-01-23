#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

typedef struct {
    char **items;     // array of C strings
    size_t count;     // number of tokens
} TokenList;

// [NO EXTERN] Tokenize ASCII-ish text into lowercase words.
// Rules (initial, for G1/G2 later):
// - split on whitespace
// - ignore empty tokens
// - lowercase A-Z
TokenList tokenize(const char *text);

// Free memory allocated by tokenize()
void free_tokens(TokenList *list);

#endif
