#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/run_stress_common.sh"

CLI_TIMEOUT_SEC="${CLI_TIMEOUT_SEC:-600}"
BUILD_DIR="${BUILD_DIR:-build}"
BIN="${BIN:-${BUILD_DIR}/analyze_cli}"
DATA_DIR="${DATA_DIR:-tests/stress/data/multi-page}"
OUT_DIR="${OUT_DIR:-tests/stress/results}"
RUNS="${RUNS:-1}"

mkdir -p "$OUT_DIR"
ts="$(date +%Y%m%d-%H%M%S)"
OUT_FILE="${OUT_FILE:-$OUT_DIR/stress_cli_multi_page_${ts}.csv}"

echo "timestamp,file,bytes,pages_input,words_est,chars_est,status,exit_code,runtime_ms_analyze,runtime_ms_total,peak_kib_total,pages_received,pipeline_used,words_exact,chars_exact,word_chars_exact,counts_source,comment" > "$OUT_FILE"

FILES="$(find "$DATA_DIR" -type f -name "*.json" 2>/dev/null | sort || true)"
[ -z "$FILES" ] && { echo "ERROR: No input files found under: $DATA_DIR" >&2; exit 2; }

now_iso() { date -Iseconds; }

for f in $FILES; do
  bytes="$(wc -c < "$f" | tr -d ' ')"
  IFS=',' read -r pages_input words_est chars_est < <(json_input_estimates "$f")

  for _ in $(seq 1 "$RUNS"); do
    ts_run="$(now_iso)"
    out=""; rc=0; status="OK"; comment=""

    out="$(run_with_timeout "$CLI_TIMEOUT_SEC" "$BIN" "$f" --pipeline id 2>&1)" || rc=$?
    if [ "$rc" -eq 124 ] || printf "%s" "$out" | grep -q "<<timeout>>"; then status="TIMEOUT"
    elif [ "$rc" -ne 0 ]; then status="ERROR"; fi

    IFS=$'\t' read -r runtime_a runtime_t peak_total pages_recv pipe_used words_exact chars_exact wchars_exact counts_source < <(printf "%s" "$out" | extract_from_output)

    if [ "$status" = "OK" ]; then
      [ "${pipe_used:-NA}" = "NA" ] && pipe_used="id"
      [ "${pages_recv:-NA}" = "NA" ] && pages_recv="$pages_input"
      if [ "$counts_source" = "none" ]; then counts_source="estimated_fallback"; fi
    else
      words_exact="NA"; chars_exact="NA"; wchars_exact="NA"; pipe_used="NA"; pages_recv="NA"
      counts_source="estimated_only"
      one_line="${out//$'\n'/ }"; one_line="${one_line:0:240}"
      comment="cli_${status}: ${one_line}"
    fi

    esc_comment="$(csv_escape "$comment")"
    echo "\"$ts_run\",\"$f\",$bytes,$pages_input,$words_est,$chars_est,\"$status\",$rc,$runtime_a,$runtime_t,$peak_total,$pages_recv,\"$pipe_used\",$words_exact,$chars_exact,$wchars_exact,\"$counts_source\",\"$esc_comment\"" >> "$OUT_FILE"
  done
done

echo "==> Wrote $OUT_FILE" >&2
