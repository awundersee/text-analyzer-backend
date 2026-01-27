// src/cli/batch.h
#pragma once

// Return code:
// 0 = ok
// 1 = at least one file failed (but continued)
// 2 = fatal error (e.g. cannot open input dir)
int cli_run_batch(const char *in_dir, const char *out_dir, int continue_on_error);
