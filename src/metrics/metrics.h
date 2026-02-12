#ifndef TEXT_ANALYZER_METRICS_H
#define TEXT_ANALYZER_METRICS_H

#include <stdint.h>

/*
 * Returns the peak resident set size (RSS) of the current process in KiB.
 *
 * Used as a memory measurement point during analysis to evaluate
 * pipeline behavior (string-based vs. ID-based) and aggregation impact.
 *
 * Returns 0 if the metric is not available on the platform.
 */
uint64_t ta_peak_rss_kib(void);

#endif // TEXT_ANALYZER_METRICS_H
