#!/bin/sh
# Usage: orientation.sh portrait|landscape. Saves the choice and restarts.
DIR="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$DIR/config/dashboard.conf"
case "$1" in portrait|landscape) ;; *) exit 1 ;; esac
if grep -q '^orientation' "$CONF"; then
    sed -i "s/^orientation *=.*/orientation = $1/" "$CONF"
else
    sed -i "1a orientation = $1" "$CONF"
fi
lipc-set-prop com.lab126.winmgr orientationLock "$([ "$1" = landscape ] && echo R || echo U)"
exec "$DIR/bin/start.sh"
