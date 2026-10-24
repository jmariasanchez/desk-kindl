#!/usr/bin/env bash
# Cross-build desk-kindl for Kindle (armhf, firmware 5.16.3+).
# Toolchain lessons from pomo-kndl (same device):
#  - zig's crt/libc++ assume a newer glibc and segfault at startup on the Kindle's glibc 2.20,
#    so use clang + lld with GCC 6 crt files and a static libstdc++ (Debian 9 sysroot, glibc 2.19).
#  - Build against GLib 2.32 / GTK 2.24.10 headers (Debian 7): newer GLib headers inline
#    g_once_init_* paths the firmware's GLib lacks.
# The sysroot is the one pomo-kndl/kindle/cross.sh fetches; run that once if it is missing.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="${DESKKINDL_SYSROOT:-$HOME/.cache/pomo-kndl/sysroot}"
for m in .done-v2 .done-gtk; do
  [ -f "$CACHE/$m" ] || { echo "sysroot incomplete at $CACHE (run pomo-kndl/kindle/cross.sh once)"; exit 1; }
done
CLANGXX="${CLANGXX:-$(nix build --no-link --print-out-paths nixpkgs#llvmPackages_21.clang-unwrapped)/bin/clang++}"
LLD_DIR="${LLD_DIR:-$(nix build --no-link --print-out-paths nixpkgs#llvmPackages_21.lld)/bin}"

R="$CACHE/root"; G="$CACHE/gtkroot"
INC=""
for d in usr/include/gtk-2.0 usr/lib/arm-linux-gnueabihf/gtk-2.0/include usr/include/gdk-pixbuf-2.0 \
         usr/include/pango-1.0 usr/include/cairo usr/include/atk-1.0 usr/include/glib-2.0 \
         usr/lib/arm-linux-gnueabihf/glib-2.0/include usr/include/freetype2 \
         usr/include/pixman-1 usr/include/libpng12; do
  INC="$INC -isystem $G/$d"
done
FLAGS="--target=arm-linux-gnueabihf --sysroot=$R --gcc-toolchain=$R/usr -mcpu=cortex-a7 -mfpu=neon-vfpv4
 -mfloat-abi=hard -fuse-ld=lld -B$LLD_DIR --rtlib=libgcc --unwindlib=libgcc --stdlib=libstdc++ -no-pie
 -std=c++11 -O2 -s -pthread -static-libstdc++ -Wl,-dynamic-linker,/lib/ld-linux-armhf.so.3
 -Wl,--as-needed -Wl,--allow-shlib-undefined"
LIBS="-L$R/usr/lib/arm-linux-gnueabihf -L$R/lib/arm-linux-gnueabihf
 -lgtk-x11-2.0 -lgdk-x11-2.0 -lpango-1.0 -lgobject-2.0 -lpthread
 $R/lib/arm-linux-gnueabihf/libglib-2.0.so.0"
# ^ libglib-2.0.so in the sysroot is an absolute symlink, so -lglib-2.0 would pick the static .a
#   and put a second GLib in the process next to the firmware's one.

mkdir -p "$HERE/build-kindle"
PATH="$LLD_DIR:$PATH" "$CLANGXX" $FLAGS -o "$HERE/build-kindle/desk-kindl" "$HERE/src/main.cpp" $INC $LIBS

D="$HERE/dist/deskkindl"
rm -rf "$HERE/dist" && mkdir -p "$D/bin"
cp "$HERE"/kual/deskkindl/config.xml "$HERE"/kual/deskkindl/menu.json "$D/"
cp "$HERE"/kual/deskkindl/bin/*.sh "$D/bin/"
cp "$HERE/build-kindle/desk-kindl" "$D/bin/"
cp -r "$HERE/config" "$D/config"
chmod +x "$D"/bin/* "$D"/config/scripts/*.sh
file "$D/bin/desk-kindl"
echo "built: $D"
