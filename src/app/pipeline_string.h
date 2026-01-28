#pragma once
#include "core/tokenizer.h"
#include "core/freq.h"
#include "core/bigrams.h"

// tokens sollen bereits stopword-gefiltert sein
int analyze_string_pipeline(
  const TokenList *tokens,
  int include_bigrams,
  WordCountList *out_words,
  BigramCountList *out_bigrams
);
