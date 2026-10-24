# desk-kindl

A native (C++ / GTK 2) desk clock for jailbroken Kindles. A large clock and
date lead the screen, followed by today's Google Calendar agenda, weather
and transit. Server health remains an optional panel.

## How it works

The binary draws a clock and date above a grid of borderless panels.
Each panel runs a shell command on an interval and shows its output. Data
sources live in `config/scripts/` as POSIX sh (busybox friendly), so adding a
new panel is just a new script plus a `[panel Name]` block in
`config/dashboard.conf`. Print `#markup` as the first line to use Pango markup.

The clock redraws once per minute. A full-window GTK repaint runs every
`full_refresh` seconds, tapping refreshes the panels, and the screensaver is
suppressed while running.

## Configure

```sh
cp config/env.example config/env   # iCal URL, weather location, hosts...
```

### Orientation

Set `orientation = portrait | landscape | auto` in `config/dashboard.conf`.
Portrait puts the clock on top with panels stacked below. Landscape puts
the clock on the left and panels in a column on the right. On the Kindle,
use the KUAL menu items "Landscape layout" / "Portrait layout" to switch
(the screen is rotated and the choice saved). On desktop, press `o`.

### Google Calendar

In Google Calendar on a computer, open **Settings → Settings for my calendars
→ your calendar → Integrate calendar → Secret address in iCal format**.
Paste that HTTPS link into `ICAL_URL` in `config/env`. This is a read-only
download: no Apps Script, OAuth setup, or public calendar is required.
Treat the link as a password; anyone with it can read the calendar.
[Google's instructions](https://support.google.com/calendar/answer/37648?hl=en).
Some managed Workspace accounts do not expose a secret iCal address.

Set `timezone` in `config/dashboard.conf` to your IANA timezone, e.g.
`Europe/Dublin` (the default). Both the clock and event times use it, including
daylight-saving changes. Keep the Kindle's system clock accurate.

The feed downloads every 15 minutes. The agenda is recalculated from the
local copy every minute: current/next appointment, up to three remaining
appointments, and up to two all-day events, with a count when more exist.
Recurring events, individual reschedules/cancellations, and event timezones
are handled with libical. A failed or invalid download keeps the last good
copy and shows its age. The private cache lives in `config/.calendar-cache/`
on the Kindle. Tapping refreshes the display; downloads still respect the
15-minute interval. This is not two-way sync or an event editor.
Feeds with `RECURRENCE-ID;RANGE` overrides are rejected, preserving the last good copy.

## Run on desktop

```sh
make run      # uses kindle = false
make test     # calendar and offline-cache regression checks
make test-ui  # layout checks at 758×1024 and 600×800; requires a display
```

Build prerequisites: C++11 compiler, GTK 2 via pkg-config, CMake, Perl,
curl, and shasum; Python 3 for tests. The first build downloads and verifies
libical 3.0.20, then links it statically. Timezone data is bundled with the app.

Keys: `r` refresh, `q`/`Esc` quit. On the Kindle: tap to refresh, hold 2s to quit (or KUAL > Stop dashboard).

## Build for Kindle

For the current ARM hard-float Kindle (firmware 5.16.3+), use:

```sh
bash cross.sh
```

This uses Nix's clang/LLVM 21 and the existing pomo-kndl sysroot at
`~/.cache/pomo-kndl/sysroot` (override with `DESKKINDL_SYSROOT`). Run
`pomo-kndl/kindle/cross.sh` first if that sysroot is missing. The build also
cross-compiles libical and packages its timezone data.

Alternatively, use [koxtoolchain](https://github.com/koreader/koxtoolchain) (e.g.
`kindlepw2` for PW2+ / `kindlehf` for firmware 5.16.3+) and a sysroot holding
the device's GTK 2 headers and libs (pull `/usr/lib` from the Kindle):

```sh
make dist CROSS=arm-kindlepw2-linux-gnueabi- SYSROOT=/path/to/kindle-sysroot
```

Copy `dist/deskkindl/` to `/mnt/us/extensions/` and launch it from KUAL
("Desk Dashboard > Start dashboard"). Logs: `/mnt/us/extensions/deskkindl/desk-kindl.log`.
Configure `config/env` on the device; builds do not copy your local credentials
or cached events. The bundled libical source is available from
[the pinned release](https://github.com/libical/libical/releases/tag/v3.0.20),
under the MPL 2.0 license included in the package.

## Requirements on device

Jailbreak + KUAL. Scripts use `curl` or busybox `wget`. `jq` is optional
(improves transit parsing). HTTPS with busybox wget may need a
TLS-capable build; installing a static curl is recommended.
