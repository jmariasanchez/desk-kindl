"""Exercise the real shell downloader/parser with a local curl substitute."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix="calendar-test-", dir=root / "build"))
shutil.copytree(root / "config/scripts", work / "scripts")
(work / "bin").mkdir()
(work / "bin/curl").write_text('#!/bin/sh\ncat "$TEST_FEED"\nexit "$TEST_EXIT"\n')
(work / "bin/curl").chmod(0o755)
feed = work / "feed.ics"
valid = "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//Test//EN\nEND:VCALENDAR\n"
feed.write_text(valid)
env = dict(os.environ, PATH=str(work / "bin") + ":" + os.environ["PATH"],
           ICAL_URL="https://example.test/private-calendar", TEST_FEED=str(feed), TEST_EXIT="0",
           DESKKINDL_BIN=str(root / "build/desk-kindl"), DESKKINDL_TIMEZONE="Europe/Dublin")


def run():
    return subprocess.run(["sh", "scripts/calendar.sh"], cwd=work, env=env,
                          capture_output=True, text=True)


result = run()
assert result.returncode == 0, result.stderr + result.stdout
assert "No more appointments today" in result.stdout
cache = next((work / ".calendar-cache").glob("*.ics"))
assert cache.read_text() == valid
assert cache.stat().st_mode & 0o077 == 0
# Fresh caches avoid the network entirely, even if it would now fail.
env["TEST_EXIT"] = "1"
feed.write_text("failed download")
assert "Sync unavailable" not in run().stdout
old = time.time() - 1800
os.utime(cache, (old, old))
result = run()
assert "Sync unavailable" in result.stdout
assert "No more appointments today" in result.stdout
assert cache.read_text() == valid
# A failed sync is not retried on the next minute's run.
env["TEST_EXIT"] = "0"
feed.write_text(valid.replace("END:VCALENDAR", "BEGIN:VEVENT\nUID:x\nDTSTART:20300101T000000Z\nEND:VEVENT\nEND:VCALENDAR"))
assert "Sync unavailable" in run().stdout
assert cache.read_text() == valid
feed.write_text("failed download")
tried = Path(str(cache) + ".tried")
tried.write_text(str(int(time.time()) - 1000))
# An HTTP 200 login/error page must not replace a valid calendar.
env["TEST_EXIT"] = "0"
assert "Sync unavailable" in run().stdout
assert cache.read_text() == valid
# A different calendar must never display the previous calendar's cached data.
env["ICAL_URL"] = "https://example.test/different-calendar"
result = run()
assert result.returncode != 0
assert "Calendar unavailable" in result.stdout
assert "No more appointments" not in result.stdout
env["ICAL_URL"] = ""
assert "Add your Google Calendar" in run().stdout
print("Calendar cache checks passed (offline, malformed response, source change, private permissions)")
