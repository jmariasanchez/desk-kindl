#!/bin/sh
# Home server health: ping hosts, HTTP-check URLs.
. scripts/common.sh

for h in $SERVER_HOSTS; do
    if ping -c 1 -W 2 "$h" >/dev/null 2>&1; then echo "UP   $h"; else echo "DOWN $h"; fi
done
for u in $SERVER_URLS; do
    if fetch "$u" >/dev/null 2>&1; then echo "OK   $u"; else echo "FAIL $u"; fi
done
[ -z "$SERVER_HOSTS$SERVER_URLS" ] && echo "set SERVER_HOSTS / SERVER_URLS in config/env"
exit 0
