#!/usr/bin/env bash
# Cross-build desk-kindl for Kindle (armhf, firmware 5.16.3+) with zig. No Docker.
# Reuses the Debian armhf GTK 2 header sysroot (same one as pomo-kndl/kindle/cross.sh).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SYS="${DESKKINDL_SYSROOT:-$HOME/.cache/pomo-kndl/sysroot}"
[ -f "$SYS/.done" ] || { echo "sysroot missing at $SYS (run pomo-kndl/kindle/cross.sh once)"; exit 1; }
command -v zig >/dev/null || { echo "zig is required"; exit 1; }

R="$SYS/root"; L="$R/usr/lib/arm-linux-gnueabihf"
INC=""
for d in usr/include/gtk-2.0 usr/lib/arm-linux-gnueabihf/gtk-2.0/include usr/include/gdk-pixbuf-2.0 \
         usr/include/pango-1.0 usr/include/cairo usr/include/atk-1.0 usr/include/glib-2.0 \
         usr/lib/arm-linux-gnueabihf/glib-2.0/include usr/include/harfbuzz usr/include/freetype2 \
         usr/include/pixman-1 usr/include/libpng16 usr/include usr/include/arm-linux-gnueabihf; do
  INC="$INC -I$R/$d"
done
mkdir -p "$HERE/build-kindle"
zig c++ -target arm-linux-gnueabihf.2.17 -mcpu=cortex_a7 -std=c++11 -O2 -s \
  -o "$HERE/build-kindle/desk-kindl" "$HERE/src/main.cpp" $INC \
  -L"$L" -L"$R/lib/arm-linux-gnueabihf" -Wl,--as-needed -Wl,--allow-shlib-undefined \
  -lgtk-x11-2.0 -lgdk-x11-2.0 -lpango-1.0 -lgobject-2.0 \
  "$R/lib/arm-linux-gnueabihf/libglib-2.0.so.0"  # .so symlink is absolute, avoid static .a

D="$HERE/dist/deskkindl"
rm -rf "$HERE/dist" && mkdir -p "$D/bin"
cp "$HERE"/kual/deskkindl/config.xml "$HERE"/kual/deskkindl/menu.json "$D/"
cp "$HERE"/kual/deskkindl/bin/*.sh "$D/bin/"
cp "$HERE/build-kindle/desk-kindl" "$D/bin/"
cp -r "$HERE/config" "$D/config"
chmod +x "$D"/bin/* "$D"/config/scripts/*.sh
file "$D/bin/desk-kindl"
echo "built: $D"
