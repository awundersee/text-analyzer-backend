#!/usr/bin/env zsh
set -e

bytes(){ stat -c%s "$1" 2>/dev/null || stat -f%z "$1"; }
TARGET=$((500*1024*1024))
MAX_PAGES_STEPS=12   # 10 Seiten Basis -> P13 = 40960 Seiten (genug Verlauf, aber noch handhabbar)

for base in *_BASE_*.json; do
  name="${base%.json}"

  # P1 = Basis kopieren
  cp "$base" "${name}_P1.json"
  p=1
  echo "[$base] P1 size=$(bytes "${name}_P1.json") bytes"

  # Pages-Stufen erzeugen
  while [ "$(bytes "${name}_P${p}.json")" -lt "$TARGET" ] && [ $p -lt $MAX_PAGES_STEPS ]; do
    prev="${name}_P${p}.json"
    p=$((p+1))
    next="${name}_P${p}.json"

    jq '.pages |= (. + (map(.id = (.id + (length)))))' "$prev" > "${next}.tmp"
    mv "${next}.tmp" "$next"
    echo "[$base] P${p} size=$(bytes "$next") bytes"
  done

  # Falls noch nicht >=500MB: ab letzter P-Stufe Text-Stufen erzeugen (T2..)
  if [ "$(bytes "${name}_P${p}.json")" -lt "$TARGET" ]; then
    cp "${name}_P${p}.json" "${name}_P${p}_T1.json"
    t=1
    echo "[$base] P${p}_T1 size=$(bytes "${name}_P${p}_T1.json") bytes"

    while [ "$(bytes "${name}_P${p}_T${t}.json")" -lt "$TARGET" ]; do
      prev="${name}_P${p}_T${t}.json"
      t=$((t+1))
      next="${name}_P${p}_T${t}.json"

      jq '.pages |= map(.text = (.text + " " + .text))' "$prev" > "${next}.tmp"
      mv "${next}.tmp" "$next"
      echo "[$base] P${p}_T${t} size=$(bytes "$next") bytes"
    done
  fi
done
