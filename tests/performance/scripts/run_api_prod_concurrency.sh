#!/usr/bin/env bash
set -uo pipefail

# ------------------------------------------------------------
# run_api_prod_concurrency.sh
#
# Concurrency tests explicitly against PROD target architecture:
#   https://textanalyse.wundersee.dev
#
# Measures (over RUNS repetitions per (file,pipeline,concurrency)):
#   - avgRuntimeAnalyzeMs (mean of per-run medians)
#   - minRuntimeAnalyzeMs (min of per-run medians)
#   - maxRuntimeAnalyzeMs (max of per-run medians)
#   - avgPeakKiB          (mean of per-run medians)
#   - minPeakKiB          (min of per-run medians)
#   - maxPeakKiB          (max of per-run medians)
#
# Note:
# - Each "run" launches <concurrency> parallel requests.
# - For each run we compute the MEDIAN across those parallel requests.
# - Then we aggregate across RUNS using avg/min/max of these run-medians.
# ------------------------------------------------------------

# Fixed target base (prod)
TARGET_BASE="https://textanalyse.wundersee.dev"
API_URL="${API_URL:-$TARGET_BASE/analyze}"
HEALTH_URL="${HEALTH_URL:-$TARGET_BASE/health}"

DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"

# Only ID pipeline requested
PIPELINE="id"

# Concurrency levels
CONCURRENCY="${CONCURRENCY:-1 4 8 16}"

# Number of repetitions per (file,concurrency)
RUNS="${RUNS:-5}"

# Warmup requests per pipeline (kept simple)
WARMUP="${WARMUP:-1}"

# Include large test set (optional)
INCLUDE_LARGE="${INCLUDE_LARGE:-0}"

# Remote curl behavior (avoid hanging forever)
CONNECT_TIMEOUT_SEC="${CONNECT_TIMEOUT_SEC:-5}"
MAX_TIME_SEC="${MAX_TIME_SEC:-180}"

PY="$(command -v python3 || command -v python)"
mkdir -p "$OUT_DIR"

ts="$(date +%Y%m%d-%H%M%S)"
OUT="$OUT_DIR/perf_api_prod_concurrency_${ts}.csv"
echo "target,concurrency,file,ok,runs,totalOk,avgRuntimeAnalyzeMs,minRuntimeAnalyzeMs,maxRuntimeAnalyzeMs,avgPeakKiB,minPeakKiB,maxPeakKiB,comment" > "$OUT"

median_of_sorted() {
  awk '{a[NR]=$1} END{ if(NR==0) exit 1; if(NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'
}

mean_of_values() {
  awk '{s+=$1; n+=1} END{ if(n==0) exit 1; print s/n }'
}

min_of_values() {
  awk 'NR==1{m=$1} $1<m{m=$1} END{ if(NR==0) exit 1; print m }'
}

max_of_values() {
  awk 'NR==1{m=$1} $1>m{m=$1} END{ if(NR==0) exit 1; print m }'
}

fmt3() { awk -v x="$1" 'BEGIN{ if(x=="") print "NA"; else printf "%.3f", x }'; }
fmt0() { awk -v x="$1" 'BEGIN{ if(x=="") print "NA"; else printf "%.0f", x }'; }
csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

json_with_pipeline() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    o=json.load(f)
o.setdefault("options", {})["pipeline"] = sys.argv[2]
print(json.dumps(o))
PY
}

json_get() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
try:
    with open(sys.argv[1]) as f:
        o=json.load(f)
    v=o.get("meta",{}).get(sys.argv[2],"")
    print(v)
except Exception:
    print("")
PY
}

collect_files() {
  local dirs=("$DATA_DIR/small" "$DATA_DIR/medium")
  if [ "${INCLUDE_LARGE:-0}" = "1" ] && [ -d "$DATA_DIR/large" ]; then
    dirs+=("$DATA_DIR/large")
  fi
  find "${dirs[@]}" -type f -name "*.json" 2>/dev/null | sort
}

FILES="$(collect_files)"

# Guard: no files
if [ -z "${FILES//[[:space:]]/}" ]; then
  echo "ERROR: No input files found. Checked:" >&2
  echo "  - $DATA_DIR/small" >&2
  echo "  - $DATA_DIR/medium" >&2
  if [ "${INCLUDE_LARGE:-0}" = "1" ]; then
    echo "  - $DATA_DIR/large" >&2
  fi
  echo "Hint: set DATA_DIR to your test data root (containing small/medium[/large])." >&2
  exit 2
fi

# ---------------- Health check ----------------
if ! curl -fsS \
  --connect-timeout "$CONNECT_TIMEOUT_SEC" \
  --max-time "$MAX_TIME_SEC" \
  "$HEALTH_URL" >/dev/null; then
  echo "ERROR: PROD API not reachable at $HEALTH_URL" >&2
  exit 1
fi

# ---------------- Warmup ----------------
first="$(echo "$FILES" | head -n1)"
if [ -n "$first" ]; then
  req="$(mktemp)"
  json_with_pipeline "$first" "$PIPELINE" > "$req"
  for _ in $(seq 1 "$WARMUP"); do
    curl -sS \
      --connect-timeout "$CONNECT_TIMEOUT_SEC" \
      --max-time "$MAX_TIME_SEC" \
      -H "Content-Type: application/json" \
      --data-binary @"$req" \
      "$API_URL" >/dev/null || true
  done
  rm -f "$req"
fi

# ---------------- Main ----------------
while IFS= read -r f; do
  [ -z "$f" ] && continue

  for c in $CONCURRENCY; do
    echo "==> PROD concurrency=$c [$PIPELINE] $f (RUNS=$RUNS)" >&2

    req="$(mktemp)"
    json_with_pipeline "$f" "$PIPELINE" > "$req"

    run_medians_ms=()
    run_medians_pk=()
    totalOk=0
    comment=""

    # Repeat RUNS times
    for r in $(seq 1 "$RUNS"); do
      work="$(mktemp -d)"

      # Fire c parallel requests
      for i in $(seq 1 "$c"); do
        (
          curl -sS \
            --connect-timeout "$CONNECT_TIMEOUT_SEC" \
            --max-time "$MAX_TIME_SEC" \
            -H "Content-Type: application/json" \
            --data-binary @"$req" \
            "$API_URL" \
            -o "$work/resp_$i.json" \
            -w "%{http_code}" > "$work/code_$i.txt" \
          || echo "000" > "$work/code_$i.txt"
        ) &
      done
      wait

      times=(); peaks=(); ok=0; run_comment=""
      for i in $(seq 1 "$c"); do
        code="$(cat "$work/code_$i.txt" 2>/dev/null || echo 000)"
        resp="$work/resp_$i.json"
        if [ "$code" = "200" ]; then
          ms="$(json_get "$resp" runtimeMsAnalyze)"
          pk="$(json_get "$resp" peakRssKiB)"
          if [ -n "$ms" ] && [ -n "$pk" ]; then
            times+=("$ms"); peaks+=("$pk"); ok=$((ok+1))
          else
            run_comment="missing meta fields"
          fi
        else
          run_comment="http $code"
        fi
      done

      if [ "$ok" -gt 0 ]; then
        totalOk=$((totalOk + ok))

        printf "%s\n" "${times[@]}" | sort -n > /tmp/times.$$
        printf "%s\n" "${peaks[@]}" | sort -n > /tmp/peaks.$$

        med_ms="$(median_of_sorted </tmp/times.$$ || true)"
        med_pk="$(median_of_sorted </tmp/peaks.$$ || true)"

        if [ -n "$med_ms" ] && [ -n "$med_pk" ]; then
          run_medians_ms+=("$med_ms")
          run_medians_pk+=("$med_pk")
        else
          comment="${comment:-}run$r: median calc failed; "
        fi
      else
        comment="${comment:-}run$r: ${run_comment:-no ok}; "
      fi

      rm -rf "$work"
    done

    # Aggregate across RUNS (using run-medians)
    if [ "${#run_medians_ms[@]}" -gt 0 ] && [ "${#run_medians_pk[@]}" -gt 0 ]; then
      printf "%s\n" "${run_medians_ms[@]}" > /tmp/runmed_ms.$$
      printf "%s\n" "${run_medians_pk[@]}" > /tmp/runmed_pk.$$

      avg_ms="$(mean_of_values </tmp/runmed_ms.$$ || true)"
      min_ms="$(min_of_values  </tmp/runmed_ms.$$ || true)"
      max_ms="$(max_of_values  </tmp/runmed_ms.$$ || true)"

      avg_pk="$(mean_of_values </tmp/runmed_pk.$$ || true)"
      min_pk="$(min_of_values  </tmp/runmed_pk.$$ || true)"
      max_pk="$(max_of_values  </tmp/runmed_pk.$$ || true)"

      rm -f /tmp/runmed_ms.$$ /tmp/runmed_pk.$$ /tmp/times.$$ /tmp/peaks.$$

      echo "\"$TARGET_BASE\",\"$c\",\"$f\",1,$RUNS,$totalOk,$(fmt3 "$avg_ms"),$(fmt3 "$min_ms"),$(fmt3 "$max_ms"),$(fmt0 "$avg_pk"),$(fmt0 "$min_pk"),$(fmt0 "$max_pk"),\"\"" >> "$OUT"
    else
      rm -f /tmp/times.$$ /tmp/peaks.$$ /tmp/runmed_ms.$$ /tmp/runmed_pk.$$ 2>/dev/null || true
      esc="$(csv_escape "${comment:-no successful runs}")"
      echo "\"$TARGET_BASE\",\"$c\",\"$f\",0,$RUNS,$totalOk,NA,NA,NA,NA,NA,NA,\"$esc\"" >> "$OUT"
    fi

    rm -f "$req"
  done
done <<< "$FILES"

echo "Wrote: $OUT"
