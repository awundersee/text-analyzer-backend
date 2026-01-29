#!/usr/bin/env bash
set -euo pipefail

# -------------------------------------------------------------------
# run_perf_topk_measure.sh (bash 3.2 compatible; no mapfile)
#
# Intended to be executed by CMake from the repo root (like your other
# perf scripts), so defaults are relative paths:
#   BUILD_DIR=build
#   BIN=build/analyze_cli
#
# Data dir auto-detect (first existing wins):
#   performance/data
#   tests/performance/data
#   data
#
# Outputs:
#   performance/results/perf_topk.csv   (default)
#
# Override via env vars:
#   BUILD_DIR, BIN, DATA_DIR, OUT_DIR, PIPELINE, INCLUDE_LARGE, K, TIMEOUT_SEC
# -------------------------------------------------------------------

PIPELINE="${PIPELINE:-id}"
INCLUDE_LARGE="${INCLUDE_LARGE:-0}"   # set to 1 to include $DATA_DIR/large
K="${K:-}"                            # if your CLI supports --topk <K>, set K to pass it
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"

BUILD_DIR="${BUILD_DIR:-build}"
BIN="${BIN:-${BUILD_DIR}/analyze_cli}"

# DATA_DIR autodetect if not provided
if [ -z "${DATA_DIR:-}" ]; then
  if [ -d "performance/data" ]; then
    DATA_DIR="performance/data"
  elif [ -d "tests/performance/data" ]; then
    DATA_DIR="tests/performance/data"
  else
    DATA_DIR="data"
  fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PERF_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"   # tests/performance
OUT_DIR="${OUT_DIR:-$PERF_DIR/results}"

ts="$(date +%Y%m%d-%H%M%S)"
CSV="$OUT_DIR/perf_topk_${ts}.csv"

# --- helpers --------------------------------------------------------

have() { command -v "$1" >/dev/null 2>&1; }

bytes() { stat -c%s "$1" 2>/dev/null || stat -f%z "$1" 2>/dev/null || echo 0; }

run_with_timeout() {
  local seconds="$1"; shift
  if have timeout; then timeout "${seconds}s" "$@"; return $?
  elif have gtimeout; then gtimeout "${seconds}s" "$@"; return $?
  else "$@"; return $?
  fi
}

to_csv_row() {
  local file="$1"
  local size_bytes="$2"
  local line="$3"

  # shellcheck disable=SC2206
  local parts=($line)
  local kind="${parts[1]}"  # words|bigrams

  local total_ns="" copy_ns="" sort_ns="" out_ns="" n="" k=""
  for p in "${parts[@]}"; do
    case "$p" in
      total_ns=*) total_ns="${p#total_ns=}" ;;
      copy_ns=*)  copy_ns="${p#copy_ns=}" ;;
      sort_ns=*)  sort_ns="${p#sort_ns=}" ;;
      out_ns=*)   out_ns="${p#out_ns=}" ;;
      n=*)        n="${p#n=}" ;;
      k=*)        k="${p#k=}" ;;
    esac
  done

  local total_ms copy_ms sort_ms out_ms
  total_ms="$(awk -v x="$total_ns" 'BEGIN{ printf "%.3f", x/1000000 }')"
  copy_ms="$(awk -v x="$copy_ns"  'BEGIN{ printf "%.3f", x/1000000 }')"
  sort_ms="$(awk -v x="$sort_ns"  'BEGIN{ printf "%.3f", x/1000000 }')"
  out_ms="$(awk -v x="$out_ns"   'BEGIN{ printf "%.3f", x/1000000 }')"

  local ts
  ts="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$ts" "$file" "$size_bytes" "$PIPELINE" "$kind" "$n" "$k" \
    "$total_ns" "$copy_ns" "$sort_ns" "$out_ns" \
    "$total_ms" "$copy_ms" "$sort_ms" "$out_ms"
}

collect_files() {
  local dirs=()

  # [ -d "$DATA_DIR/small" ]  && dirs+=("$DATA_DIR/small")
  # [ -d "$DATA_DIR/medium" ] && dirs+=("$DATA_DIR/medium")
  [ -d "$DATA_DIR/topk" ] && dirs+=("$DATA_DIR/topk")
  if [ "$INCLUDE_LARGE" = "1" ] && [ -d "$DATA_DIR/large" ]; then
    dirs+=("$DATA_DIR/large")
  fi

  if [ "${#dirs[@]}" -eq 0 ]; then
    return 0
  fi

  find "${dirs[@]}" -type f -name "*.json" 2>/dev/null | sort
}

# --- main -----------------------------------------------------------

mkdir -p "$OUT_DIR"
echo "timestamp,file,bytes,pipeline,kind,n,k,total_ns,copy_ns,sort_ns,out_ns,total_ms,copy_ms,sort_ms,out_ms" > "$CSV"

if [ ! -x "$BIN" ]; then
  echo "[ERROR] BIN '$BIN' not found or not executable. Set BIN=... (e.g. BIN=build/analyze_cli)" >&2
  exit 2
fi

files=()
while IFS= read -r f; do
  [ -n "$f" ] && files+=("$f")
done < <(collect_files || true)

if [ "${#files[@]}" -eq 0 ]; then
  echo "[ERROR] No JSON files found under $DATA_DIR/{small,medium$( [ "$INCLUDE_LARGE" = "1" ] && echo ",large" )}." >&2
  echo "        Set DATA_DIR=... or INCLUDE_LARGE=1 and ensure directories exist." >&2
  exit 3
fi

echo "[INFO] BIN=$BIN"
echo "[INFO] DATA_DIR=$DATA_DIR (include_large=$INCLUDE_LARGE)"
echo "[INFO] PIPELINE=$PIPELINE"
echo "[INFO] TIMEOUT_SEC=$TIMEOUT_SEC"
echo "[INFO] Writing CSV to $CSV"
echo

echo "[INFO] K=${K:-<unset>}"

export PERF_TOPK=1

# Warmup
warm="${files[0]}"
echo "[INFO] Warmup: $(basename "$warm")"
args=("$BIN" "$warm" --pipeline "$PIPELINE")
if [ -n "$K" ]; then args+=("--topk" "$K"); fi
echo "[DEBUG] warmup args: ${args[*]}"
run_with_timeout "$TIMEOUT_SEC" "${args[@]}" >/dev/null 2>&1 || true

echo

ok=0
fail=0

for f in "${files[@]}"; do
  base="$(basename "$f")"
  sz="$(bytes "$f")"

  args=("$BIN" "$f" --pipeline "$PIPELINE")
  if [ -n "$K" ]; then args+=("--topk" "$K"); fi
  echo "[DEBUG] run args: ${args[*]}"

  if out="$(run_with_timeout "$TIMEOUT_SEC" "${args[@]}" 2>&1 1>/dev/null)"; then
    lines="$(printf "%s\n" "$out" | tr -d '\r' | grep 'PERF_TOPK' || true)"
    if [ -z "$lines" ]; then
      echo "[WARN] $base: no PERF_TOPK lines found (did you add logging in topk.c and set PERF_TOPK=1?)"
      echo "[DEBUG] stderr sample:"
      printf "%s\n" "$out" | head -n 20 | cat -v
      ok=$((ok+1))
      continue
    fi

    echo "[OK]   $base  ($sz bytes)"
    while IFS= read -r line; do
      echo "       $line"
      to_csv_row "$base" "$sz" "$line" >> "$CSV"
    done <<< "$lines"

    ok=$((ok+1))
  else
    rc=$?
    echo "[FAIL] $base  ($sz bytes) rc=$rc"
    fail=$((fail+1))
  fi
done

echo
echo "[DONE] ok=$ok fail=$fail"
echo "[DONE] CSV: $CSV"
