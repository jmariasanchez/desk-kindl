#!/bin/sh
# Build only the static iCalendar parser; the Kindle needs no installed library.
set -eu
out=$1
shift
mkdir -p "$out"
out=$(cd "$out" && pwd)
src="$out/libical-3.0.20"
archive="$out/libical-3.0.20.tar.gz"
if [ ! -f "$src/CMakeLists.txt" ]; then
    curl -fLSs https://github.com/libical/libical/releases/download/v3.0.20/libical-3.0.20.tar.gz -o "$archive"
    printf '%s  %s\n' e73de92f5a6ce84c1b00306446b290a2b08cdf0a80988eca0a2c9d5c3510b4c2 "$archive" | shasum -a 256 -c -
    tar -xzf "$archive" -C "$out"
fi
cmake -S "$src" -B "$out/ical" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE=Release -DSTATIC_ONLY=ON -DWITH_CXX_BINDINGS=OFF \
    -DICAL_GLIB=OFF -DGOBJECT_INTROSPECTION=OFF -DICAL_BUILD_DOCS=OFF \
    -DLIBICAL_BUILD_TESTING=OFF -DLIBICAL_BUILD_EXAMPLES=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON -DCMAKE_DISABLE_FIND_PACKAGE_BerkeleyDB=ON \
    -DUSE_BUILTIN_TZDATA=ON "$@"
cmake --build "$out/ical" --target ical --parallel 4
