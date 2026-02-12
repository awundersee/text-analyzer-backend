#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "core/tokenizer.h"
#include "core/stopwords.h"
#include "core/freq.h"
#include "core/bigrams.h"

/* ID-based analysis pipeline entrypoint.
 *
 * filtered: token stream used for word counts (stopwords/short/digits removed)
 * raw:      original token stream used for adjacency-based bigrams
 * sw:       loaded stopword list used by bigram exclusion (no bridging)
 *
 * include_bigrams controls whether out_bigrams is populated.
 */
int analyze_id_pipeline(
  const TokenList *filtered,
  const TokenList *raw,
  bool include_bigrams,
  const StopwordList *sw,
  WordCountList *out_words,
  BigramCountList *out_bigrams
);
