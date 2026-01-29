\
#!/usr/bin/env bash
set -euo pipefail

API_URL="${API_URL:-http://localhost:8080}"
REQ_DIR="tests/api/requests"
EXP_DIR="tests/api/expected"

mkdir -p "$EXP_DIR"

# portable python detection
PY=""
if command -v python3 >/dev/null 2>&1; then
  PY="python3"
elif command -v python >/dev/null 2>&1; then
  PY="python"
else
  echo "ERROR: python is required (no jq dependency)." >&2
  exit 90
fi

CURL_COMMON=(-sS -X POST "$API_URL/analyze" -H "Content-Type: application/json")

for f in "$REQ_DIR"/*.json; do
  base="$(basename "$f" .json)"
  echo "Generating expected for $base ..."

  tmp_body="$(mktemp)"
  tmp_err="$(mktemp)"

  http_code="$(
    curl "${CURL_COMMON[@]}" \
      --data-binary @"$f" \
      -o "$tmp_body" \
      -w "%{http_code}" \
      2>"$tmp_err" || true
  )"

  if [[ -s "$tmp_err" ]]; then
    echo "WARN: curl stderr for $base:" >&2
    sed -e 's/^/  /' "$tmp_err" >&2
  fi

  if [[ "$http_code" != "200" ]]; then
    echo "ERROR: $base -> HTTP $http_code (expected 200). Saving body to: $EXP_DIR/$base.error.txt" >&2
    cp "$tmp_body" "$EXP_DIR/$base.error.txt" || true
    rm -f "$tmp_body" "$tmp_err"
    continue
  fi

  if [[ ! -s "$tmp_body" ]]; then
    echo "ERROR: $base -> empty response body (HTTP 200). Saving marker to: $EXP_DIR/$base.error.txt" >&2
    printf "EMPTY_BODY\n" > "$EXP_DIR/$base.error.txt"
    rm -f "$tmp_body" "$tmp_err"
    continue
  fi

  if ! "$PY" - "$tmp_body" > "$EXP_DIR/$base.response.json" <<'PYCODE'
import sys, json, pathlib
p = pathlib.Path(sys.argv[1])
raw = p.read_text(encoding="utf-8", errors="replace").strip()
try:
    data = json.loads(raw)
except json.JSONDecodeError as e:
    sys.stderr.write(f"JSON decode failed: {e}\n")
    sys.stderr.write("First 200 chars of body:\n")
    sys.stderr.write(raw[:200] + "\n")
    raise
json.dump(data, sys.stdout, ensure_ascii=False, sort_keys=True, indent=2)
sys.stdout.write("\n")
PYCODE
  then
    echo "ERROR: $base -> response is not valid JSON. Saving raw body to: $EXP_DIR/$base.error.txt" >&2
    cp "$tmp_body" "$EXP_DIR/$base.error.txt" || true
    rm -f "$tmp_body" "$tmp_err"
    continue
  fi

  rm -f "$tmp_body" "$tmp_err"
done

echo "Done. Expected files in $EXP_DIR"
