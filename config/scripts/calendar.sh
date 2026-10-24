#!/bin/sh
# Download the private Google iCal feed; render the cached agenda every minute.
. scripts/common.sh
if [ -z "$ICAL_URL" ]; then
    printf '#markup\n<big>Your day, at a glance</big>\nAdd your Google Calendar iCal link to get started.\n'
    exit 0
fi
case "$ICAL_URL" in
    https://*) ;;
    *) echo "Calendar needs an HTTPS iCal link"; exit 1 ;;
esac
if [ ! -x "$DESKKINDL_BIN" ]; then
    echo "Calendar reader unavailable"
    exit 1
fi
umask 077
mkdir -p .calendar-cache || exit 1
key=$(printf '%s' "$ICAL_URL" | cksum | awk '{print $1 "-" $2}')
cache=".calendar-cache/$key.ics"
now=$(date +%s)
last=0
if [ -f "$cache" ]; then
    last=$(stat -c %Y "$cache" 2>/dev/null || stat -f %m "$cache")
fi
status=""
# Remember the last attempt so a failing feed is retried every 15 min, not every minute.
tried=0
[ -f "$cache.tried" ] && tried=$(cat "$cache.tried" 2>/dev/null)
case "$tried" in ''|*[!0-9]*) tried=0 ;; esac
[ "$now" -lt "$tried" ] && tried=0
due=0
if [ "$last" -eq 0 ] || [ $((now - last)) -ge 900 ] || [ "$now" -lt "$last" ]; then
    due=1
    # Retry a first-ever download sooner: there is nothing to show yet.
    wait=900; [ "$last" -eq 0 ] && wait=120
    [ "$tried" -gt 0 ] && [ $((now - tried)) -lt "$wait" ] && due=2
fi
if [ "$due" -eq 2 ]; then
    status="Sync unavailable · "
elif [ "$due" -eq 1 ]; then
    echo "$now" > "$cache.tried"
    # Fixed staging file: one panel process runs at a time, and rename preserves the last good copy.
    pending="$cache.pending"
    if fetch "$ICAL_URL" > "$pending" 2>/dev/null &&
       "$DESKKINDL_BIN" --calendar "$pending" "$DESKKINDL_TIMEZONE" >/dev/null 2>&1 &&
       mv "$pending" "$cache"; then
        last=$now
    else
        status="Sync unavailable · "
    fi
fi
if [ "$last" -eq 0 ]; then
    echo "Calendar unavailable — check your connection and private iCal link."
    exit 1
fi
if ! agenda=$("$DESKKINDL_BIN" --calendar "$cache" "$DESKKINDL_TIMEZONE" 2>/dev/null); then
    echo "Saved calendar could not be read. Check the calendar and timezone."
    exit 1
fi
age=$(( (now - last) / 60 ))
[ "$age" -ge 0 ] || age=0
printf '%s\n<small>%sSynced %s min ago</small>\n' "$agenda" "$status" "$age"
