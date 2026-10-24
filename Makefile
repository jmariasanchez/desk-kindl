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
ICAL_DIR ?= build/deps
ICAL_LIB := $(ICAL_DIR)/ical/lib/libical.a
ICAL_CFLAGS := -I$(ICAL_DIR)/ical/src -I$(ICAL_DIR)/libical-3.0.20/src/libical

all: $(BIN)

$(ICAL_LIB): scripts/build-libical.sh
	sh scripts/build-libical.sh $(ICAL_DIR)

build/zoneinfo/zones.tab: $(ICAL_LIB)
	cp -r $(ICAL_DIR)/libical-3.0.20/zoneinfo build/

$(BIN): src/main.cpp src/calendar.cpp src/calendar.h $(ICAL_LIB) build/zoneinfo/zones.tab
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) $(ICAL_CFLAGS) src/main.cpp src/calendar.cpp -o $@ $(GTK_LIBS) $(ICAL_LIB) -pthread

# Cross compile against a Kindle sysroot. Link only what the device ships.
kindle:
	sh scripts/build-libical.sh build-kindle/deps -DCMAKE_SYSTEM_NAME=Linux \
	  -DCMAKE_C_COMPILER=$(CROSS)gcc -DCMAKE_SYSROOT=$(SYSROOT) \
	  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
	$(MAKE) BIN=build/desk-kindl-arm \
	  ICAL_DIR=build-kindle/deps \
	  PKG="PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/pkgconfig pkg-config" \
	  CXXFLAGS="-O2 -std=c++11 --sysroot=$(SYSROOT) -static-libstdc++ -static-libgcc"

# Package a KUAL extension: copy dist/deskkindl to /mnt/us/extensions/ on the Kindle.
dist: kindle
	rm -rf dist/deskkindl dist/deskkindl.zip
	mkdir -p dist/deskkindl/bin dist/deskkindl/config
	cp -r kual/deskkindl/* dist/deskkindl/
	cp config/dashboard.conf config/env.example dist/deskkindl/config/
	cp -r config/scripts build-kindle/deps/libical-3.0.20/zoneinfo dist/deskkindl/config/
	cp build-kindle/deps/libical-3.0.20/LICENSE.MPL2.txt dist/deskkindl/libical-LICENSE
	cp build/desk-kindl-arm dist/deskkindl/bin/desk-kindl
	cd dist && zip -qr deskkindl.zip deskkindl

run: $(BIN)
	sed 's/^kindle = true/kindle = false/' config/dashboard.conf > build/desktop.conf
	cp config/env build/ 2>/dev/null || true
	cp -r config/scripts build/
	cp -r $(ICAL_DIR)/libical-3.0.20/zoneinfo build/
	./$(BIN) build/desktop.conf

test: $(BIN)
	$(CXX) $(CXXFLAGS) $(ICAL_CFLAGS) tests/calendar.cpp src/calendar.cpp -o build/test-calendar $(ICAL_LIB) -pthread
	./build/test-calendar $(ICAL_DIR)/libical-3.0.20/zoneinfo
	python3 tests/calendar_cache.py

test-ui: $(BIN)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) $(ICAL_CFLAGS) tests/ui.cpp src/calendar.cpp -o build/test-ui $(GTK_LIBS) $(ICAL_LIB) -pthread
	cp tests/ui.conf build/ui.conf
	./build/test-ui build/ui.conf

clean:
	rm -rf build dist

.PHONY: all kindle dist run test test-ui clean
