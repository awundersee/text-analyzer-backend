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

for f in "$REQ_DIR"/*.json; do
  base="$(basename "$f" .json)"
  echo "Generating expected for $base ..."

  curl -sS -X POST "$API_URL/analyze" \
    -H "Content-Type: application/json" \
    --data-binary @"$f" \
  | "$PY" - <<'PYCODE' > "$EXP_DIR/$base.response.json"
import sys, json
data = json.load(sys.stdin)
json.dump(data, sys.stdout, ensure_ascii=False, sort_keys=True, indent=2)
print()
PYCODE
done

echo "Done. Expected files in $EXP_DIR"
