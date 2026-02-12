#pragma once

/* Batch runner for CLI regression/performance runs.
 *
 * Processes all input JSON files in a directory and writes outputs to out_dir.
 * continue_on_error controls whether a single bad file aborts the run.
 *
 * Return codes:
 * 0 = all files succeeded
 * 1 = at least one file failed (continued)
 * 2 = fatal setup error (e.g. cannot open input dir)
 */
int cli_run_batch(const char *in_dir, const char *out_dir, int continue_on_error);
