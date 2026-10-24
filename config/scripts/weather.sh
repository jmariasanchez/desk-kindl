#!/bin/sh
# Weather via wttr.in (no API key). Format codes: https://github.com/chubin/wttr.in
. scripts/common.sh
loc=$(echo "${WEATHER_LOCATION:-}" | sed 's/ /+/g')
fetch "https://wttr.in/${loc}?format=%l\n%C\n%t+(feels+%f)\nWind+%w\nHumidity+%h\nRain+%p&m" \
    || echo "weather unavailable"
