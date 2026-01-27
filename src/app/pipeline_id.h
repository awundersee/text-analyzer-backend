#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"

// Analysiert bereits tokenisierten Text (Tokens sollen bereits stopword-gefiltert sein)
// und liefert WordCountList / BigramCountList (Strings), damit Aggregation/TopK unverändert bleibt.
int analyze_id_pipeline(
  const TokenList *tokens,
  int include_bigrams,
  WordCountList *out_words,
  BigramCountList *out_bigrams
);
