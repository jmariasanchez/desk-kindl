# Native build:   make
# Kindle build:   make kindle CROSS=arm-kindlepw2-linux-gnueabi-   (koxtoolchain)
#                 SYSROOT must contain GTK 2 headers/libs from the device.

CROSS   ?=
CXX     := $(CROSS)g++
PKG     ?= pkg-config
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++11
GTK_CFLAGS := $(shell $(PKG) --cflags gtk+-2.0)
GTK_LIBS   := $(shell $(PKG) --libs gtk+-2.0)

BIN := build/desk-kindl

all: $(BIN)

$(BIN): src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) $< -o $@ $(GTK_LIBS)

# Cross compile against a Kindle sysroot. Link only what the device ships.
kindle:
	$(MAKE) BIN=build/desk-kindl-arm \
	  PKG="PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/pkgconfig pkg-config" \
	  CXXFLAGS="-O2 -std=c++11 --sysroot=$(SYSROOT) -static-libstdc++ -static-libgcc"

# Package a KUAL extension: copy dist/deskkindl to /mnt/us/extensions/ on the Kindle.
dist: kindle
	rm -rf dist && mkdir -p dist/deskkindl/bin
	cp -r kual/deskkindl/* dist/deskkindl/
	cp -r config dist/deskkindl/config
	cp build/desk-kindl-arm dist/deskkindl/bin/desk-kindl
	cd dist && zip -qr deskkindl.zip deskkindl

run: $(BIN)
	sed 's/^kindle = true/kindle = false/' config/dashboard.conf > build/desktop.conf
	cp config/env build/ 2>/dev/null || true
	cp -r config/scripts build/
	./$(BIN) build/desktop.conf

clean:
	rm -rf build dist

.PHONY: all kindle dist run clean
