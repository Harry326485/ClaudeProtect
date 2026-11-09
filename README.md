# ClaudeProtect

Windows x64 托盘程序，用于在指定时区和语言环境下启动 Claude Desktop，并监管它的进程树。

ClaudeProtect 使用 LocaleEmulatorPlus-Core（LEP）提供进程内时区、区域和语言虚拟化，通过 Windows Job Object 管理 Claude 及其子进程。默认配置为洛杉矶时区和美式英语，支持夏令时。

项目仍处于实验阶段。已有原生探针、Chromium 代理及 Claude 进程级验证，具体范围见下方的[验证与限制](#验证与限制)。

## 功能

- 托盘启动、停止 Claude Desktop，内置配置编辑器和只读日志窗口。待机显示灰色 Claude 图标，受监管会话运行时显示橙色 Claude 图标及蓝色盾牌。
- 动态发现 Claude Desktop 安装路径，按路径区分 Claude Desktop 与 Claude Code。
- 配置 Windows 时区、BCP-47 区域、代码页及首选语言。
- 启动前运行父子进程探针，运行中检查 LEP 注入记录和初始化日志。
- 默认严格验证，检测到受监管应用进程的注入失败时停止会话。
- 停止会话、Claude 主进程退出或监管进程关闭时，清理 Job 内的进程树。

运行采用用户模式，宿主时区、语言、注册表设置及 Claude 安装文件保持原值。历史验证中 Claude renderer 保留 `--enable-sandbox`。

## 环境要求

- Windows x64。历史验证使用的 Claude Desktop 为 4.29.0，其他版本需要自行验证。
- Git 和 PowerShell 7（`pwsh`）。
- MinGW-w64 的 x64 `g++` 和 `windres`，支持 C++17。已验证 GCC 8.1。
- 构建 LEP 还需要 Python 3.10+、MSVC x64/x86 C++ 工具链和 Windows SDK。已验证 MSVC 14.51 与 SDK 10.0.26100。

请将 `git`、`g++`、`windres`、`python`、`pwsh` 加入 `PATH`。MSVC 和 SDK 由脚本自动发现，也可通过构建参数指定。

## 从源码构建

使用 Git 克隆本仓库，在仓库根目录运行以下命令。GitHub 的源码 ZIP 需要另行取得子模块，建议使用 Git 克隆。

```powershell
git submodule update --init --recursive
pwsh -NoProfile -File scripts/build.ps1 -BuildLep
```

LEP 固定在 `0b11f533ef426ab5606e12f91acd4b1c2f7e6bad`。构建会先通过 `scripts/prepare-lep.ps1` 应用仓库内的集成补丁，重复构建会识别已应用状态。补丁与上游源码分别维护，说明见[补丁文档](patches/lep/README.md)。

也可以分别构建。

```powershell
pwsh -NoProfile -File scripts/build-lep.ps1
pwsh -NoProfile -File scripts/build.ps1
```

缺少 WDK 的 `km` 头目录时，脚本会在 `build/sdk-overlay` 创建包含两个最小声明头的 SDK overlay。具体工具链参数见 `scripts/build-lep.ps1` 的 `param`。

生成的运行目录如下。整个 `build/` 已加入 Git 忽略规则。

```text
build/
  ClaudeProtect.exe
  EnvironmentProbe.exe
  lep/
    LoaderDll_x64.dll
    LocaleEmulatorPlus_x64.dll
    LoaderDll_x86.dll
    LocaleEmulatorPlus_x86.dll
    LICENSE-LGPL.txt
    LICENSE-GPL.txt
```

## 使用

1. 退出当前直接启动的 Claude Desktop，包括后台进程。
2. 运行 `build\ClaudeProtect.exe`，在托盘菜单中打开 **Edit settings…**。
3. 设置时区和语言后保存。编辑器支持 Ctrl+S，保存前会校验配置。
4. 选择 **Start Claude Desktop**。若发现未受监管的 Claude Desktop，程序会拒绝启动，避免 Electron 单实例转发绕过监管。
5. 选择 **Stop Claude Desktop** 结束会话，或通过 **Exit** 关闭托盘程序并清理受监管进程。

配置和日志生成在可执行文件所在目录，因此该目录需要当前用户具有写入权限。

### 配置

`ClaudeProtect.ini` 在首次运行时生成，修改在下次启动会话时生效。`Executable` 留空时自动发现安装路径。建议通过内置编辑器修改配置。

```ini
[ClaudeProtect]
Executable=
Timezone=Pacific Standard Time
TimezoneIana=America/Los_Angeles
Locale=en-US
RegistryRedirectionMode=2
HookUILanguageMode=2
VerifyInjection=strict
VerifyGraceSeconds=8
Preflight=auto
LepDirectory=lep
ExtraArguments=
```

| 配置项 | 说明 |
| --- | --- |
| `Timezone` | Windows 时区 ID，例如 `Pacific Standard Time`、`Tokyo Standard Time`、`UTC` |
| `TimezoneIana` | IANA 名称，保留用于配置和诊断 |
| `Locale` | BCP-47 区域，例如 `en-US`、`ja-JP`、`zh-CN` |
| `VerifyInjection` | `strict` 检测失败时停止会话，`warn` 仅记录，`off` 关闭检查 |
| `Preflight` | `auto` 在探针存在时预检，`require` 要求探针存在并通过，`off` 跳过预检 |
| `LepDirectory` | LEP DLL 目录，支持相对可执行文件目录的路径 |
| `ExtraArguments` | 追加给 Claude Desktop 的启动参数 |

子进程的 `TZ` 根据 Windows 当前年份的时区规则生成 POSIX 值，例如洛杉矶为 `PST8PDT,M3.2.0,M11.1.0`。`--print-config` 输出中的 `childTZ` 可用于检查实际值。完整配置项见[实现文档](docs/implementation.md)。

ClaudeProtect 自动追加 `--lang=<Locale>`，已有 `--lang=` 参数时保留用户配置。Claude 自身保存的界面语言设置会优先于系统语言自动检测。

### 命令行

```powershell
.\build\ClaudeProtect.exe --help
.\build\ClaudeProtect.exe --discover
.\build\ClaudeProtect.exe --check
.\build\ClaudeProtect.exe --print-config
.\build\ClaudeProtect.exe --edit-settings
.\build\ClaudeProtect.exe --selftest
.\build\ClaudeProtect.exe --run --seconds 60
```

无参数启动进入托盘模式。CLI `--run` 默认持续 5 秒，适合诊断，可用 `--seconds` 调整。

日志可从托盘的 **Open log** 查看。提交诊断资料前，请检查其中的用户名、安装路径、进程命令行及其他个人信息。

## 测试

基础构建、配置和监管测试可在本机执行。

```powershell
pwsh -NoProfile -File scripts/build.ps1
pwsh -NoProfile -File scripts/test-settings.ps1
pwsh -NoProfile -File scripts/test-supervisor.ps1 -SkipBuild
```

`test-supervisor.ps1` 检查父子探针、Job 清理、主进程退出、监管进程强杀及路径识别。LEP DLL 存在时还会执行 LEP 自检。GitHub Actions 执行基础构建和这些测试。

LEP 构建完成后，可运行原生测试和时区/语言自检。

```powershell
pwsh -NoProfile -File scripts/build-lep.ps1 -Test
pwsh -NoProfile -File scripts/test-lep.ps1
```

浏览器及 Claude 实测需要相应应用，以下命令会启动测试会话。`test-lep.ps1` 临时调整本地 INI，并在结束时恢复。

```powershell
pwsh -NoProfile -File scripts/test-lep.ps1 -RunChromium -RunClaude
```

更多参数见 `scripts/chromium-observations.ps1` 和 `scripts/claude-observations.ps1`。`electron-observations.ps1` 用于支持 CDP 的构建，历史测试中的 Claude Desktop 会屏蔽 CDP。

## 验证与限制

| 范围 | 已有证据与边界 |
| --- | --- |
| 原生时区与语言 | UTC/en-US、Tokyo/ja-JP 父子探针通过，包括动态时区、代码页、UI/首选语言和 Job 成员关系 |
| Chromium 页面与 Worker | Edge 代理报告配置的 `Intl` 时区、区域及 `navigator` 语言 |
| Claude 进程树 | 历史测试的 7 个进程位于 Job 内，具备注入记录，停止及退出清理通过 |
| 线程及 WinRT 语言 | 后续补丁通过匹配架构的 x86/x64 独立探针，最终补丁尚待运行中的 Claude UI 复测 |
| 宿主残余值 | system locale/LCID、原生 `GetLocaleInfoEx` 日期格式仍可能返回宿主值 |
| 注入失败处理 | LEP 上游子进程注入为 fail-open，ClaudeProtect 通过预检及周期检查检测并停止受影响会话。短生命周期进程可能逃过运行中检查，外部辅助进程的注入失败仅记录 |

Claude 的 renderer JavaScript 历史观测受 CDP 屏蔽限制，页面与 Worker 的结果来自 Edge 代理。具体版本、方法和原始记录见[历史验证报告](docs/lep-verification-2026-10-29.md)、[语言补丁验证](docs/fixes-2026-11-02.md)和[原始记录索引](docs/verification/README.md)。

## 开发与维护

| 目录 | 内容 |
| --- | --- |
| `src/` | Win32 托盘、配置、LEP 适配、Job 生命周期和原生探针 |
| `scripts/` | 构建、测试、诊断和清理脚本 |
| `tests/` | 配置测试及独立语言探针启动器 |
| `patches/lep/` | 相对固定 LEP 版本的完整集成补丁 |
| `LocaleEmulatorPlus-Core/` | 上游 Git 子模块 |
| `docs/` | 架构、实现和验证证据 |

清理生成的缓存、测试目录和日志前，请关闭本仓库运行的 ClaudeProtect 与探针。

```powershell
pwsh -NoProfile -File scripts/clean.ps1 -WhatIf
pwsh -NoProfile -File scripts/clean.ps1
```

清理脚本保留运行二进制、配置及其备份、SDK overlay。贡献说明见 [CONTRIBUTING.md](CONTRIBUTING.md)，安全问题报告见 [SECURITY.md](SECURITY.md)。

## 许可证与致谢

ClaudeProtect 自有代码采用 **GPL-3.0-only**，完整条款见 [LICENSE](LICENSE)。

LEP 来自 [julixian/LocaleEmulatorPlus-Core](https://github.com/julixian/LocaleEmulatorPlus-Core)，源自 [Locale Emulator](https://github.com/xupefei/Locale-Emulator)。相关依赖及 LEP 补丁保留其原有许可要求，详情见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

本项目由社区独立开发。Claude 和 Claude Desktop 的相关商标归其各自权利人所有。
