#!/usr/bin/env python3
"""Shared, validated macOS settings used by both the panel and supervisor."""
import json
import locale
import os
from pathlib import Path
import re
import shlex
from functools import lru_cache
from zoneinfo import ZoneInfo, available_timezones

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get("CLAUDEPROTECT_DATA_DIR", ROOT / "build/macos")).expanduser().resolve()
CONFIG = BUILD / "ClaudeProtect.json"
DEFAULTS = {
    "timezone": "America/Los_Angeles", "locale": "en-US",
    "verifyinjection": "strict", "verifygraceseconds": "8", "preflight": "auto",
    "watchdogseconds": "8", "pollintervalms": "200", "syncccswitch": "on",
    "executable": "", "extraarguments": "",
}
# verifyinjection / verifygraceseconds / watchdogseconds are retained only to
# read older config files. Environment mode does not use injection settings.
NAMES = {"timezone": "Timezone", "locale": "Locale", "verifyinjection": "VerifyInjection",
         "verifygraceseconds": "VerifyGraceSeconds", "preflight": "Preflight",
         "watchdogseconds": "WatchdogSeconds", "pollintervalms": "PollIntervalMs",
         "syncccswitch": "SyncCCSwitch", "executable": "Executable", "extraarguments": "ExtraArguments"}
EPOCHS = (1768478400, 1784116800)


@lru_cache(maxsize=64)
def c_locale(language):
    identifier = language.replace("-", "_")
    candidates = [identifier + ".UTF-8"]
    if identifier == "nb_NO":
        candidates.append("no_NO.UTF-8")
    old = locale.setlocale(locale.LC_TIME)
    try:
        for candidate in candidates:
            try:
                locale.setlocale(locale.LC_TIME, candidate)
                return candidate
            except locale.Error:
                pass
    finally:
        locale.setlocale(locale.LC_TIME, old)
    raise ValueError(f"Locale：当前 macOS 不提供 {language} 对应的 UTF-8 区域，请选择其他语言与区域。")


def source_app(value):
    if value:
        path = Path(value).expanduser().resolve()
        if path.is_file() and path.parent.name == "MacOS" and path.parent.parent.name == "Contents":
            path = path.parent.parent.parent
        candidates = [path]
    else:
        candidates = [Path("/Applications/Claude.app"), Path.home() / "Applications/Claude.app"]
    import plistlib
    for app in candidates:
        info_path = app / "Contents/Info.plist"
        if not info_path.is_file():
            continue
        info = plistlib.loads(info_path.read_bytes())
        if info.get("CFBundleIdentifier") == "com.anthropic.claudefordesktop" and (app / "Contents/MacOS" / info.get("CFBundleExecutable", "Claude")).is_file():
            return app
    raise ValueError("Executable：未找到 Claude Desktop，请选择已安装的 Claude.app 或其中的可执行文件。")


def extra_arguments(value):
    try:
        arguments = shlex.split(value)
    except ValueError as error:
        raise ValueError(f"ExtraArguments：参数引号不完整：{error}") from None
    # The data directory and native Apple locale belong to the supervisor.
    # A conflicting --lang would make the region and renderer disagree.
    reserved = ("--user-data-dir", "--lang", "-AppleLanguages", "-AppleLocale")
    for argument in arguments:
        if any(argument == key or argument.startswith(key + "=") for key in reserved):
            raise ValueError(f"ExtraArguments：{argument.split('=')[0]} 由时区、语言和隔离资料设置统一管理，请在通用设置中修改。")
    return arguments


def validate(values):
    if not isinstance(values, dict) or set(values) - set(DEFAULTS):
        raise ValueError("设置表单包含未知字段，请重新加载。")
    result = dict(DEFAULTS)
    for key, value in values.items():
        if not isinstance(value, str) or len(value) > 32767 or "\0" in value or "\n" in value or "\r" in value:
            raise ValueError(f"{NAMES[key]}：设置必须是有效的单行文本。")
        result[key] = value.strip()
    try:
        ZoneInfo(result["timezone"])
    except (ValueError, KeyError):
        raise ValueError("Timezone：请选择有效的 IANA 时区，例如 America/Los_Angeles。") from None
    language = result["locale"]
    if not re.fullmatch(r"[A-Za-z]{2,3}-[A-Za-z]{2}", language):
        raise ValueError("Locale：请选择语言与区域，例如 en-US、zh-CN。")
    left, right = language.split("-")
    result["locale"] = left.lower() + "-" + right.upper()
    c_locale(result["locale"])
    for key, choices in {"verifyinjection": ("strict", "warn", "off"), "preflight": ("auto", "require", "off"), "syncccswitch": ("on", "off")}.items():
        if result[key] not in choices:
            raise ValueError(f"{NAMES[key]}：不支持的选项。")
    for key, low, high in (("verifygraceseconds", 1, 120), ("watchdogseconds", 3, 120), ("pollintervalms", 100, 5000)):
        if not re.fullmatch(r"\d+", result[key]) or not low <= int(result[key]) <= high:
            raise ValueError(f"{NAMES[key]}：请输入 {low}–{high} 之间的整数。")
        result[key] = str(int(result[key]))
    if result["executable"]:
        source_app(result["executable"])
    extra_arguments(result["extraarguments"])
    return result


def load():
    if not CONFIG.is_file():
        return validate(DEFAULTS)
    return validate(json.loads(CONFIG.read_text()))


def save(values):
    result = validate(values)
    BUILD.mkdir(parents=True, exist_ok=True)
    if CONFIG.is_symlink():
        raise ValueError("配置文件不能是符号链接。")
    if CONFIG.exists():
        backup = CONFIG.with_suffix(".json.bak")
        fd = os.open(backup, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(CONFIG.read_bytes())
    temporary = CONFIG.with_suffix(".json.tmp")
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
    temporary.replace(CONFIG)
    return result


def zone_matches(actual, expected):
    if actual == expected:
        return True
    utc = {"UTC", "GMT", "Etc/UTC", "Etc/GMT", "Etc/UCT", "UCT", "Zulu", "Universal"}
    if actual in utc and expected in utc:
        return True
    try:
        first, second = Path("/usr/share/zoneinfo") / actual, Path("/usr/share/zoneinfo") / expected
        return first.read_bytes() == second.read_bytes()
    except (OSError, TypeError):
        return False


def offsets(values):
    from datetime import datetime
    zone = ZoneInfo(values["timezone"])
    return [int(datetime.fromtimestamp(epoch, zone).utcoffset().total_seconds()) for epoch in EPOCHS]


def timezones():
    from datetime import datetime, timezone
    now = datetime.now(timezone.utc)
    choices = []
    for name in available_timezones():
        if name.startswith(("posix/", "right/")) or name in ("localtime", "Factory"):
            continue
        delta = int(now.astimezone(ZoneInfo(name)).utcoffset().total_seconds() / 60)
        sign = "+" if delta >= 0 else "−"
        hours, minutes = divmod(abs(delta), 60)
        choices.append((delta, name, {"id": name, "label": f"UTC{sign}{hours:02}:{minutes:02} · {name}"}))
    return [item[2] for item in sorted(choices)]
