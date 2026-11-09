# 原始验证记录

这里只保留用于文档结论的原始观测；运行日志和可重新生成的测试目录由 `scripts/clean.ps1` 清理。

| 文件 | 内容 |
| --- | --- |
| [2026-10-29/chromium-tokyo-ja-jp.json](2026-10-29/chromium-tokyo-ja-jp.json) | Edge 代理的页面及 Worker 时区/语言观测 |
| [2026-11-02/la-en-us-x64.jsonl](2026-11-02/la-en-us-x64.jsonl) | x64 父/子探针：洛杉矶、en-US、WinRT 与语言向量检查 |
| [2026-11-02/tokyo-ja-jp-x64.jsonl](2026-11-02/tokyo-ja-jp-x64.jsonl) | x64 父/子探针：东京、ja-JP |
| [2026-11-02/la-en-us-x86.jsonl](2026-11-02/la-en-us-x86.jsonl) | 匹配 x86 加载器的父/子探针：洛杉矶、en-US |
| [2026-11-02/host.json](2026-11-02/host.json) | 宿主原生观测：时区和语言保持原值 |

JSONL 每行对应一份完整 JSON 报告。父进程报告保留其子进程结果。记录包含历史进程 ID 和日志文件名，不要求对应的临时进程或日志仍存在。

验证范围与限制见 [修复记录](../fixes-2026-11-02.md) 和 [历史验证报告](../lep-verification-2026-10-29.md)。这些原生/代理观测不能替代最终 Claude 界面的直接观测。
