#!/usr/bin/env python3
"""Import CC Switch's selected Desktop profile into isolated worktree data only."""
import json
import os
from pathlib import Path
import urllib.parse
import urllib.request

PROFILE_ID = "00000000-0000-4000-8000-000000157210"


def write_private(path, value):
    if path.is_symlink():
        raise RuntimeError("Refusing a symlink in the experimental profile")
    temporary = path.with_name(path.name + ".claudeprotect.tmp")
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
    os.chmod(temporary, 0o600)
    temporary.replace(path)


def sync_profile(build):
    source = Path.home() / "Library/Application Support/Claude-3p"
    meta_path = source / "configLibrary/_meta.json"
    if not meta_path.is_file():
        return False
    meta = json.loads(meta_path.read_text())
    if meta.get("appliedId") != PROFILE_ID:
        raise RuntimeError("CC Switch Desktop profile is not the selected third-party configuration")
    profile = json.loads((source / f"configLibrary/{PROFILE_ID}.json").read_text())
    url = urllib.parse.urlsplit(profile.get("inferenceGatewayBaseUrl", ""))
    if profile.get("inferenceProvider") != "gateway" or url.scheme != "http" or url.hostname != "127.0.0.1" or url.path.rstrip("/") != "/claude-desktop" or url.username or url.password or url.query or url.fragment:
        raise RuntimeError("Expected CC Switch's reviewed local Desktop model-mapping gateway")
    token = profile.get("inferenceGatewayApiKey")
    if not isinstance(token, str) or not token:
        raise RuntimeError("CC Switch Desktop local gateway credential is missing")
    # A disabled CC Switch routing service must not cause an invisible fallback
    # to the official service or a different inference provider.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    request = urllib.request.Request(profile["inferenceGatewayBaseUrl"].rstrip("/") + "/v1/models", headers={"Authorization": "Bearer " + token})
    try:
        with opener.open(request, timeout=5) as response:
            models = json.load(response)
            if not models.get("data"):
                raise RuntimeError("CC Switch returned no Desktop model routes")
    except Exception:
        raise RuntimeError("CC Switch Desktop local routing is unavailable; start its routing service") from None
    # Desktop reads the mode selector before switching to its '-3p' data path.
    # Supply its selected library in both isolated paths; never copy login data.
    for name in ("profile", "profile-3p"):
        destination = build / name
        if destination.is_symlink() or not destination.resolve().is_relative_to(build.resolve()):
            raise RuntimeError("Refusing an experimental profile outside the build directory")
        destination.mkdir(exist_ok=True, mode=0o700)
        os.chmod(destination, 0o700)
        library = destination / "configLibrary"
        if library.is_symlink():
            raise RuntimeError("Refusing a symlinked configuration library")
        library.mkdir(exist_ok=True, mode=0o700)
        config_path = destination / "claude_desktop_config.json"
        config = json.loads(config_path.read_text()) if config_path.is_file() else {}
        config["deploymentMode"] = "3p"
        write_private(config_path, config)
        destination_meta = library / "_meta.json"
        current = json.loads(destination_meta.read_text()) if destination_meta.is_file() else {}
        entries = [entry for entry in current.get("entries", []) if entry.get("id") != PROFILE_ID]
        entries.append({"id": PROFILE_ID, "name": "CC Switch"})
        current.update(appliedId=PROFILE_ID, entries=entries)
        write_private(library / f"{PROFILE_ID}.json", profile)
        write_private(destination_meta, current)
    return True
