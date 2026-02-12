#pragma once
#include <stdbool.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"

/* String-based analysis pipeline.
 *
 * Responsibilities:
 * - Words: counted from `filtered` tokens (stopwords/digits/minlen already removed).
 * - Bigrams: derived from `raw` tokens using stopword-aware exclusion
 *   (original adjacency, no bridging over ignored tokens).
 *
 * Used directly when APP_PIPELINE_STRING is selected or when AUTO
 * chooses the string pipeline (typically for smaller inputs).
 */
int analyze_string_pipeline(
  const TokenList *filtered,
  const TokenList *raw,
  bool include_bigrams,
  const StopwordList *sw,
  WordCountList *out_words,
  BigramCountList *out_bigrams
);
