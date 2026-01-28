#include "app/pipeline_string.h"

int analyze_string_pipeline(
  const TokenList *tokens,
  int include_bigrams,
  WordCountList *out_words,
  BigramCountList *out_bigrams
) {
  if (!tokens || !out_words) return 0;

  *out_words = count_words(tokens);

  if (include_bigrams && out_bigrams) {
    *out_bigrams = count_bigrams(tokens);
  } else if (out_bigrams) {
    *out_bigrams = (BigramCountList){0};
  }

  return 1;
}
