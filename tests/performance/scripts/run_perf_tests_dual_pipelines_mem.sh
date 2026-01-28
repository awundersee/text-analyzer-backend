#!/usr/bin/env bash
set -euo pipefail

# -------------------------------
# Performance test runner (CLI+API) – dual pipelines + memory (peak RSS KiB)
#
# Collects per run:
#   CLI (stdout lines):
#     runtime_ms_analyze=<ms>
#     runtime_ms_total=<ms>
#     peak_rss_kib_analyze=<kib>
#     peak_rss_kib_total=<kib>
#
#   API (JSON meta):
#     runtimeMsAnalyze, runtimeMsTotal
#     peakRssKiBAnalyze, peakRssKiBTotal
#
# Backward-compat fallbacks:
#   CLI: runtime_ms=..., peak_rss_kib=...
#   API: meta.runtimeMs, meta.peakRssKiB
#
# Features:
# - Runs BOTH pipelines: string + id (for CLI and API)
# - No jq required (python3/python used for JSON patching + parsing)
# - Timeouts for CLI + API to avoid hangs on huge inputs
# - Keeps going on errors (writes NA rows with comment)
# - Writes per-mode CSVs with an extra "metric" column (analyze|total)
#
# Env:
#   BUILD_DIR=build
#   DATA_DIR=tests/performance/data
#   OUT_DIR=tests/performance/results
#   BIN=build/analyze_cli
#   API_URL=http://127.0.0.1:8080/analyze
#   RUNS=5
#   WARMUP=1
#   PIPELINES="string id"
#   INCLUDE_LARGE=0|1   (default 0)
#   CLI_TIMEOUT_SEC=60  (default 60)
#   API_TIMEOUT_SEC=60  (default 60)
# -------------------------------

# --- JSON parser (portable) ---
PY=""
if command -v python3 >/dev/null 2>&1; then
  PY="python3"
elif command -v python >/dev/null 2>&1; then
  PY="python"
else
  echo "ERROR: Need python3 or python to parse/patch API JSON." >&2
  exit 90
fi

# --- Portable timeout wrapper ---
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
OUT_FILE_CLI="${OUT_FILE_CLI:-$OUT_DIR/perf_cli_mem_${ts}.csv}"
OUT_FILE_API="${OUT_FILE_API:-$OUT_DIR/perf_api_mem_${ts}.csv}"

BIN="${BIN:-${BUILD_DIR}/analyze_cli}"
API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"

RUNS="${RUNS:-5}"
WARMUP="${WARMUP:-1}"
PIPELINES="${PIPELINES:-string id}"
INCLUDE_LARGE="${INCLUDE_LARGE:-0}"

mkdir -p "$OUT_DIR"

echo "pipeline,file,metric,median_ms,min_ms,max_ms,median_peak_kib,min_peak_kib,max_peak_kib,runs,comment" > "$OUT_FILE_CLI"
echo "pipeline,file,metric,median_ms,min_ms,max_ms,median_peak_kib,min_peak_kib,max_peak_kib,runs,comment" > "$OUT_FILE_API"

if [ "$RUNS" -lt 1 ]; then
  echo "RUNS must be >= 1" >&2
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
fmt0() { awk -v x="$1" 'BEGIN{ if (x=="NA" || x=="") print x; else printf "%.0f", x }'; }

# CSV escape for comment field (double quotes)
csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

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

# Extract error message from JSON response, if any
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

# Extract API runtimes + peaks:
# Prints: "<a_ms>\t<t_ms>\t<a_pk>\t<t_pk>" or empty string.
json_get_api_metrics() {
  "$PY" - "$1" <<'PY'
import json, sys
p = sys.argv[1]
def norm(x):
    return "" if x is None else x
try:
    with open(p, "rb") as f:
        o = json.load(f)
    meta = o.get("meta", {}) or {}

    a_ms = norm(meta.get("runtimeMsAnalyze", ""))
    t_ms = norm(meta.get("runtimeMsTotal", ""))
    a_pk = norm(meta.get("peakRssKiBAnalyze", ""))
    t_pk = norm(meta.get("peakRssKiBTotal", ""))

    # fallback to legacy single fields
    if a_ms == "" or t_ms == "":
        legacy_ms = norm(meta.get("runtimeMs", ""))
        if legacy_ms != "" and a_ms == "": a_ms = legacy_ms
        if legacy_ms != "" and t_ms == "": t_ms = legacy_ms
    if a_pk == "" or t_pk == "":
        legacy_pk = norm(meta.get("peakRssKiB", ""))
        if legacy_pk != "" and a_pk == "": a_pk = legacy_pk
        if legacy_pk != "" and t_pk == "": t_pk = legacy_pk

    if a_ms == "" or t_ms == "" or a_pk == "" or t_pk == "":
        sys.stdout.write("")
    else:
        sys.stdout.write(f"{a_ms}\t{t_ms}\t{a_pk}\t{t_pk}")
except Exception:
    sys.stdout.write("")
PY
}

validate_runtime_pair() {
  "$PY" - "$1" "$2" <<'PY'
import sys
a = float(sys.argv[1]); t = float(sys.argv[2])
sys.exit(0 if (a >= 0.0 and t > 0.0 and t >= a) else 1)
PY
}

# Prefer small -> medium -> (optional large)
collect_files() {
  local tmp
  tmp="$(mktemp)"
  find "$DATA_DIR/small"  -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  find "$DATA_DIR/medium" -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  if [ "$INCLUDE_LARGE" = "1" ]; then
    find "$DATA_DIR/large" -type f -name "*.json" 2>/dev/null | sort >> "$tmp" || true
  fi
  cat "$tmp"
  rm -f "$tmp"
}

FILES="$(collect_files)"
if [ -z "${FILES}" ]; then
  echo "No input files found under: $DATA_DIR (small/medium/large)" >&2
  exit 2
fi

# -------- CLI --------
while IFS= read -r f; do
  for pip in $PIPELINES; do
    echo "==> CLI [$pip] $f" >&2

    # warmup (silence)
    for _ in $(seq 1 "$WARMUP"); do
      run_with_timeout "$CLI_TIMEOUT_SEC" "$BIN" "$f" --pipeline "$pip" >/dev/null 2>&1 || true
    done

    a_times="$(mktemp)"; t_times="$(mktemp)"
    a_peaks="$(mktemp)"; t_peaks="$(mktemp)"

    for _ in $(seq 1 "$RUNS"); do
      out="$(run_with_timeout "$CLI_TIMEOUT_SEC" "$BIN" "$f" --pipeline "$pip" 2>&1)" || {
        esc_comment="$(csv_escape "cli_error: analyze_cli failed/timeout: ${out//$'\n'/ }")"
        rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"
        echo "\"$pip\",\"$f\",\"analyze\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        echo "\"$pip\",\"$f\",\"total\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        continue 2
      }

      a_ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms_analyze=/{print $2; exit}')"
      t_ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms_total=/{print $2; exit}')"
      a_pk="$(printf "%s\n" "$out" | awk -F= '/^peak_rss_kib_analyze=/{print $2; exit}')"
      t_pk="$(printf "%s\n" "$out" | awk -F= '/^peak_rss_kib_total=/{print $2; exit}')"

      # legacy fallback
      if [ -z "$a_ms" ] || [ -z "$t_ms" ]; then
        legacy_ms="$(printf "%s\n" "$out" | awk -F= '/^runtime_ms=/{print $2; exit}')"
        [ -n "$legacy_ms" ] && { a_ms="${a_ms:-$legacy_ms}"; t_ms="${t_ms:-$legacy_ms}"; }
      fi
      if [ -z "$a_pk" ] || [ -z "$t_pk" ]; then
        legacy_pk="$(printf "%s\n" "$out" | awk -F= '/^peak_rss_kib=/{print $2; exit}')"
        [ -n "$legacy_pk" ] && { a_pk="${a_pk:-$legacy_pk}"; t_pk="${t_pk:-$legacy_pk}"; }
      fi

      if [ -z "$a_ms" ] || [ -z "$t_ms" ] || [ -z "$a_pk" ] || [ -z "$t_pk" ]; then
        esc_comment="$(csv_escape "cli_error: could not parse runtimes/peaks; output=${out//$'\n'/ }")"
        rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"
        echo "\"$pip\",\"$f\",\"analyze\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        echo "\"$pip\",\"$f\",\"total\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        continue 2
      fi

      if ! validate_runtime_pair "$a_ms" "$t_ms"; then
        esc_comment="$(csv_escape "cli_error: invalid runtimes (total must be >= analyze): analyze=$a_ms total=$t_ms")"
        rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"
        echo "\"$pip\",\"$f\",\"analyze\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        echo "\"$pip\",\"$f\",\"total\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_CLI"
        continue 2
      fi

      echo "$a_ms" >> "$a_times"; echo "$t_ms" >> "$t_times"
      echo "$a_pk" >> "$a_peaks"; echo "$t_pk" >> "$t_peaks"
    done

    # analyze stats
    sort -n "$a_times" -o "$a_times"; sort -n "$a_peaks" -o "$a_peaks"
    a_min_ms="$(head -n 1 "$a_times")"; a_max_ms="$(tail -n 1 "$a_times")"; a_med_ms="$(cat "$a_times" | median_of_sorted)"
    a_min_pk="$(head -n 1 "$a_peaks")"; a_max_pk="$(tail -n 1 "$a_peaks")"; a_med_pk="$(cat "$a_peaks" | median_of_sorted)"

    # total stats
    sort -n "$t_times" -o "$t_times"; sort -n "$t_peaks" -o "$t_peaks"
    t_min_ms="$(head -n 1 "$t_times")"; t_max_ms="$(tail -n 1 "$t_times")"; t_med_ms="$(cat "$t_times" | median_of_sorted)"
    t_min_pk="$(head -n 1 "$t_peaks")"; t_max_pk="$(tail -n 1 "$t_peaks")"; t_med_pk="$(cat "$t_peaks" | median_of_sorted)"

    rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"

    echo "\"$pip\",\"$f\",\"analyze\",$(fmt3 "$a_med_ms"),$(fmt3 "$a_min_ms"),$(fmt3 "$a_max_ms"),$(fmt0 "$a_med_pk"),$(fmt0 "$a_min_pk"),$(fmt0 "$a_max_pk"),$RUNS,\"\"" >> "$OUT_FILE_CLI"
    echo "\"$pip\",\"$f\",\"total\",$(fmt3 "$t_med_ms"),$(fmt3 "$t_min_ms"),$(fmt3 "$t_max_ms"),$(fmt0 "$t_med_pk"),$(fmt0 "$t_min_pk"),$(fmt0 "$t_max_pk"),$RUNS,\"\"" >> "$OUT_FILE_CLI"
  done
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_CLI" >&2

# -------- API --------
while IFS= read -r f; do
  for pip in $PIPELINES; do
    echo "==> API [$pip] $f" >&2

    tmp_req="$(mktemp)"
    json_with_pipeline "$f" "$pip" > "$tmp_req"

    # Warmup runs (ignored)
    for _ in $(seq 1 "$WARMUP"); do
      curl -sS --max-time "$API_TIMEOUT_SEC" -H "Content-Type: application/json" --data-binary @"$tmp_req" "$API_URL" >/dev/null || true
    done

    a_times="$(mktemp)"; t_times="$(mktemp)"
    a_peaks="$(mktemp)"; t_peaks="$(mktemp)"
    comment=""

    for _ in $(seq 1 "$RUNS"); do
      tmp_resp="$(mktemp)"
      tmp_hdr="$(mktemp)"
      tmp_err="$(mktemp)"

      http_code="$(curl --http1.1 -sS --max-time "$API_TIMEOUT_SEC" -D "$tmp_hdr" -o "$tmp_resp" -w "%{http_code}" \
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

      vals="$(json_get_api_metrics "$tmp_resp")"
      rm -f "$tmp_resp" "$tmp_hdr" "$tmp_err"

      if [ -z "$vals" ]; then
        comment="api_error: could not parse meta runtimes/peaks"
        break
      fi

      a_ms="$(printf "%s" "$vals" | awk -F'\t' '{print $1}')"
      t_ms="$(printf "%s" "$vals" | awk -F'\t' '{print $2}')"
      a_pk="$(printf "%s" "$vals" | awk -F'\t' '{print $3}')"
      t_pk="$(printf "%s" "$vals" | awk -F'\t' '{print $4}')"

      if ! validate_runtime_pair "$a_ms" "$t_ms"; then
        comment="api_error: invalid runtimes (total must be >= analyze): analyze=$a_ms total=$t_ms"
        break
      fi

      echo "$a_ms" >> "$a_times"; echo "$t_ms" >> "$t_times"
      echo "$a_pk" >> "$a_peaks"; echo "$t_pk" >> "$t_peaks"
    done

    rm -f "$tmp_req"

    if [ -s "$a_times" ] && [ -s "$t_times" ] && [ -s "$a_peaks" ] && [ -s "$t_peaks" ]; then
      sort -n "$a_times" -o "$a_times"; sort -n "$a_peaks" -o "$a_peaks"
      a_min_ms="$(head -n 1 "$a_times")"; a_max_ms="$(tail -n 1 "$a_times")"; a_med_ms="$(cat "$a_times" | median_of_sorted)"
      a_min_pk="$(head -n 1 "$a_peaks")"; a_max_pk="$(tail -n 1 "$a_peaks")"; a_med_pk="$(cat "$a_peaks" | median_of_sorted)"

      sort -n "$t_times" -o "$t_times"; sort -n "$t_peaks" -o "$t_peaks"
      t_min_ms="$(head -n 1 "$t_times")"; t_max_ms="$(tail -n 1 "$t_times")"; t_med_ms="$(cat "$t_times" | median_of_sorted)"
      t_min_pk="$(head -n 1 "$t_peaks")"; t_max_pk="$(tail -n 1 "$t_peaks")"; t_med_pk="$(cat "$t_peaks" | median_of_sorted)"

      rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"

      echo "\"$pip\",\"$f\",\"analyze\",$(fmt3 "$a_med_ms"),$(fmt3 "$a_min_ms"),$(fmt3 "$a_max_ms"),$(fmt0 "$a_med_pk"),$(fmt0 "$a_min_pk"),$(fmt0 "$a_max_pk"),$RUNS,\"\"" >> "$OUT_FILE_API"
      echo "\"$pip\",\"$f\",\"total\",$(fmt3 "$t_med_ms"),$(fmt3 "$t_min_ms"),$(fmt3 "$t_max_ms"),$(fmt0 "$t_med_pk"),$(fmt0 "$t_min_pk"),$(fmt0 "$t_max_pk"),$RUNS,\"\"" >> "$OUT_FILE_API"
    else
      rm -f "$a_times" "$t_times" "$a_peaks" "$t_peaks"
      esc_comment="$(csv_escape "$comment")"
      echo "\"$pip\",\"$f\",\"analyze\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
      echo "\"$pip\",\"$f\",\"total\",NA,NA,NA,NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
      continue
    fi
  done
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_API" >&2
