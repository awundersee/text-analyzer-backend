#!/usr/bin/env bash
set -euo pipefail

# --- python detection ---
PY=""
if command -v python3 >/dev/null 2>&1; then
  PY="python3"
elif command -v python >/dev/null 2>&1; then
  PY="python"
else
  echo "ERROR: python is required (no jq dependency)." >&2
  exit 90
fi

API_URL="${API_URL:-http://localhost:8080}"
REQ_DIR="${REQ_DIR:-tests/api/requests}"

echo "API: $API_URL"
echo "Requests: $REQ_DIR"

# globals (wegen set -u)
CURL_HTTP_CODE=""
CURL_RC=0
CURL_ERR=""
CURL_BODY=""

curl_json() {
  local url="$1"
  local tmp err http rc

  tmp="$(mktemp)"
  err="$(mktemp)"

  set +e
  http="$(curl -sS --max-time 5 -o "$tmp" -w "%{http_code}" "$url" 2>"$err")"
  rc=$?
  set -e

  CURL_RC=$rc
  CURL_ERR="$(cat "$err" 2>/dev/null || true)"
  rm -f "$err"

  if [[ -z "${http:-}" ]]; then
    http="000"
  fi
  CURL_HTTP_CODE="$http"

  CURL_BODY="$(cat "$tmp" 2>/dev/null || true)"
  rm -f "$tmp"
}

health_ok() {
  local http

  curl_json "$API_URL/health"
  http="${CURL_HTTP_CODE:-000}"

  if [[ "$http" != "200" || -z "${CURL_BODY:-}" ]]; then
    return 1
  fi

  printf "%s" "$CURL_BODY" | "$PY" -c 'import sys, json; j=json.load(sys.stdin); assert j.get("status")=="ok"' >/dev/null 2>&1
}

# Wait for health
for i in {1..30}; do
  if health_ok; then
    break
  fi
  sleep 0.2
done

if ! health_ok; then
  curl_json "$API_URL/health"
  http="${CURL_HTTP_CODE:-000}"

  echo "API not healthy at $API_URL"
  echo "Health HTTP: $http"
  echo "curl rc: ${CURL_RC:-?}"
  echo "curl err:"
  printf "%s\n" "${CURL_ERR:-<none>}"

  echo "Health body (first 300 chars):"
  printf "%s\n" "${CURL_BODY:0:300}"
  exit 1
fi

for f in "$REQ_DIR"/*.json; do
  echo "==> Testing $f"

  resp="$(curl -sS --max-time 30 -X POST "$API_URL/analyze"     -H "Content-Type: application/json"     --data-binary @"$f")"

  printf "%s" "$resp" | "$PY" -c '
import sys, json
j = json.load(sys.stdin)

assert j["meta"]["pagesReceived"] >= 1
assert j["meta"]["wordCount"] >= 1
assert j["meta"]["wordCharCount"] >= 1

assert isinstance(j["domainResult"]["words"], list)
assert isinstance(j["pageResults"], list)
assert isinstance(j["pageResults"][0]["name"], str)
'

  echo "OK: $f"
done

echo "All API tests passed."
