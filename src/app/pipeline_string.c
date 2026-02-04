#include "app/pipeline_string.h"

int analyze_string_pipeline(
  const TokenList *filtered,
  const TokenList *raw,
  bool include_bigrams,
  const StopwordList *sw,
  WordCountList *out_words,
  BigramCountList *out_bigrams
) {
  if (!filtered || !raw || !out_words) return 0;

  *out_words = count_words(filtered);

  if (include_bigrams && out_bigrams) {
    if (!sw) return 0;
    *out_bigrams = count_bigrams_excluding_stopwords(raw, sw);
  } else if (out_bigrams) {
    *out_bigrams = (BigramCountList){0};
  }

  return 1;
}
