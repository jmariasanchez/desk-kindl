#!/bin/sh
# Departures. TRANSIT_URL may return plain text (printed as is) or
# transport.rest JSON, e.g.
#   https://v6.db.transport.rest/stops/8011160/departures?duration=60&results=6
. scripts/common.sh
need TRANSIT_URL

data=$(fetch "$TRANSIT_URL") || { echo "transit unavailable"; exit 0; }
case "$data" in
    \{*|\[*) ;;
    *) echo "$data" | head -n "${TRANSIT_LIMIT:-6}"; exit 0 ;;
esac

if command -v jq >/dev/null 2>&1; then
    echo "$data" | jq -r '(.departures // .)[] |
        "\((.when // .plannedWhen)[11:16])  \(.line.name)  \(.direction)" +
        (if (.delay // 0) > 0 then "  +\(.delay/60|floor)" else "" end)' \
        | head -n "${TRANSIT_LIMIT:-6}"
else
    # jq-less fallback: pair up "when" times with "direction" names.
    echo "$data" | tr ',' '\n' | awk -F'"' '
        /"when":"/      { w = substr($4, 12, 5) }
        /"direction":"/ { print w "  " $4 }' | head -n "${TRANSIT_LIMIT:-6}"
fi
