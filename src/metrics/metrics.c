#include "metrics/metrics.h"

#if defined(__unix__) || defined(__APPLE__)
  #include <sys/resource.h>
  #include <sys/time.h>
#endif

uint64_t ta_peak_rss_kib(void) {
#if defined(__unix__) || defined(__APPLE__)
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0) {
    return 0;
  }

  // On Linux, ru_maxrss is in KiB.
  // On macOS, ru_maxrss is in bytes.
  #if defined(__APPLE__)
    if (ru.ru_maxrss <= 0) return 0;
    return (uint64_t)ru.ru_maxrss / 1024ULL;
  #else
    if (ru.ru_maxrss <= 0) return 0;
    return (uint64_t)ru.ru_maxrss; // already KiB on Linux
  #endif
#else
  return 0;
#endif
}
