#pragma once
#include <stdbool.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"

// Analysiert tokenisierten Text.
// - filtered: stopword-/digits-/minlen-gefilterte Tokenliste (für Words)
// - raw: originale Tokenfolge (für natürliche Bigrams)
// - sw: geladene StopwordList (für bigram-excluding)
int analyze_string_pipeline(
  const TokenList *filtered,
  const TokenList *raw,
  bool include_bigrams,
  const StopwordList *sw,
  WordCountList *out_words,
  BigramCountList *out_bigrams
);
