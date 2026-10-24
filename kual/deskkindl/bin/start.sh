#!/bin/sh
# Launched by KUAL. Runs in the foreground like pomo-kndl: a backgrounded
# GUI can get killed when KUAL's shell exits.
DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$DIR/desk-kindl.log"
export DISPLAY="${DISPLAY:-:0}"
echo "--- $(date) start.sh (DISPLAY=$DISPLAY uid=$(id -u))" >>"$LOG"
killall desk-kindl 2>/dev/null
lipc-set-prop com.lab126.powerd preventScreenSaver 1 >>"$LOG" 2>&1
cd "$DIR/config" || { echo "no config dir" >>"$LOG"; exit 1; }
"$DIR/bin/desk-kindl" "$DIR/config/dashboard.conf" >>"$LOG" 2>&1
echo "exit $?" >>"$LOG"
lipc-set-prop com.lab126.powerd preventScreenSaver 0
