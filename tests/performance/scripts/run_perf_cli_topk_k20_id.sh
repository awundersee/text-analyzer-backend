#!/usr/bin/env bash
set -euo pipefail

# -------------------------------------------------------------------
# run_perf_cli_topk_k20_id.sh
#
# Single-source, comparable measurements for CLI + topk:
# - pipeline: id (fixed)
# - K: 20 (default, override via K=...)
# - reports ONLY:
#     runtime_ms_analyze  (from CLI stdout)
#     topk_words_ms       (sum of PERF_TOPK words total_ns across the whole run)
#     topk_bigrams_ms     (sum of PERF_TOPK bigrams total_ns across the whole run)
#
# Notes:
# - PERF_TOPK lines are emitted by topk.c to stderr when PERF_TOPK=1.
# - runtime_ms_analyze is printed by CLI main.c to stdout.
#
# Override via env vars:
#   BIN, DATA_DIR, OUT_DIR, K, RUNS, TIMEOUT_SEC
# -------------------------------------------------------------------

PIPELINE="id"
K="${K:-20}"
RUNS="${RUNS:-5}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"

BIN="${BIN:-build/analyze_cli}"

# DATA_DIR autodetect if not provided
if [ -z "${DATA_DIR:-}" ]; then
  if [ -d "tests/performance/data/topk" ]; then
    DATA_DIR="tests/performance/data/topk"
  elif [ -d "performance/data/topk" ]; then
    DATA_DIR="performance/data/topk"
  else
    DATA_DIR="data/topk"
  fi
fi

OUT_DIR="${OUT_DIR:-tests/performance/results}"
ts="$(date +%Y%m%d-%H%M%S)"
CSV="$OUT_DIR/perf_cli_topk_id_k${K}_${ts}.csv"

have() { command -v "$1" >/dev/null 2>&1; }

bytes() { stat -c%s "$1" 2>/dev/null || stat -f%z "$1" 2>/dev/null || echo 0; }

run_with_timeout() {
  local seconds="$1"; shift
  if have timeout; then timeout "${seconds}s" "$@"; return $?
  elif have gtimeout; then gtimeout "${seconds}s" "$@"; return $?
  else "$@"; return $?
  fi
}

ns_to_ms() { awk -v x="$1" 'BEGIN{ printf "%.3f", x/1000000 }'; }

mkdir -p "$OUT_DIR"

if [ ! -x "$BIN" ]; then
  echo "[ERROR] BIN '$BIN' not found or not executable. Set BIN=... (e.g. BIN=build/analyze_cli)" >&2
  exit 2
fi
if [ ! -d "$DATA_DIR" ]; then
  echo "[ERROR] DATA_DIR '$DATA_DIR' not found. Set DATA_DIR=... (directory with *.json)" >&2
  exit 3
fi

echo "timestamp,file,bytes,pipeline,k,run_idx,runtime_ms_analyze,topk_words_ms,topk_bigrams_ms" > "$CSV"

export PERF_TOPK=1

# Warmup: first file (optional)
warm="$(find "$DATA_DIR" -type f -name "*.json" | sort | head -n 1 || true)"
if [ -n "$warm" ]; then
  echo "[INFO] Warmup: $(basename "$warm")"
  run_with_timeout "$TIMEOUT_SEC" "$BIN" "$warm" --pipeline "$PIPELINE" --topk "$K" >/dev/null 2>&1 || true
fi

echo "[INFO] BIN=$BIN"
echo "[INFO] DATA_DIR=$DATA_DIR"
echo "[INFO] PIPELINE=$PIPELINE"
echo "[INFO] K=$K"
echo "[INFO] RUNS=$RUNS"
echo "[INFO] Writing CSV to $CSV"
echo

# Iterate files
while IFS= read -r f; do
  [ -z "$f" ] && continue
  base="$(basename "$f")"
  sz="$(bytes "$f")"

  for i in $(seq 1 "$RUNS"); do
    # capture stdout (for runtime_ms_analyze) and stderr (PERF_TOPK) together
    out="$(
      run_with_timeout "$TIMEOUT_SEC" "$BIN" "$f" --pipeline "$PIPELINE" --topk "$K" 2>&1
    )" || {
      echo "[FAIL] $base run=$i" >&2
      continue
    }

    # runtime_ms_analyze from CLI stdout
    analyze_ms="$(printf "%s\n" "$out" | tr -d '\r' | awk -F= '/^runtime_ms_analyze=/{print $2; exit}')"
    if [ -z "$analyze_ms" ]; then
      echo "[WARN] $base run=$i: missing runtime_ms_analyze" >&2
      analyze_ms="NA"
    fi

    # Sum PERF_TOPK total_ns by kind across whole run
    # Expected lines: PERF_TOPK words total_ns=... copy_ns=... sort_ns=... out_ns=... n=... k=...
    words_ns="$(printf "%s\n" "$out" | tr -d '\r' | awk '
      $1=="PERF_TOPK" && $2=="words" {
        for (j=1;j<=NF;j++) if ($j ~ /^total_ns=/) { sub(/^total_ns=/,"",$j); sum += $j }
      }
      END { printf "%.0f", (sum+0) }')"
    bigrams_ns="$(printf "%s\n" "$out" | tr -d '\r' | awk '
      $1=="PERF_TOPK" && $2=="bigrams" {
        for (j=1;j<=NF;j++) if ($j ~ /^total_ns=/) { sub(/^total_ns=/,"",$j); sum += $j }
      }
      END { printf "%.0f", (sum+0) }')"

    words_ms="$(ns_to_ms "${words_ns:-0}")"
    bigrams_ms="$(ns_to_ms "${bigrams_ns:-0}")"

    utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n" \
      "$utc" "$base" "$sz" "$PIPELINE" "$K" "$i" \
      "$analyze_ms" "$words_ms" "$bigrams_ms" >> "$CSV"
  done
done < <(find "$DATA_DIR" -type f -name "*.json" | sort)

echo
echo "[DONE] CSV: $CSV"
