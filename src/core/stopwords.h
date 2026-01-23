#ifndef STOPWORDS_H
#define STOPWORDS_H

#include "core/tokenizer.h"

// [NO EXTERN]
// Removes tokens that are present in the stopword list file.
// The stopword file is expected to contain one word per line (lowercase recommended).
// This function modifies the TokenList in-place (frees removed tokens).
int filter_stopwords(TokenList *tokens, const char *stopwords_file_path);

#endif
