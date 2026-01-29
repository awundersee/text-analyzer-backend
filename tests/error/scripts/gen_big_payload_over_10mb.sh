#!/usr/bin/env bash
set -euo pipefail
SIZE_MB="${1:-11}"
python3 - <<PY "$SIZE_MB"
import json, sys
mb=int(sys.argv[1])
txt="a"*(mb*1024*1024)
obj={"pages":[{"id":1,"name":"Big","url":"https://example.test/big","text":txt}],
     "options":{"includeBigrams":True,"perPageResults":False}}
print(json.dumps(obj, ensure_ascii=False))
PY
