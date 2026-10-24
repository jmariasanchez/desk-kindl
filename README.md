# desk-kindl

A native (C++ / GTK 2) desk dashboard for jailbroken Kindles: clock, calendar,
weather, transit departures, GitHub build status and home-server health on
e-ink.

## How it works

The binary draws a header (clock, date, last update) and a grid of panels.
Each panel runs a shell command on an interval and shows its output. Data
sources live in `config/scripts/` as POSIX sh (busybox friendly), so adding a
new panel is just a new script plus a `[panel Name]` block in
`config/dashboard.conf`. Print `#markup` as the first line to use Pango markup.

E-ink niceties: the clock redraws once per minute, a full `eips -c` flash
clears ghosting every `full_refresh` seconds, tapping the screen forces a
refresh, and the screensaver is suppressed while running.

## Configure

```sh
cp config/env.example config/env   # iCal URL, weather location, repos, hosts...
```

## Run on desktop

```sh
make run      # needs gtk+-2.0 via pkg-config; uses kindle = false
```

Keys: `r` refresh, `q`/`Esc` quit.

## Build for Kindle

Use [koxtoolchain](https://github.com/koreader/koxtoolchain) (e.g.
`kindlepw2` for PW2+ / `kindlehf` for firmware 5.16.3+) and a sysroot holding
the device's GTK 2 headers and libs (pull `/usr/lib` from the Kindle):

```sh
make dist CROSS=arm-kindlepw2-linux-gnueabi- SYSROOT=/path/to/kindle-sysroot
```

Copy `dist/deskkindl/` to `/mnt/us/extensions/` and launch it from KUAL
("Desk Dashboard > Start dashboard"). Logs: `/tmp/desk-kindl.log`.

## Requirements on device

Jailbreak + KUAL. Scripts use `curl` or busybox `wget`. `jq` is optional
(improves transit parsing). HTTPS with busybox wget may need a
TLS-capable build; installing a static curl is recommended.
