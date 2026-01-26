#!/usr/bin/env bash
set -euo pipefail

# Optional: server auto-start (only if BIN is set)
if [[ -n "${BIN:-}" ]]; then
  PORT="${PORT:-8080}"
  API_URL="${API_URL:-http://localhost:$PORT}"

  "$BIN" &
  SERVER_PID=$!
  trap 'kill $SERVER_PID 2>/dev/null || true' EXIT

  # wait for health
  for i in {1..30}; do
    if curl -sS "$API_URL/health" | jq -e '.status=="ok"' >/dev/null 2>&1; then
      break
    fi
    sleep 0.2
  done

  # if still not healthy, fail
  curl -sS "$API_URL/health" | jq -e '.status=="ok"' >/dev/null \
    || { echo "API not healthy at $API_URL"; exit 1; }
fi

API_URL="${API_URL:-http://localhost:8080}"
REQ_DIR="${REQ_DIR:-tests/api/requests}"

echo "API: $API_URL"
echo "Requests: $REQ_DIR"

# Always verify API is reachable even if we didn't start it
curl -sS "$API_URL/health" | jq -e '.status=="ok"' >/dev/null \
  || { echo "API not healthy at $API_URL"; exit 1; }

for f in "$REQ_DIR"/*.json; do
  echo "==> Testing $f"
  resp="$(curl -sS -X POST "$API_URL/analyze" \
    -H "Content-Type: application/json" \
    --data-binary @"$f")"

  echo "$resp" | jq -e '.meta.pagesReceived >= 1' >/dev/null
  echo "$resp" | jq -e '.meta.wordCount >= 1' >/dev/null
  echo "$resp" | jq -e '.meta.wordCharCount >= 1' >/dev/null
  echo "$resp" | jq -e '.domainResult.words | type=="array"' >/dev/null
  echo "$resp" | jq -e '.pageResults | type=="array"' >/dev/null
  echo "$resp" | jq -e '.pageResults[0].name | type=="string"' >/dev/null

  echo "OK: $f"
done

echo "All API tests passed."