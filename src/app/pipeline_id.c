#include "app/pipeline_id.h"

#include "core/dict.h"
#include "core/id_freq.h"
#include "core/id_bigrams.h"

int analyze_id_pipeline(
  const TokenList *filtered,
  const TokenList *raw,
  bool include_bigrams,
  const StopwordList *sw,
  WordCountList *out_words,
  BigramCountList *out_bigrams
) {
  if (!filtered || !out_words) return 0;

  *out_words = (WordCountList){0};
  if (out_bigrams) *out_bigrams = (BigramCountList){0};

  // Dict wird geteilt: Words (filtered) + Bigram-Export (raw)
  Dict dict;
  size_t hint = filtered->count + (raw ? raw->count : 0);
  if (!dict_init(&dict, hint * 2 + 16)) return 0;

  // Words aus filtered
  if (!id_count_words(filtered, &dict, out_words)) {
    dict_free(&dict);
    free_word_counts(out_words);
    return 0;
  }

  // Bigrams aus raw + sw (kein Bridging) – Logik steckt in id_bigrams.c
  if (include_bigrams && out_bigrams) {
    if (!raw || !sw) {
      dict_free(&dict);
      free_word_counts(out_words);
      return 0;
    }

    if (!id_count_bigrams_excluding_stopwords(raw, sw, &dict, out_bigrams)) {
      dict_free(&dict);
      free_word_counts(out_words);
      free_bigram_counts(out_bigrams);
      return 0;
    }
  }

  dict_free(&dict);
  return 1;
}
