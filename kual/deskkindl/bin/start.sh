#!/bin/sh
# Launched by KUAL. Runs the dashboard on top of the Kindle framework.
DIR=$(cd "$(dirname "$0")/.." && pwd)
export DISPLAY=:0
# Stop the Kindle screensaver from covering the dashboard.
lipc-set-prop com.lab126.powerd preventScreenSaver 1
killall desk-kindl 2>/dev/null
cd "$DIR/config"
nohup "$DIR/bin/desk-kindl" "$DIR/config/dashboard.conf" > /tmp/desk-kindl.log 2>&1 &
