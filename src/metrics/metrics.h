#ifndef TEXT_ANALYZER_METRICS_H
#define TEXT_ANALYZER_METRICS_H

#include <stdint.h>

// Returns the maximum resident set size (peak RSS) of the current process in KiB.
// If unavailable, returns 0.
uint64_t ta_peak_rss_kib(void);

#endif // TEXT_ANALYZER_METRICS_H
