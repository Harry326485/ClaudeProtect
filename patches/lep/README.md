# LEP 集成补丁

子模块来源为 [LocaleEmulatorPlus-Core](https://github.com/julixian/LocaleEmulatorPlus-Core)，固定提交为 `0b11f533ef426ab5606e12f91acd4b1c2f7e6bad`。

`claude-protect.patch` 保存 ClaudeProtect 所需的全部 LEP 本地改动，包括新增源文件和集成说明。补丁内的修改日期分别记录在集成说明中，主要改动覆盖动态时区、ACP/OEMCP、UI/首选语言、线程语言和 WinRT 语言接口，包含相应构建输入。公共 LEPB v4 布局保持一致。

## 应用

```powershell
git submodule update --init --recursive
pwsh -NoProfile -File scripts/prepare-lep.ps1
```

`build-lep.ps1` 会自动调用准备脚本。脚本检查固定提交，完整补丁已应用时直接成功，存在冲突或部分应用时停止并提示检查。主仓库通过 `.gitmodules` 的 `ignore = dirty` 忽略应用补丁后的子模块工作区状态，固定提交引用仍受跟踪。检查本地 LEP 改动时使用 `git -C LocaleEmulatorPlus-Core status` 和 `git -C LocaleEmulatorPlus-Core diff`。

## 维护

修改 LEP 前先阅读子模块的 `AGENTS.md`。提交主仓库时，更新这里的补丁，使其包含相对固定提交的全部修改及新增文件。使用 `git diff` 导出时，需要将新增文件纳入补丁，否则克隆者会缺少源文件。

在干净的固定版本上验证补丁可应用、可重复执行准备脚本，并运行 `scripts/build-lep.ps1 -Test` 及相关探针。验证方法与适用边界记录在主仓库 `docs/` 中。

补丁应用后的详细说明位于子模块的 `ai-docs/2026-10-29-claude-protect-integration.md` 和 `ai-docs/claude-language-virtualization.md`。

## 许可

LEP 修改遵循被修改组件的原有许可要求。依赖来源、许可证位置及分发说明见 [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。
