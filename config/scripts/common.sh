# Shared helpers. Scripts must be POSIX sh: Kindle has busybox, not bash.
[ -f ./env ] && . ./env

# fetch URL [extra curl args...] -> stdout, falls back to wget (busybox)
fetch() {
    url="$1"; shift
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --max-time 20 "$@" "$url"
    else
        wget -q -T 20 -O - "$url"
    fi
}

# Crude JSON string extraction without jq: json_field name < json
json_field() {
    sed -n "s/.*\"$1\" *: *\"\([^\"]*\)\".*/\1/p"
}

need() {
    eval "v=\$$1"
    if [ -z "$v" ]; then echo "set $1 in config/env"; exit 0; fi
}
