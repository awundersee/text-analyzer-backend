#!/usr/bin/env bash
set -uo pipefail

API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"
PIPELINES="${PIPELINES:-string id}"
CONCURRENCY="${CONCURRENCY:-1 4 8}"
WARMUP="${WARMUP:-1}"
INCLUDE_LARGE="${INCLUDE_LARGE:-0}"

PY="$(command -v python3 || command -v python)"
mkdir -p "$OUT_DIR"

ts="$(date +%Y%m%d-%H%M%S)"
OUT="$OUT_DIR/perf_api_concurrency_${ts}.csv"
echo "concurrency,pipeline,file,ok,count,medianRuntimeMs,medianPeakKiB,comment" > "$OUT"

# ---------------- Health check ----------------
if ! curl -fsS "${API_URL%/analyze}/health" >/dev/null; then
  echo "ERROR: API not reachable at $API_URL" >&2
  exit 1
fi

median_of_sorted() {
  awk '{a[NR]=$1} END{ if(NR==0) exit 1; if(NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'
}

fmt3() { awk -v x="$1" 'BEGIN{ if(x=="") print "NA"; else printf "%.3f", x }'; }
fmt0() { awk -v x="$1" 'BEGIN{ if(x=="") print "NA"; else printf "%.0f", x }'; }
csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

json_with_pipeline() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as f: o=json.load(f)
o.setdefault("options", {})["pipeline"] = sys.argv[2]
print(json.dumps(o))
PY
}

json_get() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
try:
    with open(sys.argv[1]) as f: o=json.load(f)
    v=o.get("meta",{}).get(sys.argv[2],"")
    print(v)
except Exception:
    print("")
PY
}

collect_files() {
  find "$DATA_DIR/small" "$DATA_DIR/medium" \
    -type f -name "*.json" 2>/dev/null | sort
}

FILES="$(collect_files)"

# ---------------- Warmup ----------------
first="$(echo "$FILES" | head -n1)"
for pip in $PIPELINES; do
  req="$(mktemp)"
  json_with_pipeline "$first" "$pip" > "$req"
  for _ in $(seq 1 "$WARMUP"); do
    curl -sS -H "Content-Type: application/json" --data-binary @"$req" "$API_URL" >/dev/null || true
  done
  rm -f "$req"
done

# ---------------- Main ----------------
while IFS= read -r f; do
  for pip in $PIPELINES; do
    for c in $CONCURRENCY; do
      echo "==> API concurrency=$c [$pip] $f" >&2

      req="$(mktemp)"
      json_with_pipeline "$f" "$pip" > "$req"
      work="$(mktemp -d)"

      for i in $(seq 1 "$c"); do
        (
          curl -sS -H "Content-Type: application/json" \
            --data-binary @"$req" "$API_URL" \
            -o "$work/resp_$i.json" \
            -w "%{http_code}" > "$work/code_$i.txt" \
          || echo "000" > "$work/code_$i.txt"
        ) &
      done
      wait

      times=(); peaks=(); ok=0; comment=""
      for i in $(seq 1 "$c"); do
        code="$(cat "$work/code_$i.txt")"
        resp="$work/resp_$i.json"
        if [ "$code" = "200" ]; then
          ms="$(json_get "$resp" runtimeMsTotal)"
          pk="$(json_get "$resp" peakRssKiB)"
          if [ -n "$ms" ] && [ -n "$pk" ]; then
            times+=("$ms"); peaks+=("$pk"); ok=$((ok+1))
          fi
        else
          comment="http $code"
        fi
      done

      if [ "$ok" -gt 0 ]; then
        printf "%s\n" "${times[@]}" | sort -n > /tmp/times.$$
        printf "%s\n" "${peaks[@]}" | sort -n > /tmp/peaks.$$
        med_ms="$(median_of_sorted </tmp/times.$$)"
        med_pk="$(median_of_sorted </tmp/peaks.$$)"
        echo "\"$c\",\"$pip\",\"$f\",1,$ok,$(fmt3 "$med_ms"),$(fmt0 "$med_pk"),\"\"" >> "$OUT"
      else
        esc="$(csv_escape "$comment")"
        echo "\"$c\",\"$pip\",\"$f\",0,0,NA,NA,\"$esc\"" >> "$OUT"
      fi

      rm -rf "$work" "$req"
    done
  done
done <<< "$FILES"

echo "Wrote: $OUT"
