#!/usr/bin/env bash
set -euo pipefail

SUITE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${SUITE_ROOT}/data"
RESULTS_DIR="${SUITE_ROOT}/results"
MANIFEST="${SUITE_ROOT}/manifest.json"

OUT_CSV="${RESULTS_DIR}/results_cli_api.csv"
SUMMARY_TXT="${RESULTS_DIR}/summary.txt"
mkdir -p "${RESULTS_DIR}"

# Python detection
if command -v python3 >/dev/null 2>&1; then PY=python3
elif command -v python >/dev/null 2>&1; then PY=python
else
  echo "ERROR: python3/python required (manifest parsing + JSON snippets)" >&2
  exit 2
fi

# Find project root by walking upwards from suite root
find_project_root() {
  local d="$1"
  for _ in 1 2 3 4 5 6; do
    if [[ -f "$d/CMakeLists.txt" && -d "$d/src" ]]; then
      echo "$d"; return 0
    fi
    d="$(cd "$d/.." && pwd)"
  done
  echo ""
}

PROJECT_ROOT="$(find_project_root "$SUITE_ROOT")"
if [[ -z "$PROJECT_ROOT" ]]; then
  PROJECT_ROOT="$(cd "$SUITE_ROOT/../.." && pwd)"
fi

CLI_BIN="${CLI_BIN:-}"
BUILD_DIR="${BUILD_DIR:-}"

# honor cmake env if passed
if [[ -z "$BUILD_DIR" && -n "${CMAKE_BINARY_DIR:-}" ]]; then
  BUILD_DIR="${CMAKE_BINARY_DIR}"
fi

if [[ -z "$BUILD_DIR" ]]; then
  for cand in "$PROJECT_ROOT/build" "$PROJECT_ROOT/build-debug" "$PROJECT_ROOT/build-release" "$PROJECT_ROOT/out/build"; do
    if [[ -d "$cand" ]]; then BUILD_DIR="$cand"; break; fi
  done
fi

if [[ -z "$CLI_BIN" ]]; then
  for cand in     "$BUILD_DIR/textanalyse"     "$BUILD_DIR/text-analyse"     "$BUILD_DIR/text-analyze"     "$BUILD_DIR/cli"     "$BUILD_DIR/main"     ; do
    if [[ -n "$cand" && -x "$cand" ]]; then CLI_BIN="$cand"; break; fi
  done
fi

API_URL="${API_URL:-http://127.0.0.1:8080/analyze}"

CLI_MISSING=0
if [[ -z "$CLI_BIN" || ! -x "$CLI_BIN" ]]; then
  echo "ERROR: CLI_BIN not set/found. Set CLI_BIN or BUILD_DIR/CMAKE_BINARY_DIR." >&2
  echo "  SUITE_ROOT=$SUITE_ROOT" >&2
  echo "  PROJECT_ROOT=$PROJECT_ROOT" >&2
  echo "  BUILD_DIR=${BUILD_DIR:-<unset>}" >&2
  CLI_MISSING=1
fi

echo "Suite root : $SUITE_ROOT" >&2
echo "Project root: $PROJECT_ROOT" >&2
echo "Build dir  : ${BUILD_DIR:-<unset>}" >&2
echo "CLI bin    : ${CLI_BIN:-<unset>}" >&2
echo "API URL    : ${API_URL}" >&2

echo "file,category,expect_cli,cli_exit,cli_pass,expect_api,api_http,api_pass,note,cli_snip,api_snip" > "${OUT_CSV}"

"$PY" - <<'PY' "$MANIFEST" "$OUT_CSV" "$CLI_BIN" "$API_URL" "$DATA_DIR"
import json, os, subprocess, sys, tempfile

manifest_path, out_csv, cli_bin, api_url, data_dir = sys.argv[1:6]
cases = json.load(open(manifest_path, "r", encoding="utf-8"))

def snip(s, n=260):
    s = (s or "").replace("\r", " ").replace("\n", " ").replace(",", ";")
    return s[:n]

def run_cli(fp):
    if not cli_bin or not os.path.exists(cli_bin):
        return ("NA", "CLI_BIN not set/found")
    try:
        p = subprocess.run([cli_bin, fp], capture_output=True, text=True)
        out = (p.stdout or "") + "\n" + (p.stderr or "")
        return (str(p.returncode), snip(out))
    except Exception as e:
        return ("NA", f"exec error: {e}")

def run_api(fp):
    if not api_url:
        return ("NA", "API_URL not set")
    body_path = None
    try:
        tf = tempfile.NamedTemporaryFile(delete=False); body_path=tf.name; tf.close()
        cmd = ["curl","--http1.1","-sS","-o",body_path,"-w","%{http_code}",
               "-H","Content-Type: application/json",
               "--data-binary", f"@{fp}", api_url]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            err = (p.stderr or "").strip()
            return ("000", snip("curl_error: " + err))
        http = (p.stdout or "").strip() or "NA"
        body = open(body_path, "rb").read().decode("utf-8", "replace")
        msg = body
        try:
            j = json.loads(body)
            if isinstance(j, dict):
                for k in ("message","error","detail","errors"):
                    if k in j:
                        v = j[k]
                        msg = v if isinstance(v, str) else json.dumps(v, ensure_ascii=False)
                        break
        except Exception:
            pass
        return (http, snip(msg))
    finally:
        if body_path:
            try: os.unlink(body_path)
            except: pass

rows=[]
cli_pass_n=api_pass_n=both_pass=0
mismatches=0
cli_na=api_na=0

for c in cases:
    fn=c["file"]; fp=os.path.join(data_dir, fn)
    exp_cli=str(c["expect_cli_exit"]); exp_api=str(c["expect_api_http"])
    cat=c.get("category",""); note=c.get("note","")

    cli_exit, cli_sn = run_cli(fp)
    api_http, api_sn = run_api(fp)

    if cli_exit=="NA": cli_na+=1
    if api_http=="NA": api_na+=1

    cli_pass = (cli_exit==exp_cli) if cli_exit!="NA" else False
    api_pass = (api_http==exp_api) if api_http!="NA" else False

    if cli_exit!="NA" and cli_pass: cli_pass_n+=1
    if api_http!="NA" and api_pass: api_pass_n+=1
    if cli_exit!="NA" and api_http!="NA" and cli_pass and api_pass: both_pass+=1
    if (cli_exit!="NA" and not cli_pass) or (api_http!="NA" and not api_pass):
        mismatches += 1

    rows.append([fn,cat,exp_cli,cli_exit,"true" if cli_pass else "false",
                 exp_api,api_http,"true" if api_pass else "false",
                 note,cli_sn,api_sn])

with open(out_csv, "a", encoding="utf-8", newline="\n") as f:
    for r in rows:
        esc=[]
        for x in r:
            x="" if x is None else str(x)
            if any(ch in x for ch in ['"',',','\n','\r']):
                x='"'+x.replace('"','""')+'"'
            esc.append(x)
        f.write(",".join(esc) + "\n")

summary=f"""Total cases: {len(cases)}
CLI pass: {cli_pass_n} (NA: {cli_na})
API pass: {api_pass_n} (NA: {api_na})
Both pass: {both_pass}
Mismatches (investigate / gaps): {mismatches}
"""
open(os.path.join(os.path.dirname(out_csv), "summary.txt"), "w", encoding="utf-8").write(summary)
print(summary)
PY

echo "Wrote:"
echo "  ${OUT_CSV}"
echo "  ${SUMMARY_TXT}"

if [[ $CLI_MISSING -eq 1 ]]; then
  exit 1
fi
