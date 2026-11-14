# 贡献指南

欢迎通过 GitHub Issues 报告问题，通过 Pull Request 提交修复。贡献的自有代码按本仓库 GPL-3.0-only 许可证提供，第三方代码和 LEP 修改需保留适用的原有许可。

## 本地开发

按 [README](README.md) 初始化子模块并安装工具链。修改主程序后运行以下检查。

```powershell
pwsh -NoProfile -File scripts/build.ps1
pwsh -NoProfile -File scripts/test-settings.ps1
pwsh -NoProfile -File scripts/test-supervisor.ps1 -SkipBuild
pwsh -NoProfile -File scripts/test-multi-session.ps1
node tests/ui-dual-session.js
```

双会话测试需要已构建的 LEP 和 WebView2 SDK，运行前需退出 Desktop 和 Science。前端控制测试需要 Node.js 18 或更新版本。

LEP 修改使用独立补丁维护，工作流程见 [patches/lep](patches/lep/README.md)。涉及钩子、ABI 或注入流程的改动还需要运行上游原生测试和相关观测脚本，并记录验证版本及结果。

## 清理生成文件

关闭本仓库运行的 ClaudeProtect 与探针后，先预览再执行清理。

```powershell
pwsh -NoProfile -File scripts/clean.ps1 -WhatIf
pwsh -NoProfile -File scripts/clean.ps1
```

清理脚本保留运行二进制、配置及其备份、SDK overlay。

## 修改约定

- 保持用户模式和只读宿主设置的边界，使用 Job Object 管理自己启动的进程。
- 通过安装路径识别 Claude Desktop，保留对 Claude Code 的区分。
- 保留 Chromium sandbox 和注入失败检测。
- 将 LEP 源码修改与监管程序分开维护，更新可复现的集成补丁。
- 根据实际测试描述验证范围，区分原生探针、浏览器代理和 Claude 的直接观测。

## 提交前

- 说明问题、修改范围和已运行的检查。
- 检查 `git diff --check`，确认提交包含所需的新文件。
- 通过 `git status --short --untracked-files=all` 检查发布内容。
- 保持 `build/`、运行配置、日志、转储及本地凭据在 Git 忽略范围内。
- 上传诊断资料前清理用户名、私有路径、访问令牌和命令行中的敏感参数。

GitHub Actions 覆盖主程序构建、配置及基础监管测试。LEP、Chromium 和 Claude 的集成验证需要在具备相应依赖的 Windows 环境中运行。
