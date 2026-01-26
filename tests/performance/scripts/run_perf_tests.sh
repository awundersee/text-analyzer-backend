#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
DATA_DIR="${DATA_DIR:-tests/performance/data}"
OUT_DIR="${OUT_DIR:-tests/performance/results}"

OUT_FILE_CLI="${OUT_FILE_CLI:-$OUT_DIR/perf_cli.csv}"
OUT_FILE_API="${OUT_FILE_API:-$OUT_DIR/perf_api.csv}"

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

# -------- CLI --------
while IFS= read -r f; do
  echo "==> CLI $f" >&2

  for _ in $(seq 1 "$WARMUP"); do
    "$BIN" "$f" >/dev/null
  done

  times_file="$(mktemp)"
  for _ in $(seq 1 "$RUNS"); do
    out="$("$BIN" "$f")" || { echo "analyze_cli failed for $f" >&2; exit 4; }
    ms="$(echo "$out" | awk -F= '/^runtime_ms=/{print $2}' | tail -n 1)"
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

  echo "\"$f\",$median_ms,$min_ms,$max_ms,$RUNS,\"\"" >> "$OUT_FILE_CLI"
done <<< "$FILES"

echo "==> Wrote $OUT_FILE_CLI"

  # -------- API --------
  while IFS= read -r f; do
    echo "==> API $f" >&2

    # Warmup runs (ignored) - wenn Warmup schon fehlschlägt: trotzdem weitermachen
    warmup_ok=1
    for _ in $(seq 1 "$WARMUP"); do
      if ! curl -sS -H "Content-Type: application/json" --data-binary @"$f" "$API_URL" >/dev/null; then
        warmup_ok=0
        break
      fi
    done

    times_file="$(mktemp)"
    comment=""

    # Measured runs
    for _ in $(seq 1 "$RUNS"); do
      resp="$(curl -sS -H "Content-Type: application/json" --data-binary @"$f" "$API_URL" 2>/dev/null || true)"

      # Falls leer/keine JSON Antwort:
      if [ -z "$resp" ]; then
        comment="api_error: empty response"
        break
      fi

      # Versuche runtimeMs zu parsen
      if command -v jq >/dev/null 2>&1; then
        ms="$(echo "$resp" | jq -r '.meta.runtimeMs // empty' 2>/dev/null || true)"
        # wenn meta.runtimeMs fehlt, versuche message
        if [ -z "$ms" ]; then
          msg="$(echo "$resp" | jq -r '.message // .error // empty' 2>/dev/null || true)"
        else
          msg=""
        fi
      else
        ms="$(python3 - <<'PY' <<< "$resp" 2>/dev/null || true
import json, sys
obj=json.load(sys.stdin)
v=obj.get("meta",{}).get("runtimeMs","")
print(v if v is not None else "")
PY
)"
        if [ -z "$ms" ]; then
          msg="$(python3 - <<'PY' <<< "$resp" 2>/dev/null || true
import json, sys
obj=json.load(sys.stdin)
print(obj.get("message") or obj.get("error") or "")
PY
)"
        else
          msg=""
        fi
      fi

      if [ -z "$ms" ]; then
        # API hat Fehler gemeldet oder format passt nicht
        if [ -n "${msg:-}" ]; then
          comment="api_error: $msg"
        else
          comment="api_error: could not parse meta.runtimeMs"
        fi
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

      # Comment leer
      echo "\"$f\",$median_ms,$min_ms,$max_ms,$RUNS,\"\"" >> "$OUT_FILE_API"
    else
      rm -f "$times_file"
      # Keine Messwerte -> NA
      # comment CSV-sicher machen (Quotes escapen)
      esc_comment="$(printf '%s' "$comment" | sed 's/"/""/g')"
      echo "\"$f\",NA,NA,NA,$RUNS,\"$esc_comment\"" >> "$OUT_FILE_API"
      # Weiter mit nächster Datei
      continue
    fi
  done <<< "$FILES"


echo "==> Wrote $OUT_FILE_API"
