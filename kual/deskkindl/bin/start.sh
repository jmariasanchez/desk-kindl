#!/bin/sh
# Launched by KUAL. Runs the dashboard on top of the Kindle framework.
DIR=$(cd "$(dirname "$0")/.." && pwd)
export DISPLAY=:0
# Stop the Kindle screensaver from covering the dashboard.
lipc-set-prop com.lab126.powerd preventScreenSaver 1
killall desk-kindl 2>/dev/null
cd "$DIR/config"
# KOReader libs provide curl-less TLS fallbacks if needed; GTK 2 is from firmware.
export LD_LIBRARY_PATH="/mnt/us/koreader/libs:$LD_LIBRARY_PATH"
nohup "$DIR/bin/desk-kindl" "$DIR/config/dashboard.conf" > "$DIR/desk-kindl.log" 2>&1 &
