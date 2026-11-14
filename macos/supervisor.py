#!/usr/bin/env python3
"""Launch installed Claude with per-session environment and user-space supervision.

No injection, app patching or re-signing. The independent profile is retained.
Native language preferences and renderer APIs are not guaranteed by this mode.
"""
import argparse
import ctypes
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from ccswitch_profile import sync_profile
import settings as preferences

ROOT = Path(__file__).resolve().parent.parent
BUILD = preferences.BUILD
STATUS = BUILD / "status.json"

class BSDInfo(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in (
        "flags", "status", "xstatus", "pid", "ppid", "uid", "gid", "ruid", "rgid", "svuid", "svgid", "reserved"
    )] + [("comm", ctypes.c_char * 16), ("name", ctypes.c_char * 32)] + [(name, ctypes.c_uint32) for name in (
        "nfiles", "pgid", "jobc", "tdev", "tpgid"
    )] + [("nice", ctypes.c_int32), ("start_sec", ctypes.c_uint64), ("start_usec", ctypes.c_uint64)]


LIBPROC = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
LIBPROC.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
LIBPROC.proc_pidinfo.restype = ctypes.c_int
LIBPROC.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
LIBPROC.proc_pidpath.restype = ctypes.c_int
LIBPROC.proc_listallpids.argtypes = [ctypes.c_void_p, ctypes.c_int]
LIBPROC.proc_listallpids.restype = ctypes.c_int


def process_info(pid):
    info = BSDInfo()
    if LIBPROC.proc_pidinfo(pid, 3, 0, ctypes.byref(info), ctypes.sizeof(info)) != ctypes.sizeof(info):
        return None
    path = ctypes.create_string_buffer(4096)
    LIBPROC.proc_pidpath(pid, path, len(path))
    return {"pid": pid, "ppid": info.ppid, "group": info.pgid, "identity": (info.start_sec, info.start_usec), "path": os.fsdecode(path.value), "zombie": info.status == 5}


def processes():
    capacity = max(LIBPROC.proc_listallpids(None, 0) * 2, 4096)
    buffer = (ctypes.c_int * capacity)()
    count = LIBPROC.proc_listallpids(buffer, ctypes.sizeof(buffer))
    if count <= 0 or count >= capacity:
        raise RuntimeError("Cannot obtain a complete process inventory")
    return {pid: info for pid in buffer[:count] if pid > 0 and (info := process_info(pid)) and not info["zombie"]}


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def publish(state, detail, **extra):
    value = {"state": state, "detail": detail, "timestamp": time.time(), **extra}
    temporary = STATUS.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2))
    temporary.replace(STATUS)
    signature = (state, detail, extra.get("processes"))
    if getattr(publish, "previous", None) != signature:
        with (BUILD / "status-history.jsonl").open("a") as stream:
            stream.write(json.dumps(value, ensure_ascii=False) + "\n")
        publish.previous = signature


class Session:
    def __init__(self, owner=None, values=None):
        self.values = preferences.validate(values or preferences.load())
        self.gui_owner = owner
        self.known = {}
        self.process = None
        self.stop_requested = False
        self.directory = BUILD / f"session-{time.time_ns()}"
        self.directory.mkdir(mode=0o700)

    def owned(self):
        table = processes()
        # Never select every process at the installed app path: only this
        # session's observed identities and descendants belong to us.
        selected = {pid: item for pid, item in table.items() if self.known.get(pid) == item["identity"]}
        while True:
            added = {pid: item for pid, item in table.items() if pid not in selected and item["ppid"] in selected}
            if not added:
                break
            selected.update(added)
        for pid, item in selected.items():
            self.known[pid] = item["identity"]
        return selected

    def signal_one(self, item, sig):
        current = process_info(item["pid"])
        if current and current["identity"] == item["identity"] and item["pid"] != os.getpid():
            try:
                os.kill(item["pid"], sig)
            except ProcessLookupError:
                pass

    def cleanup(self):
        for _ in range(10):
            owned = self.owned()
            if not owned:
                break
            for item in owned.values():
                self.signal_one(item, signal.SIGSTOP)
            for item in self.owned().values():
                self.signal_one(item, signal.SIGKILL)
            time.sleep(0.1)
        if self.process:
            self.process.wait(timeout=5)
        remaining = self.owned()
        if remaining:
            raise RuntimeError(f"Failed to stop {len(remaining)} session processes")

    def environment(self):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("DYLD_", "CP_", "LC_"))
               and key not in ("ELECTRON_RUN_AS_NODE", "NODE_OPTIONS", "LANG", "LANGUAGE")}
        c_locale = preferences.c_locale(self.values["locale"])
        env.update(TZ=self.values["timezone"], LANG=c_locale, LC_ALL=c_locale)
        return env

    def apple_arguments(self):
        return ["-AppleLanguages", "(" + self.values["locale"] + ")",
                "-AppleLocale", self.values["locale"].replace("-", "_")]

    def preflight(self, required=False):
        probe = Path(os.environ.get("CLAUDEPROTECT_RESOURCE_DIR", BUILD)) / "EnvironmentProbe"
        if not probe.is_file():
            if required:
                raise RuntimeError("预检探针缺失，请重新构建 ClaudeProtect。")
            return "未找到探针，已跳过自动预检。"
        # Deliberately use inherited environment only. Record the native locale
        # as an observation, never pretend it is forced to match LANG.
        with (self.directory / "preflight-output.json").open("wb") as log:
            process = subprocess.Popen([str(probe)], env=self.environment(), stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            info = process_info(process.pid)
            if info:
                self.known[process.pid] = info["identity"]
            deadline = time.monotonic() + 15
            try:
                while process.poll() is None:
                    if self.stop_requested or time.monotonic() > deadline:
                        raise RuntimeError("环境预检已取消或超时。")
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
        report = read_json(self.directory / "preflight-output.json")
        expected = preferences.offsets(self.values)
        valid = isinstance(report, dict) and preferences.zone_matches(report.get("foundationTimeZone"), self.values["timezone"]) and report.get("libcLocale") == preferences.c_locale(self.values["locale"])
        if valid:
            samples = report.get("offsets", [])
            valid = len(samples) == len(expected) and all(sample.get("libcOffset") == offset and sample.get("foundationOffset") == offset for sample, offset in zip(samples, expected))
        if process.returncode or not valid:
            raise RuntimeError("环境预检失败：时区、语言或夏令时结果与设置不符。")
        return "环境预检通过。"

    def run(self, seconds):
        publish("preparing", "正在准备官方 Claude 的启动环境", values=self.values)
        app = preferences.source_app(self.values["executable"])
        # An old experimental copy must never silently become the default.
        if app.resolve().is_relative_to(BUILD.resolve()):
            raise RuntimeError("环境变量模式请选择正式安装的 Claude.app，不能使用工作区实验副本。")
        if any(item["path"].startswith((str(app) + "/", str(BUILD / "Claude.app") + "/")) for item in processes().values()):
            raise RuntimeError("请先退出已运行的 Claude，再从 ClaudeProtect 启动。")
        check = subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], capture_output=True)
        if check.returncode:
            raise RuntimeError("已安装 Claude 的签名校验失败，请选择完整的正式安装版。")
        if self.values["syncccswitch"] == "on" and not sync_profile(BUILD):
            raise RuntimeError("未找到已选择的 CC Switch Desktop 配置。")
        # Keep existing independent data. Do not copy daily login material.
        for name in ("profile", "profile-3p"):
            profile = BUILD / name
            if profile.is_symlink():
                raise RuntimeError("独立资料目录不能是符号链接。")
            profile.mkdir(exist_ok=True, mode=0o700)
        if self.values["preflight"] != "off":
            publish("preparing", "正在执行启动前环境预检", values=self.values)
            self.preflight(required=self.values["preflight"] == "require")
        if self.stop_requested:
            return "用户停止会话"
        import plistlib
        info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
        command = [str(app / "Contents/MacOS" / info.get("CFBundleExecutable", "Claude")),
                   f"--user-data-dir={BUILD / 'profile'}", "--lang=" + self.values["locale"]] + self.apple_arguments() + preferences.extra_arguments(self.values["extraarguments"])
        started = time.monotonic()
        with (self.directory / "console.log").open("wb") as log:
            self.process = subprocess.Popen(command, env=self.environment(), stdout=log, stderr=log, start_new_session=True)
            info = process_info(self.process.pid)
            if not info:
                raise RuntimeError("无法确认 Claude 启动进程。")
            self.known[self.process.pid] = info["identity"]
            while not self.stop_requested:
                if self.gui_owner:
                    owner = process_info(self.gui_owner["pid"])
                    if not owner or owner["identity"] != self.gui_owner["identity"]:
                        raise RuntimeError("控制程序已退出，正在停止会话。")
                if self.process.poll() is not None:
                    if self.process.returncode:
                        raise RuntimeError(f"Claude exited with code {self.process.returncode}")
                    return "Claude 已退出"
                owned = self.owned()
                publish("running", "会话运行中", pid=self.process.pid,
                        processes=len(owned), values=self.values, session=self.directory.name)
                if seconds and time.monotonic() - started >= seconds:
                    return "定时运行已结束"
                time.sleep(int(self.values["pollintervalms"]) / 1000)
        return "用户停止会话"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=0, help="Stop after N seconds; 0 runs until stopped")
    parser.add_argument("--owner-pid", type=int, help="Stop if the controller exits")
    parser.add_argument("--selftest", action="store_true", help="Check launcher environment without starting Claude")
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error("--seconds cannot be negative")
    BUILD.mkdir(parents=True, exist_ok=True)
    with (BUILD / "session.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit("A ClaudeProtect session is already running")
        owner = process_info(args.owner_pid) if args.owner_pid else None
        if args.owner_pid and not owner:
            raise SystemExit("Menu owner is not running")
        session = Session(owner=owner)
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda *_: setattr(session, "stop_requested", True))
        failure = None
        try:
            detail = session.preflight(required=True) if args.selftest else session.run(args.seconds)
        except Exception as error:
            failure = str(error)
            detail = "会话已停止：" + failure
        finally:
            try:
                session.cleanup()
            except Exception as error:
                failure = str(error)
                detail = "清理失败：" + failure
            publish("failed" if failure else "stopped", detail, session=session.directory.name)
        print(detail)
        raise SystemExit(1 if failure else 0)


if __name__ == "__main__":
    main()
