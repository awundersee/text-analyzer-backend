#!/usr/bin/env bash
set -euo pipefail

API_URL="${API_URL:-http://localhost:8080}"
REQ_DIR="tests/api/requests"
EXP_DIR="tests/api/expected"

mkdir -p "$EXP_DIR"

for f in "$REQ_DIR"/*.json; do
  base="$(basename "$f" .json)"
  echo "Generating expected for $base ..."

  curl -sS -X POST "$API_URL/analyze" \
    -H "Content-Type: application/json" \
    --data-binary @"$f" \
    | jq -S . > "$EXP_DIR/$base.response.json"
done

echo "Done. Expected files in $EXP_DIR"
