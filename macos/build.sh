#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/macos
xcrun clang -arch arm64 -mmacosx-version-min=13.0 -fobjc-arc -framework Foundation macos/environment_probe.m -o build/macos/EnvironmentProbe
mkdir -p build/macos/ClaudeProtect.app/Contents/MacOS
xcrun swiftc -target arm64-apple-macos13.0 -module-cache-path build/macos/module-cache macos/MenuApp.swift -o build/macos/ClaudeProtect.app/Contents/MacOS/ClaudeProtect -framework AppKit -framework WebKit
python3 macos/pack_ui.py
python3 - <<'PY'
from pathlib import Path
import plistlib, sys
info = {
    'CFBundleExecutable': 'ClaudeProtect', 'CFBundleIdentifier': 'local.claudeprotect.menu',
    'CFBundleName': 'ClaudeProtect', 'CFBundleDisplayName': 'ClaudeProtect Experimental',
    'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': '0.3.0',
    'LSUIElement': False, 'LSMinimumSystemVersion': '13.0', 'CPPython': sys.executable,
}
Path('build/macos/ClaudeProtect.app/Contents/Info.plist').write_bytes(plistlib.dumps(info))
PY
codesign --force --sign - build/macos/ClaudeProtect.app
printf '%s\n' "Built build/macos/ClaudeProtect.app. Uses installed Claude with per-session environment."
