#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------
# API Baseline + Delta (peakRssKiB) runner
# - measures baseline peak (minimal request)
# - runs scenarios (small/medium[/large]) for pipelines string/id
# - writes: baselinePeakKiB, peakKiB, deltaKiB, runtimeMs
#
# Env:
#   API_URL=http://127.0.0.1:8080/analyze
#   DATA_DIR=tests/performance/data
#   OUT_DIR=tests/performance/results
#   RUNS=5
#   WARMUP=1
#   PIPELINES="string id"
#   INCLUDE_LARGE=0|1 (default 0)
#   BASELINE_FILE=tests/performance/data/small/p1.json
# ---------------------------------------

API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"
RUNS="${RUNS:-5}"
WARMUP="${WARMUP:-1}"
PIPELINES="${PIPELINES:-string id}"
INCLUDE_LARGE="${INCLUDE_LARGE:-0}"
BASELINE_FILE="${BASELINE_FILE:-$DATA_DIR/small/p1.json}"

PY=""
if command -v python3 >/dev/null 2>&1; then PY=python3
elif command -v python >/dev/null 2>&1; then PY=python
else echo "ERROR: need python3/python" >&2; exit 90; fi

command -v curl >/dev/null 2>&1 || { echo "ERROR: need curl" >&2; exit 91; }

mkdir -p "$OUT_DIR"
ts="$(date +%Y%m%d-%H%M%S)"
OUT="$OUT_DIR/perf_api_baseline_delta_${ts}.csv"

echo "pipeline,file,runs,baselinePeakKiB,medianPeakKiB,deltaPeakKiB,medianRuntimeMs,minRuntimeMs,maxRuntimeMs,comment" > "$OUT"

csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

median_of_sorted() {
  awk '{a[NR]=$1} END{ if(NR==0) exit 10; if(NR%2==1) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'
}

fmt3() { awk -v x="$1" 'BEGIN{ if(x==""||x=="NA") print "NA"; else printf "%.3f", x }'; }
fmt0() { awk -v x="$1" 'BEGIN{ if(x==""||x=="NA") print "NA"; else printf "%.0f", x }'; }

json_with_pipeline() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
path=sys.argv[1]; pip=sys.argv[2]
with open(path, "r", encoding="utf-8") as f: o=json.load(f)
o.setdefault("options", {})
o["options"]["pipeline"]=pip
print(json.dumps(o, ensure_ascii=False))
PY
}

json_get() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
p=sys.argv[1]; key=sys.argv[2]
try:
  with open(p,"rb") as f: o=json.load(f)
  if key=="runtime": v=o.get("meta",{}).get("runtimeMsTotal","")
  elif key=="peak": v=o.get("meta",{}).get("peakRssKiB","")
  elif key=="msg": v=o.get("message") or o.get("error") or ""
  else: v=""
  sys.stdout.write("" if v is None else str(v))
except Exception:
  sys.stdout.write("")
PY
}

collect_files() {
  local tmp="$(mktemp)"
  find "$DATA_DIR/small"  -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  find "$DATA_DIR/medium" -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  if [ "$INCLUDE_LARGE" = "1" ]; then
    find "$DATA_DIR/large"  -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  fi
  cat "$tmp"; rm -f "$tmp"
}

call_api_once() {
  local reqfile="$1"
  local pip="$2"
  local tmp_req tmp_resp tmp_hdr tmp_err http_code

  tmp_req="$(mktemp)"
  json_with_pipeline "$reqfile" "$pip" > "$tmp_req"

  tmp_resp="$(mktemp)"
  tmp_hdr="$(mktemp)"
  tmp_err="$(mktemp)"

  http_code="$(curl --http1.1 -sS -D "$tmp_hdr" -o "$tmp_resp" -w "%{http_code}" \
    -H "Content-Type: application/json" --data-binary @"$tmp_req" "$API_URL" 2>"$tmp_err" || true)"

  rm -f "$tmp_hdr" "$tmp_err" "$tmp_req"
  printf "%s %s\n" "$http_code" "$tmp_resp"
}

# ---- Baseline peak: take max of a few minimal calls (more stable)
baseline_tmp="$(mktemp)"
baseline_peak="NA"

if [ ! -f "$BASELINE_FILE" ]; then
  echo "ERROR: BASELINE_FILE not found: $BASELINE_FILE" >&2
  exit 92
fi

for i in $(seq 1 3); do
  read -r code resp <<<"$(call_api_once "$BASELINE_FILE" "auto")"
  if [ "$code" = "200" ]; then
    pk="$(json_get "$resp" peak)"
    if [ -n "$pk" ]; then echo "$pk" >> "$baseline_tmp"; fi
  fi
  rm -f "$resp"
done

if [ -s "$baseline_tmp" ]; then
  sort -n "$baseline_tmp" -o "$baseline_tmp"
  baseline_peak="$(tail -n 1 "$baseline_tmp")"
fi
rm -f "$baseline_tmp"

echo "Baseline peakRssKiB: $baseline_peak" >&2

FILES="$(collect_files)"
if [ -z "$FILES" ]; then
  echo "ERROR: no input files found under $DATA_DIR" >&2
  exit 93
fi

# ---- Main runs
while IFS= read -r f; do
  for pip in $PIPELINES; do
    echo "==> API [$pip] $f" >&2

    # warmup
    for _ in $(seq 1 "$WARMUP"); do
      read -r _code _resp <<<"$(call_api_once "$f" "$pip")"
      rm -f "$_resp" || true
    done

    times="$(mktemp)"
    peaks="$(mktemp)"
    comment=""

    for _ in $(seq 1 "$RUNS"); do
      read -r code resp <<<"$(call_api_once "$f" "$pip")"

      if [ "$code" != "200" ]; then
        msg="$(json_get "$resp" msg)"
        body_head="$(head -c 120 "$resp" | tr '\n' ' ')"
        comment="http $code${msg:+ - $msg}${body_head:+; body=$body_head}"
        rm -f "$resp"
        break
      fi

      ms="$(json_get "$resp" runtime)"
      pk="$(json_get "$resp" peak)"
      rm -f "$resp"

      if [ -z "$ms" ] || [ -z "$pk" ]; then
        comment="parse error (runtimeMsTotal/peakRssKiB missing)"
        break
      fi

      echo "$ms" >> "$times"
      echo "$pk" >> "$peaks"
    done

    if [ -s "$times" ] && [ -s "$peaks" ]; then
      sort -n "$times" -o "$times"
      sort -n "$peaks" -o "$peaks"

      min_ms="$(head -n 1 "$times")"
      max_ms="$(tail -n 1 "$times")"
      median_ms="$(cat "$times" | median_of_sorted)"

      median_pk="$(cat "$peaks" | median_of_sorted)"

      delta="NA"
      if [ "$baseline_peak" != "NA" ]; then
        delta="$("$PY" - "$median_pk" "$baseline_peak" <<'PY'
import sys
pk=float(sys.argv[1]); base=float(sys.argv[2])
print(pk-base)
PY
)"
      fi

      rm -f "$times" "$peaks"
      echo "\"$pip\",\"$f\",$RUNS,$(fmt0 "$baseline_peak"),$(fmt0 "$median_pk"),$(fmt0 "$delta"),$(fmt3 "$median_ms"),$(fmt3 "$min_ms"),$(fmt3 "$max_ms"),\"\"" >> "$OUT"
    else
      rm -f "$times" "$peaks"
      esc="$(csv_escape "$comment")"
      echo "\"$pip\",\"$f\",$RUNS,$(fmt0 "$baseline_peak"),NA,NA,NA,NA,NA,\"$esc\"" >> "$OUT"
    fi
  done
done <<< "$FILES"

echo "Wrote: $OUT" >&2
