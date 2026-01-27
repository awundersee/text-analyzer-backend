#!/usr/bin/env bash
set -euo pipefail

# -------------------------------
# Performance test runner (CLI+API)
# - Robust JSON parsing (no swallowed indentation errors)
# - API error comments include "too many pages" when returned by server
# - Keeps going on errors (writes NA row with comment)
# -------------------------------

# --- JSON parser (portable) ---
PY=""
if command -v python3 >/dev/null 2>&1; then
  PY="python3"
elif command -v python >/dev/null 2>&1; then
  PY="python"
else
  echo "ERROR: Need python3 or python to parse API JSON (jq not installed)." >&2
  exit 90
fi

BUILD_DIR="${BUILD_DIR:-build}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"

ts="$(date +%Y%m%d-%H%M%S)"
OUT_FILE_CLI="${OUT_FILE_CLI:-$OUT_DIR/perf_cli_${ts}.csv}"
OUT_FILE_API="${OUT_FILE_API:-$OUT_DIR/perf_api_${ts}.csv}"

BIN="${BIN:-${BUILD_DIR}/analyze_cli}"

# Local API (defaults)
API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"

RUNS="${RUNS:-5}"
WARMUP="${WARMUP:-1}"

mkdir -p "$OUT_DIR"
echo "file,median_ms,min_ms,max_ms,runs,comment" > "$OUT_FILE_CLI"
echo "file,median_ms,min_ms,max_ms,runs,comment" > "$OUT_FILE_API"

FILES=$(find "$DATA_DIR" -type f -name "*.json" | sort || true)
if [ -z "${FILES}" ]; then
  echo "No input files found under: $DATA_DIR"
  exit 2
fi

if [ "$RUNS" -lt 1 ]; then
  echo "RUNS must be >= 1"
  exit 3
fi

median_of_sorted() {
  awk '
  { a[NR] = $1 }
  END {
    if (NR == 0) { exit 10 }
    if (NR % 2 == 1) { print a[(NR+1)/2] }
    else { print (a[NR/2] + a[NR/2+1]) / 2 }
  }'
}

fmt3() { awk -v x="$1" 'BEGIN{ if (x=="NA" || x=="") print x; else printf "%.3f", x }'; }

# CSV escape for comment field
csv_escape() {
  # doubles quotes
  printf '%s' "$1" | sed 's/"/""/g'
}

# Read error/message from JSON file if possible; otherwise empty string.
json_get_msg() {
  "$PY" - "$1" <<'PY'
import json, sys
p = sys.argv[1]
try:
    with open(p, "rb") as f:
        o = json.load(f)
    msg = o.get("message") or o.get("error") or ""
    sys.stdout.write(str(msg))
except Exception:
    sys.stdout.write("")
PY
}

# Read meta.runtimeMs from JSON file if possible; otherwise empty string.
json_get_runtime_ms() {
  "$PY" - "$1" <<'PY'
import json, sys
p = sys.argv[1]
try:
    with open(p, "rb") as f:
        o = json.load(f)
    v = o.get("meta", {}).get("runtimeMs", "")
    if v is None:
        sys.stdout.write("")
    else:
        sys.stdout.write(str(v))
except Exception:
    sys.stdout.write("")
PY
}

# -------- CLI --------
while IFS= read -r f; do
  echo "==> CLI $f" >&2

  for _ in $(seq 1 "$WARMUP"); do
    "$BIN" "$f" >/dev/null
  done

  times_file="$(mktemp)"
  for _ in $(seq 1 "$RUNS"); do
    out="$("$BIN" "$f" 2>&1)" || { echo "analyze_cli failed for $f" >&2; exit 4; }
    ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms=/{print $2; exit}' )"
    if [ -z "$ms" ]; then
      echo "Could not parse runtime_ms from analyze_cli output for $f" >&2
      echo "Output was: $out" >&2
      exit 5
    fi
    echo "$ms" >> "$times_file"
  done

  sort -n "$times_file" -o "$times_file"
  min_ms="$(head -n 1 "$times_file")"
  max_ms="$(tail -n 1 "$times_file")"
  median_ms="$(cat "$times_file" | median_of_sorted)"
  rm -f "$times_file"

  echo "\"$f\",$(fmt3 "$median_ms"),$(fmt3 "$min_ms"),$(fmt3 "$max_ms"),$RUNS,\"\"" >> "$OUT_FILE_CLI"
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_CLI"

# -------- API --------
while IFS= read -r f; do
  echo "==> API $f" >&2

  # Warmup runs (ignored). If warmup fails, still proceed with measured runs.
  for _ in $(seq 1 "$WARMUP"); do
    curl -sS -H "Content-Type: application/json" --data-binary @"$f" "$API_URL" >/dev/null || true
  done

  times_file="$(mktemp)"
  comment=""

  for _ in $(seq 1 "$RUNS"); do
    tmp_resp="$(mktemp)"
    tmp_hdr="$(mktemp)"
    tmp_err="$(mktemp)"

    # Body -> tmp_resp, headers -> tmp_hdr, http_code -> stdout, stderr -> tmp_err
    http_code="$(curl --http1.1 -sS -D "$tmp_hdr" -o "$tmp_resp" -w "%{http_code}" \
      -H "Content-Type: application/json" --data-binary @"$f" "$API_URL" 2>"$tmp_err")"
    curl_rc=$?

    if [ $curl_rc -ne 0 ]; then
      err_head="$(head -c 200 "$tmp_err" | tr '\n' ' ')"
      comment="api_error: curl exit $curl_rc ${err_head}"
      rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"
      break
    fi

    if [ -z "$http_code" ] || [ "$http_code" = "000" ]; then
      err_head="$(head -c 200 "$tmp_err" | tr '\n' ' ')"
      comment="api_error: http_code $http_code ${err_head}"
      rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"
      break
    fi

    if [ "$http_code" != "200" ]; then
      # Try to extract message from JSON; if not JSON, include content-type + head
      msg="$(json_get_msg "$tmp_resp")"
      ct="$(awk -F': ' 'tolower($1)=="content-type"{print $2}' "$tmp_hdr" | head -n1 | tr -d '\r')"
      body_head="$(head -c 160 "$tmp_resp" | tr '\n' ' ')"
      if [ -n "$msg" ]; then
        comment="api_error: http $http_code - $msg"
      else
        comment="api_error: http $http_code${ct:+; ct=$ct}${body_head:+; body=${body_head}}"
      fi
      rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"
      break
    fi

    ms="$(json_get_runtime_ms "$tmp_resp")"

    rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"

    if [ -z "$ms" ]; then
      comment="api_error: could not parse meta.runtimeMs"
      break
    fi

    echo "$ms" >> "$times_file"
  done

  if [ -s "$times_file" ]; then
    sort -n "$times_file" -o "$times_file"
    min_ms="$(head -n 1 "$times_file")"
    max_ms="$(tail -n 1 "$times_file")"
    median_ms="$(cat "$times_file" | median_of_sorted)"
    rm -f "$times_file"
    echo "\"$f\",$(fmt3 "$median_ms"),$(fmt3 "$min_ms"),$(fmt3 "$max_ms"),$RUNS,\"\"" >> "$OUT_FILE_API"
  else
    rm -f "$times_file"
    esc_comment="$(csv_escape "$comment")"
    echo "\"$f\",NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
    continue
  fi
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_API"
