"""Exercise real startup without a display, network, or Kindle access."""
import os
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
binary = root / "build/desk-kindl"
with tempfile.TemporaryDirectory(prefix="startup-test-", dir=root / "build") as directory:
    work = Path(directory)
    feed = work / "empty.ics"
    feed.write_text("BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//Test//EN\nEND:VCALENDAR\n")
    link = work / "dashboard-link"
    link.symlink_to(binary)
    cases = [
        ("absolute path", str(binary), None, os.environ.copy()),
        ("relative path", "../desk-kindl", None, os.environ.copy()),
        ("PATH lookup", binary.name, None, dict(os.environ, PATH=str(binary.parent))),
        ("relative PATH", binary.name, None, dict(os.environ, PATH="..")),
        ("launcher argv alias", "dashboard-launcher", str(binary), dict(os.environ, PATH="")),
        ("symlink outside package", str(link), None, os.environ.copy()),
    ]
    for name, argv0, executable, env in cases:
        result = subprocess.run(
            [argv0, "--calendar", str(feed), "Europe/Dublin"],
            executable=executable, cwd=work, env=env, capture_output=True, text=True,
            timeout=10,
        )
        assert result.returncode == 0, (
            f"{name}: exit {result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
        assert "No more appointments today" in result.stdout, (name, result.stdout)
        print(f"Startup check passed: {name}")
