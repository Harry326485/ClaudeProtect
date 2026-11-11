# ClaudeProtect

> macOS：适配采用环境变量方案，直接启动官方安装的 Claude，保留签名和独立资料目录，见 [macOS 使用与验证说明](macos/README.md)。原生语言区域未完全覆盖，进程监管采用用户态实现。

Windows x64 托盘程序，用于在指定时区和语言环境下启动 Claude Desktop，并监管它的进程树。

ClaudeProtect 使用 LocaleEmulatorPlus-Core（LEP）提供进程内时区、区域和语言虚拟化，通过 Windows Job Object 管理 Claude 及其子进程。默认配置为洛杉矶时区和美式英语，支持夏令时。


![ClaudeProtect 简体中文控制面板概览](docs/images/claudeprotect-overview.png)

## 功能

- 托盘启动、停止 Claude Desktop，WebView2 控制面板。
- 动态发现 Claude Desktop 安装路径。
- 配置 Windows 时区、BCP-47 区域、代码页及首选语言。
- 默认严格验证，检测到受监管应用进程的注入失败时停止会话。
- 守护 Claude 运行，控制 Claude Desktop 生命周期。

## 环境要求

- Windows 10/11 x64 与 Microsoft Edge WebView2 Evergreen Runtime。
- MinGW-w64 的 x64 `g++` 和 `windres`，支持 C++17。已验证 GCC 8.1。
- 构建 LEP 需要 Python 3.10+、MSVC x64/x86 C++ 工具链和 Windows SDK。

请将 `git`、`g++`、`windres`、`python`、`pwsh` 加入 `PATH`。MSVC 和 SDK 由脚本自动发现，也可通过构建参数指定。

## 从源码构建

使用 Git 克隆本仓库，在仓库根目录运行以下命令。

```powershell
git submodule update --init --recursive
pwsh -NoProfile -File scripts/build.ps1 -BuildLep
```

补丁与上游源码分别维护，说明见[补丁文档](patches/lep/README.md)。

也可以分别构建。

```powershell
pwsh -NoProfile -File scripts/build-lep.ps1
pwsh -NoProfile -File scripts/build.ps1
```

首次构建会下载固定版本的官方 WebView2 SDK（1.0.2903.40），并校验 SHA-256。SDK 缓存位于 `build/deps/`，后续构建可复用缓存。HTML、CSS、JavaScript 和图标在构建时嵌入 EXE，无需 Node.js 或前端构建工具。

生成的运行目录如下。

```text
build/
  ClaudeProtect.exe
  EnvironmentProbe.exe
  WebView2Loader.dll
  WebView2-LICENSE.txt
  WebView2-NOTICE.txt
  picojson-LICENSE.txt
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
2. 运行 `build\ClaudeProtect.exe` 打开控制面板，选择 **设置**。
3. 在 **通用 / 保护策略 / 高级 / 启动与路径** 中修改设置并保存。保存前会校验配置。
4. 选择 **启动 Claude**。
5. 选择 **停止会话** 结束会话，或通过 **退出 ClaudeProtect** 关闭托盘程序并清理受监管进程。

配置和日志生成在可执行文件所在目录，因此该目录需要当前用户具有写入权限。

关闭控制面板会释放 WebView2 控制器与环境，并继续在托盘运行。再次点击托盘图标可重开窗口。WebView2 用户数据缓存在 `%LOCALAPPDATA%/ClaudeProtect/WebView2/`。

### 配置

`ClaudeProtect.ini` 在首次运行时生成，修改在下次启动会话时生效。`Executable` 留空时自动发现安装路径。日常设置可通过图形化设置页修改，无需编辑 INI。

```ini
[ClaudeProtect]
Executable=
Timezone=Pacific Standard Time
Locale=en-US
RegistryRedirectionMode=2
HookUILanguageMode=2
VerifyInjection=strict
VerifyGraceSeconds=8
Preflight=auto
ExtraArguments=
```

## 开发与维护

| 目录 | 内容 |
| --- | --- |
| `src/` | Win32 托盘、配置、LEP 适配、Job 生命周期和原生探针 |
| `ui/` | 嵌入 EXE 的简体中文 WebView2 界面 |
| `third_party/picojson/` | JSON 通信解析器及原始许可声明 |
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

清理脚本保留运行二进制、配置及其备份、SDK overlay。贡献说明见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证与致谢

ClaudeProtect 自有代码采用 **GPL-3.0-only**，完整条款见 [LICENSE](LICENSE)。

LEP 来自 [julixian/LocaleEmulatorPlus-Core](https://github.com/julixian/LocaleEmulatorPlus-Core)，源自 [Locale Emulator](https://github.com/xupefei/Locale-Emulator)。相关依赖及 LEP 补丁保留其原有许可要求，详情见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

本项目由社区独立开发。Claude 和 Claude Desktop 的相关商标归其各自权利人所有。
