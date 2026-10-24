#!/bin/sh
# Run each panel script once and dump results + env info to diagnose.txt.
DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$DIR/config" || exit 1
{
  echo "date: $(date)"; uname -a
  echo "curl: $(command -v curl)  wget: $(command -v wget)  jq: $(command -v jq)"
  echo "ldd:"; /lib/ld-linux-armhf.so.3 --list "$DIR/bin/desk-kindl" 2>&1
  for s in scripts/*.sh; do
    [ "$s" = scripts/common.sh ] && continue
    echo "== $s"; sh "$s" 2>&1 | head -n 10
  done
  echo "== log"; tail -n 30 "$DIR/desk-kindl.log" 2>&1
} > "$DIR/diagnose.txt" 2>&1
