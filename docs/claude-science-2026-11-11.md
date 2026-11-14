# Claude Science 验证记录

2026-11-11 在 Windows x64 上验证 Claude Science 启动、注入证据、进程清理，以及单个 ClaudeProtect 实例同时监管 Desktop 和 Science。

## 启动路径

Science 0.1.72 的稳定入口为 `%LOCALAPPDATA%/Programs/ClaudeScience/claude-science.exe`。其 Electron 42.10.0 位于用户目录的 `.claude-science/electron/42.10.0`，应用 shell 位于 `.claude-science/runtime/0.1.72-release/shell`。这些版本仅为本次观察值，发现逻辑使用稳定入口。

启动器的 Windows 桌面交接要求参数为 `serve`，并要求子进程环境中的 `OPERON_WIN_DESKTOP_LAUNCH` 与实际父 PID 一致。该协议来自已安装启动器的实现。ClaudeProtect 在挂起创建前设置该子进程环境变量，随后分配 Job 并恢复进程。此交接依赖 Science 的实现，后续版本需要回归验证。普通 CLI `serve` 未激活本次观察版本的 Electron 桌面路径。

## 已验证

- 配置默认兼容旧版 Desktop，Science 设置可保存并自动发现启动器。应用路径分类区分 Desktop、Science 和 npm Claude Code。
- 已有 Science 实例时拒绝启动，保留现有实例。
- 洛杉矶/en-US 和东京/ja-JP 的父子原生预检通过。
- 两组 Science 实机采样均包含启动器、Electron 主进程、GPU、utility 和两个 renderer。六个应用进程均有 Job 成员和 LEP 初始化或注入日志证据。
- en-US 组 utility/renderer 参数为 `--lang=en-US`。ja-JP 组对应参数为 `--lang=ja`。renderer 保留 `--enable-sandbox`。
- 观察到正常的 Science 登录窗口。
- 正常 Stop 与监管进程终止后，Science 应用进程均清理为零。
- 探针进程树 Stop、主进程退出、监管进程终止和后台操作取消测试通过。

在已构建的运行目录中配置时区和语言，退出已有 Science 实例后，从仓库根目录运行以下采样命令。第一条使用 en-US 配置，第二条使用 ja-JP 配置并检查监管进程终止后的清理。

```powershell
pwsh -NoProfile -File scripts/claude-observations.ps1 -Application science -Seconds 60 -ExpectLang en-US
pwsh -NoProfile -File scripts/claude-observations.ps1 -Application science -Seconds 60 -ExpectLang ja-JP -KillSupervisor
```

## 验证边界

Science 内部 conda/Python 辅助进程曾出现缺少虚拟化证据的警告，目前沿用外部辅助进程处理方式。严格验证覆盖稳定启动器和独立目录中的 Science Electron 应用进程。尚未读取 Science renderer 的 `Intl`/Worker 时区结果，原生预检与注入日志构成本次时区验证证据。Science 登录后的工作流未测试。

## 单实例双应用演进

同一托盘进程现在拥有 Desktop 和 Science 两个独立 Session/Job。控制面板按应用发送启动、停止命令并分别显示状态与运行环境。后台预检与启动串行处理，已运行会话可持续运行。环境设置共用，两条启动器路径和 Desktop 参数独立保存，旧 INI 自动兼容。

`tests/multi-session.cpp` 通过与控制面板相同的回调验证双会话，同时覆盖独立停止、单个主进程退出、取消后台启动及系统退出清理。测试夹具的两棵进程树、严格注入验证均通过。使用真实安装的 Desktop 和 Science 再运行同一测试，也已通过。

```powershell
pwsh -NoProfile -File scripts/test-multi-session.ps1
node tests/ui-dual-session.js
```

生命周期测试沿用生产启动检查，运行前需退出已有 Desktop 和 Science 实例。

`node tests/ui-dual-session.js` 验证两套按钮状态、按应用路由消息，以及旧设置映射。

正式控制面板中的两套启动按钮也已实机点击验证。Desktop 主进程和 Science 启动器的父进程均为同一个 ClaudeProtect，Science 的独立 Electron 进程树已观察到。点击停止 Science 后，Desktop 会话继续运行，随后单独停止 Desktop，两个会话均回到待机。升级前后的 INI 哈希一致。
