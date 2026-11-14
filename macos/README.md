# ClaudeProtect macOS

当前版本 **0.3.0** 使用环境变量方案，直接启动正式安装的 Claude，保留官方签名。控制面板沿用 Windows 版界面设计，由 WKWebView 承载。HTML、CSS 和 JavaScript 独立维护在 `macos/ui/`，不修改 Windows 的 `ui/`、打包脚本或根 README。

## 使用

发行包为 `ClaudeProtect-0.3.0-macOS-arm64.dmg`，适用于 Apple Silicon（M 系列）与 macOS 13+。打开 DMG 后将 App 拖入 Applications。发行 App 内置 Python，不需要 Homebrew、Xcode 或本仓库；仍需自行安装官方 Claude。当前构建仅使用本地临时签名，未经过 Developer ID 签名或 Apple 公证。

发行 App 的设置、日志和独立资料位于 `~/Library/Application Support/ClaudeProtect/`；下文的 `build/macos/` 资料路径适用于源码开发版。DMG 不包含开发机的设置、模型密钥、聊天或登录资料。旧开发版的资料仍保留在原目录，不会自动打包或覆盖已有发行版资料。

发行 App 的页面顶部、Dock 和菜单栏图标均随 Claude 会话状态切换：运行时为橙色带蓝盾，停止后恢复灰色。Finder 中的 App 文件图标保持灰色。

双击 `build/macos/ClaudeProtect.app`，保存设置后选择“启动 Claude”。请先退出其他 Claude。顶部图标在会话运行时变亮，停止后恢复灰色；“停止会话”或退出控制程序会清理已跟踪的会话进程。关闭窗口后可从 Dock 或菜单栏重新打开。

启动器设置 `TZ`、`LANG`、`LC_ALL`，并给 Claude 主程序传入 `--lang`、`-AppleLanguages`、`-AppleLocale`。不注入动态库、不修改 ASAR、不重新签名 Claude，也不改变系统时区和全局语言。

资料继续保存在 `build/macos/profile/` 和 `profile-3p/`，不使用原版 Claude 日常登录资料。开启 CC Switch 同步时只复制所选 Desktop 模型配置，不修改 CLI，不复制账号登录资料；关闭后沿用独立目录中已有配置。路由凭据失效时会明确报错。

## 覆盖范围

实际通过官方 Claude Desktop 调用 Bash 的测试确认：`TZ/LANG/LC_ALL` 正确继承，Bash 和测试原生子进程的时区及冬夏令时正确。原生探针的 Unix/C 区域为 `en_US.UTF-8`，但 Foundation 区域和首选语言仍为 `zh_CN / zh-Hans-CN`。给探针单独传入 Apple 参数后才变成 `en_US / en-US`。主程序参数不会自动成为子进程参数。

因此，**原生语言区域未完全覆盖**。本模式不宣称任意子进程均受强制约束，也未验证未修改 Claude 页面内部的 Intl、Date、navigator。界面的“运行中”表示启动和进程跟踪，不代表这些接口全部验证通过。

启动前预检和“运行环境自检”只运行独立探针，检查启动器环境的时区、UTF-8 区域和冬夏令时。探针观察到的原生语言区域保存在会话记录中，不要求它与 LANG 一致。注入验证、注入等待和注入心跳设置已隐藏；旧配置中的这些字段仅保留兼容，不再生效。

监管通过 libproc 跟踪进程身份和已观察到的父子关系，发送信号前复核启动时间。不会按官方应用路径批量终止其他会话。控制程序正常退出后监管器清理会话；监管器自身被强制杀死时，没有注入心跳来保证所有进程退出。该机制不等同于 Windows Job Object。

## 设置和构建

设置位于 `build/macos/ClaudeProtect.json`，备份为 `.json.bak`，更改下次启动生效。可设置 IANA 时区、语言区域、启动预检、进程轮询、CC Switch 同步、安装路径和附加参数。附加参数按引号解析，不经 shell 执行，不能覆盖统一管理的语言或资料目录参数。

```sh
node macos/tests/ui-session.js
python3 -B -m unittest discover -s macos -p test_environment_mode.py
bash macos/build.sh
open build/macos/ClaudeProtect.app
# 终端运行，Ctrl+C 停止
python3 macos/supervisor.py
# 仅检查启动器环境
python3 macos/supervisor.py --selftest
```

源码开发版构建需要 Xcode 命令行工具及 Python。构建独立发行包时，先从 `macos/package.py` 中固定的官方 URL 下载 Python 运行时至 `build/macos/downloads/`，再执行 `python3 macos/package.py`。打包脚本强制检查 SHA-256，内置后端、Python、许可证与图标，签名并验证 App 后生成 DMG 和 `.sha256` 文件。Python 运行时的原始许可声明保存在 `macos/runtime-licenses/`。

历史的 `Claude.app`、`runtime/` 和备份副本不再被默认启动流程使用；不要直接打开这些旧副本。历史注入实验源码不属于当前发行包。

本次模式切换前的实际 Desktop 测试记录位于 `build/macos/original-no-injection-1794375104721412000/desktop-observation.json`；该记录不等同于新版控制面板的完整验收。

模式切换后的检查已通过：Python/JavaScript 语法、App 编译、3 项环境隔离与进程归属回归、启动器独立预检、默认监管流程的 8 秒官方 Claude 启动/停止。最终会话状态为 stopped，未遗留官方 Claude 进程，官方 Claude 和新版 ClaudeProtect 的深度签名校验均通过。本次未重新执行模型对话或完整界面交互验收。
