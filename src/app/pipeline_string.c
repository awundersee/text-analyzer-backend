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

  /* Baseline string-based pipeline:
   * - words counted via linear string comparison
   * - suitable for small inputs (AUTO may switch to ID for larger ones)
   */
  *out_words = count_words(filtered);

  /* Bigrams are built from raw tokens with stopword-aware exclusion.
   * Adjacency is preserved; ignored tokens break pairs (no bridging).
   */
  if (include_bigrams && out_bigrams) {
    if (!sw) return 0;
    *out_bigrams = count_bigrams_excluding_stopwords(raw, sw);
  } else if (out_bigrams) {
    *out_bigrams = (BigramCountList){0};
  }

  return 1;
}
