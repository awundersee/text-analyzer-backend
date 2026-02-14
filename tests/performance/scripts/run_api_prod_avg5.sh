#!/usr/bin/env bash
set -euo pipefail

API_URL="${API_URL:-https://textanalyse.wundersee.dev/analyze}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"
RUNS="${RUNS:-5}"

PY="$(command -v python3 || command -v python)"
mkdir -p "$OUT_DIR"

ts="$(date +%Y%m%d-%H%M%S)"
OUT="$OUT_DIR/perf_api_prod_avg_${RUNS}runs_${ts}.csv"

echo "file,runs,ok,avgRuntimeAnalyzeMs,avgRuntimeTotalMs,avgPeakKiB,comment" > "$OUT"

FILES="$(find "$DATA_DIR" -type f -name "*.json" 2>/dev/null | sort || true)"
if [ -z "$FILES" ]; then
  echo "ERROR: Keine .json Dateien gefunden unter: $DATA_DIR" >&2
  echo "Wrote: $OUT" >&2
  exit 3
fi

# Smoke test (non-fatal): pick the smallest file to avoid 413
first="$(
  find "$DATA_DIR" -type f -name "*.json" -print0 2>/dev/null \
  | xargs -0 ls -1S 2>/dev/null \
  | tail -n 1 || true
)"
if [ -n "$first" ]; then
  code="$(curl -sS -o /dev/null -w "%{http_code}" \
    -X POST "$API_URL" \
    -H "Content-Type: application/json" \
    --data-binary @"$first" || echo "000")"
  if [ "$code" != "200" ]; then
    echo "WARN: smoke request failed (http $code) at $API_URL using $first — continuing" >&2
  fi
fi

avg_of_list() { awk '{s+=$1;n+=1} END{ if(n==0) print "NA"; else printf "%.3f", (s/n) }'; }
avg_of_list_int() { awk '{s+=$1;n+=1} END{ if(n==0) print "NA"; else printf "%.0f", (s/n) }'; }
csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

# Return a human reason string for non-200 responses
# 1) try JSON {"message": "..."} or {"error": "..."}
# 2) else fallback per HTTP code
reason_from_response() {
  local http="$1"
  local resp="$2"

  # 1) Prefer server-provided JSON message/error
  local msg=""
  msg="$("$PY" - <<PY 2>/dev/null
import json
p="$resp"
try:
  with open(p,"r",encoding="utf-8") as f:
    o=json.load(f)
  m = o.get("message") or o.get("error") or ""
  print(m.strip() if isinstance(m,str) else "")
except Exception:
  print("")
PY
)"

  if [ -n "$msg" ]; then
    echo "$msg"
    return 0
  fi

  # 2) Fallback: map common proxy/backend status codes to readable reasons
  case "$http" in
    413) echo "Payload Too Large" ;;
    400) echo "Bad Request" ;;
    401) echo "Unauthorized" ;;
    500) echo "Internal Server Error" ;;
    503) echo "Service Unavailable" ;;
    000) echo "Network Error" ;;
    *)   echo "http $http" ;;
  esac
}

while IFS= read -r f; do
  base="$(basename "$f")"
  echo "==> $base (runs=$RUNS)" >&2

  analyze_vals="$(mktemp)"
  total_vals="$(mktemp)"
  peak_vals="$(mktemp)"
  errs="$(mktemp)"
  : > "$analyze_vals"; : > "$total_vals"; : > "$peak_vals"; : > "$errs"

  ok=0
  comment=""

  for i in $(seq 1 "$RUNS"); do
    tmp="$(mktemp)"

    http="$(curl -sS -o "$tmp" -w "%{http_code}" \
      -X POST "$API_URL" \
      -H "Content-Type: application/json" \
      --data-binary @"$f" || echo "000")"

    if [ "$http" = "200" ]; then
      read -r ama tma pk < <("$PY" - <<PY 2>/dev/null
import json
try:
  j=json.load(open("$tmp","r",encoding="utf-8"))
  m=j.get("meta",{})
  a=m.get("runtimeMsAnalyze")
  t=m.get("runtimeMsTotal")
  p=m.get("peakRssKiB")
  def fmt(x):
    return "NA" if x is None else str(x)
  print(fmt(a), fmt(t), fmt(p))
except Exception:
  print("NA","NA","NA")
PY
)

      if [ "$ama" != "NA" ] && [ "$tma" != "NA" ]; then
        echo "$ama" >> "$analyze_vals"
        echo "$tma" >> "$total_vals"
        if [ "$pk" != "NA" ]; then
          echo "$pk" >> "$peak_vals"
        fi
        ok=$((ok+1))
      else
        echo "missing meta fields|1" >> "$errs"
      fi
    else
      reason="$(reason_from_response "$http" "$tmp")"
      # store reason occurrences (one per run)
      echo "$reason|1" >> "$errs"

      # Fast-skip: if payload too large, remaining runs will be the same
      if [ "$http" = "413" ]; then
        remaining=$((RUNS - i))
        if [ "$remaining" -gt 0 ]; then
          echo "$reason|$remaining" >> "$errs"
        fi
        rm -f "$tmp"
        break
      fi
    fi

    rm -f "$tmp"
  done

  # Build comment summary like "payload too large x5, timeout x2"
  if [ -s "$errs" ]; then
    summary="$(
      awk -F'|' '{c[$1]+=$2} END{
        n=0;
        for(k in c){
          if(n) printf ", ";
          printf "%s x%d", k, c[k];
          n++;
        }
      }' "$errs"
    )"
    comment="$summary"
  fi

  avg_analyze="$(avg_of_list < "$analyze_vals")"
  avg_total="$(avg_of_list < "$total_vals")"
  avg_peak="$(avg_of_list_int < "$peak_vals")"

  rm -f "$analyze_vals" "$total_vals" "$peak_vals" "$errs"

  esc="$(csv_escape "$comment")"
  echo "\"$base\",$RUNS,$ok,$avg_analyze,$avg_total,$avg_peak,\"$esc\"" >> "$OUT"
done <<< "$FILES"

echo "Wrote: $OUT" >&2
exit 0
