# 第三方组件与许可

## LocaleEmulatorPlus-Core

- 上游项目为 [julixian/LocaleEmulatorPlus-Core](https://github.com/julixian/LocaleEmulatorPlus-Core)。
- 原始项目为 [xupefei/Locale-Emulator](https://github.com/xupefei/Locale-Emulator)。
- 固定提交为 `0b11f533ef426ab5606e12f91acd4b1c2f7e6bad`，存放在 `LocaleEmulatorPlus-Core/` Git 子模块中。
- ClaudeProtect 对该版本的修改完整保存在 `patches/lep/claude-protect.patch`，包括修改说明。
- `src/lep_abi.hpp` 根据上游 `include/LocaleEmulatorPlus.ixx` 的公开 ABI 声明整理，保留上游来源说明。

上游 `README_ORG.md` 将原始 Core 库源码标识为 LGPL-3.0，仓库提供 `LICENSE-LGPL.txt` 和 `LICENSE-GPL.txt`。子模块内的第三方文件继续遵循各自声明的许可与版权，LEP 补丁遵循所修改组件的适用许可。

LEP 的 LGPL 附加条款位于 [LICENSES/LGPL-3.0.txt](LICENSES/LGPL-3.0.txt)，配套 GPL v3 全文也收录在主仓库 [LICENSE](LICENSE) 中。ClaudeProtect 自有代码采用该 GPL v3 许可证。

## 源码与二进制分发

本仓库通过子模块引用上游，另行保存全部集成补丁。使用者需初始化子模块并应用补丁才能获得本项目使用的完整 LEP 源码。

分发 LEP DLL 或组合运行包时，应保留适用版权声明与许可证，并按相应条款提供确切版本的对应源码、修改及构建所需材料。构建脚本会将 LEP 的两份许可证复制到本地 `build/lep/`。源码 Git 引用与补丁需要随对应二进制版本一起维护。

LEP 上游仓库还包含历史工具链材料，适用各自的分发条款。ClaudeProtect 的本地构建输出与工具链不纳入主仓库提交。
