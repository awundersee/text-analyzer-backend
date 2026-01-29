#!/usr/bin/env bash
set -euo pipefail

# Common helpers for CLI stress tests (no jq)

PY=""
if command -v python3 >/dev/null 2>&1; then PY="python3"
elif command -v python >/dev/null 2>&1; then PY="python"
else echo "ERROR: Need python3 or python." >&2; exit 90; fi

TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_CMD="gtimeout"
else TIMEOUT_CMD=""; fi

csv_escape() { printf '%s' "$1" | sed 's/"/""/g'; }

run_with_timeout() {
  local seconds="$1"; shift
  if [ -n "$TIMEOUT_CMD" ]; then "$TIMEOUT_CMD" "${seconds}s" "$@"; return $?; fi
  "$PY" -c 'import subprocess,sys,time
timeout_s=float(sys.argv[1]); cmd=sys.argv[2:]
p=subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
start=time.time()
while True:
    if p.poll() is not None: break
    if time.time()-start>timeout_s:
        try: p.kill()
        except Exception: pass
        sys.stdout.write("<<timeout>>\n"); sys.exit(124)
    time.sleep(0.05)
out=p.stdout.read() if p.stdout else ""
sys.stdout.write(out)
sys.exit(p.returncode if p.returncode is not None else 0)
' "$seconds" "$@"
}

json_input_estimates() {
  "$PY" -c 'import json,re,sys
data=json.load(open(sys.argv[1],"r",encoding="utf-8"))
pages = data if isinstance(data,list) else (data.get("pages",[]) if isinstance(data,dict) else [])
token_re=re.compile(r"[^\W_]+", flags=re.UNICODE)
words=0; chars=0
for p in pages:
    txt=(p.get("text") if isinstance(p,dict) else "") or ""
    chars += len(txt)
    words += len(token_re.findall(txt))
print(f"{len(pages)},{words},{chars}")
' "$1"
}

# Reads CLI output from stdin and prints (tab-separated):
# runtime_ms_analyze, runtime_ms_total, peak_kib_total, pages_received, pipeline_used,
# words_exact, chars_exact, word_chars_exact, counts_source
extract_from_output() {
  "$PY" -c 'import sys,re
out=sys.stdin.read()
out=out.replace("\r\n","\n").replace("\r","\n")

def find_kv_num(name):
    m=re.search(rf"{re.escape(name)}\s*(?:=|:)\s*([-]?\d+(?:\.\d+)?)", out)
    return m.group(1) if m else None

def find_kv_int(name):
    m=re.search(rf"{re.escape(name)}\s*(?:=|:)\s*([-]?\d+)", out)
    return m.group(1) if m else None

def find_kv_str(name):
    # reads until newline; trims spaces
    m=re.search(rf"{re.escape(name)}\s*(?:=|:)\s*([^\r\n]+)", out)
    return m.group(1).strip() if m else None

# 1) key=value mode (CLI default)
runtime_t=find_kv_num("runtime_ms_total")
runtime_a=find_kv_num("runtime_ms_analyze")
peak=find_kv_num("peak_rss_kib") or find_kv_num("peak_rss_kib_total")

# new: extra kvs
pages=find_kv_int("pages_received")
pipe=find_kv_str("pipeline_used")
words=find_kv_int("word_count")
chars=find_kv_int("char_count")
wchars=find_kv_int("word_char_count")

if any(v is not None for v in [runtime_a, runtime_t, peak, pages, pipe, words, chars, wchars]):
    def na(x): return x if (x is not None and x != "") else "NA"
    print(na(runtime_a), na(runtime_t), na(peak), na(pages), na(pipe),
          na(words), na(chars), na(wchars), "exact_kv", sep="\t")
    sys.exit(0)

# 2) JSON-schema regex (falls JSON auf stdout)
def rx_num(key):
    m=re.search(rf"\"{re.escape(key)}\"\s*:\s*([-]?\d+(?:\.\d+)?)", out); return m.group(1) if m else None
def rx_int(key):
    m=re.search(rf"\"{re.escape(key)}\"\s*:\s*([-]?\d+)", out); return m.group(1) if m else None
def rx_str(key):
    m=re.search(rf"\"{re.escape(key)}\"\s*:\s*\"([^\"]+)\"", out); return m.group(1) if m else None

runtime_a=rx_num("runtimeMsAnalyze"); runtime_t=rx_num("runtimeMsTotal"); peak=rx_int("peakRssKiB")
pages=rx_int("pagesReceived"); pipe=rx_str("pipelineUsed")
words=rx_int("wordCount"); chars=rx_int("charCount"); wchars=rx_int("wordCharCount")

if any(v is not None for v in [runtime_a, runtime_t, peak, pages, pipe, words, chars, wchars]):
    def na(x): return x if x is not None else "NA"
    print(na(runtime_a), na(runtime_t), na(peak), na(pages), na(pipe),
          na(words), na(chars), na(wchars), "exact_regex", sep="\t")
    sys.exit(0)

print("NA","NA","NA","NA","NA","NA","NA","NA","none", sep="\t")
'
}
