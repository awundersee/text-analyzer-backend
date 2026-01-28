#!/usr/bin/env bash
set -euo pipefail

# -------------------------------
# Performance test runner (CLI+API) – dual pipelines
#
# Features:
# - Runs BOTH pipelines: string + id (for CLI and API)
# - No jq required (uses python3/python for JSON patching + parsing)
# - Collects BOTH: runtime_ms_analyze + runtime_ms_total (CLI) and
#                 meta.runtimeMsAnalyze + meta.runtimeMsTotal (API)
# - Validates: total >= analyze (and analyze>=0, total>0)
# - Timeouts for CLI and API to avoid hangs on huge inputs
# - Keeps going on errors (writes NA rows with comment)
# - Writes per-mode CSVs with an extra "pipeline" column
#
# Usage examples:
#   # API in docker must be running (rebuild after code changes):
#   #   docker compose up -d --build
#   API_URL=http://127.0.0.1:8080/analyze RUNS=5 WARMUP=1 bash tests/performance/scripts/run_perf_tests_dual_pipelines.sh
#
#   # CLI build must exist:
#   cmake -S . -B build && cmake --build build -j
# -------------------------------

# --- JSON parser (portable) ---
PY=""
if command -v python3 >/dev/null 2>&1; then
  PY="python3"
elif command -v python >/dev/null 2>&1; then
  PY="python"
else
  echo "ERROR: Need python3 or python to parse/patch API JSON (jq not installed)." >&2
  exit 90
fi

# --- Portable timeout wrapper ---
# Uses: timeout (Linux), gtimeout (coreutils on macOS), or python-based fallback.
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_CMD="gtimeout"
else
  TIMEOUT_CMD=""
fi

CLI_TIMEOUT_SEC="${CLI_TIMEOUT_SEC:-60}"
API_TIMEOUT_SEC="${API_TIMEOUT_SEC:-60}"

BUILD_DIR="${BUILD_DIR:-build}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"

ts="$(date +%Y%m%d-%H%M%S)"
OUT_FILE_CLI="${OUT_FILE_CLI:-$OUT_DIR/perf_cli_pipelines_${ts}.csv}"
OUT_FILE_API="${OUT_FILE_API:-$OUT_DIR/perf_api_pipelines_${ts}.csv}"

BIN="${BIN:-${BUILD_DIR}/analyze_cli}"

# Local API (defaults)
API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"

RUNS="${RUNS:-5}"
WARMUP="${WARMUP:-1}"

# Which pipelines to test (space-separated)
PIPELINES="${PIPELINES:-string id}"

mkdir -p "$OUT_DIR"
echo "pipeline,file,metric,median_ms,min_ms,max_ms,runs,comment" > "$OUT_FILE_CLI"
echo "pipeline,file,metric,median_ms,min_ms,max_ms,runs,comment" > "$OUT_FILE_API"

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

# CSV escape for comment field (double quotes)
csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

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

# Read meta.runtimeMsAnalyze + meta.runtimeMsTotal from JSON file.
# Prints: "<analyze>\t<total>" or empty string.
json_get_runtimes_api() {
  "$PY" - "$1" <<'PY'
import json, sys
p = sys.argv[1]
try:
    with open(p, "rb") as f:
        o = json.load(f)
    meta = o.get("meta", {}) or {}
    a = meta.get("runtimeMsAnalyze", "")
    t = meta.get("runtimeMsTotal", "")
    if a is None: a = ""
    if t is None: t = ""
    if a == "" or t == "":
        sys.stdout.write("")
    else:
        sys.stdout.write(f"{a}\t{t}")
except Exception:
    sys.stdout.write("")
PY
}

validate_runtimes() {
  "$PY" - "$1" "$2" <<'PY'
import sys
a = float(sys.argv[1])
t = float(sys.argv[2])
sys.exit(0 if (a >= 0.0 and t > 0.0 and t >= a) else 1)
PY
}

# Patch request JSON to enforce a specific pipeline (writes JSON to stdout)
json_with_pipeline() {
  "$PY" - "$1" "$2" <<'PY'
import json, sys
path = sys.argv[1]
pip  = sys.argv[2]
with open(path, "r", encoding="utf-8") as f:
    obj = json.load(f)
obj.setdefault("options", {})
obj["options"]["pipeline"] = pip
print(json.dumps(obj, ensure_ascii=False))
PY
}

# Run a command with timeout; prints stdout+stderr; returns command rc.
run_with_timeout() {
  # usage: run_with_timeout <seconds> <cmd> [args...]
  local seconds="$1"; shift
  if [ -n "$TIMEOUT_CMD" ]; then
    "$TIMEOUT_CMD" "${seconds}s" "$@"
    return $?
  fi
  # python fallback: kill process after seconds
  "$PY" - "$seconds" "$@" <<'PY'
import subprocess, sys, time
timeout_s = float(sys.argv[1])
cmd = sys.argv[2:]
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
start = time.time()
while True:
    if p.poll() is not None:
        break
    if time.time() - start > timeout_s:
        try:
            p.kill()
        except Exception:
            pass
        sys.stdout.write("<<timeout>>\n")
        sys.exit(124)
    time.sleep(0.05)
out = p.stdout.read() if p.stdout else ""
sys.stdout.write(out)
sys.exit(p.returncode if p.returncode is not None else 0)
PY
}

# -------- CLI --------
while IFS= read -r f; do
  for pip in $PIPELINES; do
    echo "==> CLI [$pip] $f" >&2

    # warmup
    for _ in $(seq 1 "$WARMUP"); do
      run_with_timeout "$CLI_TIMEOUT_SEC" "$BIN" "$f" --pipeline "$pip" >/dev/null || true
    done

    times_analyze="$(mktemp)"
    times_total="$(mktemp)"
    comment=""

    for _ in $(seq 1 "$RUNS"); do
      out="$(run_with_timeout "$CLI_TIMEOUT_SEC" "$BIN" "$f" --pipeline "$pip" 2>&1)" || true
      rc=$?

      if [ $rc -ne 0 ]; then
        if [ $rc -eq 124 ]; then
          comment="cli_timeout: >${CLI_TIMEOUT_SEC}s"
        else
          comment="cli_error: exit $rc"
        fi
        break
      fi

      total_ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms_total=/{print $2; exit}' )"
      analyze_ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms_analyze=/{print $2; exit}' )"

      if [ -z "$total_ms" ] || [ -z "$analyze_ms" ]; then
        comment="cli_parse_error: missing runtime_ms_total/runtime_ms_analyze"
        break
      fi

      if ! validate_runtimes "$analyze_ms" "$total_ms" >/dev/null 2>&1; then
        comment="cli_invalid_runtimes: analyze=$analyze_ms total=$total_ms"
        break
      fi

      echo "$analyze_ms" >> "$times_analyze"
      echo "$total_ms" >> "$times_total"
    done

    if [ -s "$times_analyze" ] && [ -s "$times_total" ] && [ -z "$comment" ]; then
      sort -n "$times_analyze" -o "$times_analyze"
      min_a="$(head -n 1 "$times_analyze")"
      max_a="$(tail -n 1 "$times_analyze")"
      med_a="$(cat "$times_analyze" | median_of_sorted)"

      sort -n "$times_total" -o "$times_total"
      min_t="$(head -n 1 "$times_total")"
      max_t="$(tail -n 1 "$times_total")"
      med_t="$(cat "$times_total" | median_of_sorted)"

      rm -f "$times_analyze" "$times_total"

      echo "\"$pip\",\"$f\",analyze,$(fmt3 "$med_a"),$(fmt3 "$min_a"),$(fmt3 "$max_a"),$RUNS,\"\"" >> "$OUT_FILE_CLI"
      echo "\"$pip\",\"$f\",total,$(fmt3 "$med_t"),$(fmt3 "$min_t"),$(fmt3 "$max_t"),$RUNS,\"\"" >> "$OUT_FILE_CLI"
    else
      rm -f "$times_analyze" "$times_total"
      esc_comment="$(csv_escape "$comment")"
      echo "\"$pip\",\"$f\",analyze,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
      echo "\"$pip\",\"$f\",total,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
    fi
  done
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_CLI"

# -------- API --------
while IFS= read -r f; do
  for pip in $PIPELINES; do
    echo "==> API [$pip] $f" >&2

    # build patched request file
    tmp_req="$(mktemp)"
    json_with_pipeline "$f" "$pip" > "$tmp_req"

    # Warmup runs (ignored). If warmup fails, still proceed with measured runs.
    for _ in $(seq 1 "$WARMUP"); do
      curl -sS --max-time "$API_TIMEOUT_SEC" -H "Content-Type: application/json" --data-binary @"$tmp_req" "$API_URL" >/dev/null || true
    done

    times_analyze="$(mktemp)"
    times_total="$(mktemp)"
    comment=""

    for _ in $(seq 1 "$RUNS"); do
      tmp_resp="$(mktemp)"
      tmp_hdr="$(mktemp)"
      tmp_err="$(mktemp)"

      http_code="$(curl --http1.1 --max-time "$API_TIMEOUT_SEC" -sS -D "$tmp_hdr" -o "$tmp_resp" -w "%{http_code}" \
        -H "Content-Type: application/json" --data-binary @"$tmp_req" "$API_URL" 2>"$tmp_err")"
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

      rt="$(json_get_runtimes_api "$tmp_resp")"
      rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"

      if [ -z "$rt" ]; then
        comment="api_error: could not parse meta.runtimeMsAnalyze/meta.runtimeMsTotal"
        break
      fi

      analyze_ms="$(printf "%s" "$rt" | awk -F'\t' '{print $1}')"
      total_ms="$(printf "%s" "$rt" | awk -F'\t' '{print $2}')"

      if ! validate_runtimes "$analyze_ms" "$total_ms" >/dev/null 2>&1; then
        comment="api_error: invalid runtimes (total<analyze or negative): analyze=$analyze_ms total=$total_ms"
        break
      fi

      echo "$analyze_ms" >> "$times_analyze"
      echo "$total_ms" >> "$times_total"
    done

    rm -f "$tmp_req"

    if [ -s "$times_analyze" ] && [ -s "$times_total" ] && [ -z "$comment" ]; then
      sort -n "$times_analyze" -o "$times_analyze"
      min_a="$(head -n 1 "$times_analyze")"
      max_a="$(tail -n 1 "$times_analyze")"
      med_a="$(cat "$times_analyze" | median_of_sorted)"

      sort -n "$times_total" -o "$times_total"
      min_t="$(head -n 1 "$times_total")"
      max_t="$(tail -n 1 "$times_total")"
      med_t="$(cat "$times_total" | median_of_sorted)"

      rm -f "$times_analyze" "$times_total"

      echo "\"$pip\",\"$f\",analyze,$(fmt3 "$med_a"),$(fmt3 "$min_a"),$(fmt3 "$max_a"),$RUNS,\"\"" >> "$OUT_FILE_API"
      echo "\"$pip\",\"$f\",total,$(fmt3 "$med_t"),$(fmt3 "$min_t"),$(fmt3 "$max_t"),$RUNS,\"\"" >> "$OUT_FILE_API"
    else
      rm -f "$times_analyze" "$times_total"
      esc_comment="$(csv_escape "$comment")"
      echo "\"$pip\",\"$f\",analyze,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
      echo "\"$pip\",\"$f\",total,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
    fi
  done
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_API"
