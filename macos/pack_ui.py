#!/usr/bin/env python3
"""Package the shared Windows/macOS front end as a self-contained local page."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
html = (root / "ui/index.html").read_text()
for placeholder, relative in (("/* APP_STYLE */", "ui/app.css"), ("/* APP_SCRIPT */", "ui/app.js"), ("<!-- APP_LOGO_STOPPED -->", "assets/claude-stopped.svg"), ("<!-- APP_LOGO_RUNNING -->", "assets/claude-running.svg")):
    html = html.replace(placeholder, (root / relative).read_text())
destination = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/macos/ClaudeProtect.app") / "Contents/Resources"
destination.mkdir(parents=True, exist_ok=True)
(destination / "panel.html").write_text(html)
