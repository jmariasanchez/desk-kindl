#!/bin/sh
# Upcoming events from an iCal feed (today and next days), no dependencies.
. scripts/common.sh
need ICAL_URL

today=$(date +%Y%m%d)
fetch "$ICAL_URL" | tr -d '\r' | awk -v today="$today" -v max="${CAL_LIMIT:-6}" '
/^BEGIN:VEVENT/ { s=""; d=""; t="" }
/^SUMMARY/      { sub(/^[^:]*:/, ""); s=$0 }
/^DTSTART/      { v=$0; sub(/^[^:]*:/, "", v); d=substr(v,1,8); t=(length(v)>8)?substr(v,10,2)":"substr(v,12,2):"all day" }
/^END:VEVENT/   { if (d >= today) print d " " t "\t" s }
' | sort | head -n "${CAL_LIMIT:-6}" | awk -F'\t' -v today="$today" '
{ split($1, a, " "); day = (a[1]==today) ? "Today" : substr(a[1],7,2) "/" substr(a[1],5,2);
  printf "%-6s %-7s %s\n", day, a[2], $2; n++ }
END { if (!n) print "No upcoming events" }'
