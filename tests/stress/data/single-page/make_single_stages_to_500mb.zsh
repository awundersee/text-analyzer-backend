#!/usr/bin/env zsh
set -e

bytes(){ stat -c%s "$1" 2>/dev/null || stat -f%z "$1"; }
TARGET=$((500*1024*1024))

for base in *_BASE_*.json; do
  name="${base%.json}"

  # T1 = Basis kopieren
  cp "$base" "${name}_T1.json"
  i=1
  echo "[$base] T1 size=$(bytes "${name}_T1.json") bytes"

  # Weitere Stufen: Text verdoppeln, bis Ziel erreicht
  while [ "$(bytes "${name}_T${i}.json")" -lt "$TARGET" ]; do
    prev="${name}_T${i}.json"
    i=$((i+1))
    next="${name}_T${i}.json"

    jq '.pages |= map(.text = (.text + " " + .text))' "$prev" > "${next}.tmp"
    mv "${next}.tmp" "$next"
    echo "[$base] T${i} size=$(bytes "$next") bytes"
  done
done
