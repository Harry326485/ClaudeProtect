#!/usr/bin/env python3
"""Build a standalone Apple Silicon App and drag-to-Applications DMG.

Run from any directory. Download the pinned runtime assets into downloads first;
the archive is verified against this source-controlled SHA-256 before use.
"""
import hashlib
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/macos"
VERSION = "0.3.0"
RUNTIME = "cpython-3.13.18+20261110-aarch64-apple-darwin-install_only_stripped.tar.gz"
RUNTIME_SHA256 = "7a2384641584ab9984221fcb0fc9265df465922af71be71410529e2a9c3f0540"
RUNTIME_URL = "https://github.com/astral-sh/python-build-standalone/releases/download/20261110/" + RUNTIME.replace("+", "%2B")


def run(*arguments):
    subprocess.run([str(x) for x in arguments], check=True, cwd=ROOT)


def main():
    archive = BUILD / "downloads" / RUNTIME
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != RUNTIME_SHA256:
        raise SystemExit("Download the pinned runtime from " + RUNTIME_URL + " into " + str(archive.parent) + "; SHA-256 verification is required.")
    run("bash", ROOT / "macos/build.sh")
    stage = BUILD / "dmg-stage"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    app = stage / "ClaudeProtect.app"
    shutil.copytree(BUILD / "ClaudeProtect.app", app, symlinks=True)
    resources = app / "Contents/Resources"
    backend = resources / "backend"
    backend.mkdir()
    # Explicit allowlist: never copy workspace profiles, logs or credentials.
    for name in ("runner.py", "settings.py", "config_service.py", "supervisor.py", "ccswitch_profile.py", "README.md"):
        shutil.copy2(ROOT / "macos" / name, backend / name)
    shutil.copy2(BUILD / "EnvironmentProbe", resources / "EnvironmentProbe")
    shutil.copy2(ROOT / "LICENSE", resources / "LICENSE")
    shutil.copytree(ROOT / "macos/runtime-licenses", resources / "Python-Licenses")
    with tarfile.open(archive) as source:
        source.extractall(resources, filter="data")
    (resources / "PYTHON-RUNTIME-NOTICE.txt").write_text(
        "Python runtime: CPython 3.13.18, python-build-standalone 20261110.\n"
        "Distributor: astral-sh/python-build-standalone.\n" + RUNTIME_URL + "\nSHA-256: " + RUNTIME_SHA256 +
        "\nRuntime license notices are included under python/.\n"
        "Python source: https://www.python.org/downloads/source/\n"
        "Build recipes and dependency sources: https://github.com/astral-sh/python-build-standalone/tree/20261110\n")
    # Reuse the repository icon. The largest ICO entry is an embedded PNG.
    for state, icon_name in (("stopped", "ClaudeProtect"), ("running", "ClaudeProtect-running")):
        ico = (ROOT / f"assets/claude-{state}.ico").read_bytes()
        count = struct.unpack_from("<H", ico, 4)[0]
        entries = [struct.unpack_from("<BBBBHHII", ico, 6 + 16 * i) for i in range(count)]
        largest = max(entries, key=lambda e: (e[0] or 256) * (e[1] or 256))
        png = ico[largest[7]:largest[7] + largest[6]]
        if not png.startswith(b"\x89PNG"):
            raise SystemExit("Expected PNG image in largest ICO entry")
        iconset = BUILD / (icon_name + ".iconset")
        iconset.mkdir(exist_ok=True)
        base = BUILD / (icon_name + "-base.png")
        base.write_bytes(png)
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
                run("sips", "-z", size * scale, size * scale, base, "--out", iconset / name)
        run("iconutil", "-c", "icns", iconset, "-o", resources / (icon_name + ".icns"))
    info_path = app / "Contents/Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.pop("CPPython", None)
    info.update(CFBundleDisplayName="ClaudeProtect", CFBundleIconFile="ClaudeProtect.icns", CFBundleVersion=VERSION)
    info_path.write_bytes(plistlib.dumps(info))
    # Sign every native runtime component from the inside out, then the bundle.
    macho = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"}
    for path in sorted(resources.rglob("*")):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as stream:
                if stream.read(4) in macho:
                    run("codesign", "--force", "--sign", "-", path)
    run("codesign", "--force", "--sign", "-", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    # Verify interpreter relocation with isolated mode, before creating the DMG.
    run(resources / "python/bin/python3", "-I", "-B", "-c", "import ctypes, ssl, sqlite3, zoneinfo; print('Embedded Python runtime OK')")
    (stage / "Applications").symlink_to("/Applications")
    (stage / "安装说明.txt").write_text(
        "将 ClaudeProtect.app 拖入 Applications，然后打开。需要 macOS 13+、Apple Silicon 和已安装的官方 Claude。\n"
        "本构建使用本地临时签名，未经过 Apple Developer ID 签名或公证；macOS 可能要求在系统设置中确认打开。\n"
        "用户资料保存在 ~/Library/Application Support/ClaudeProtect，不包含在此安装包中。\n"
        "原生子进程语言区域未完全覆盖，详见 App 菜单中的适配说明。\n")
    dmg = BUILD / f"ClaudeProtect-{VERSION}-macOS-arm64.dmg"
    if dmg.exists():
        dmg.unlink()
    run("hdiutil", "create", "-volname", f"ClaudeProtect {VERSION}", "-srcfolder", stage, "-format", "UDZO", dmg)
    checksum = hashlib.sha256(dmg.read_bytes()).hexdigest()
    dmg.with_suffix(".dmg.sha256").write_text(checksum + "  " + dmg.name + "\n")
    print("Created", dmg)


if __name__ == "__main__":
    main()
