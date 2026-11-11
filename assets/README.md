# 托盘图标

`claude-stopped.svg` 为灰色待机图标，`claude-running.svg` 为橙色 Claude 图形及右下角蓝色盾牌。对应 ICO 包含 16、20、24、32、40、48、64、128、256 像素透明图像，随 Windows 资源嵌入程序。

共用前端同时嵌入两份 SVG，页面顶部随受监管会话的状态切换：运行时橙色带蓝盾，停止时灰色。Windows 托盘使用对应 ICO 独立切换。macOS 准备环境和单独自检期间保持灰色。

Claude 图形取自 Simple Icons 的 Claude SVG，按 CC0-1.0 提供。原始图形来源和许可见 [第三方说明](../THIRD_PARTY_NOTICES.md)。盾牌和配色由本项目调整。

常规 C++ 构建直接使用 ICO。修改 SVG 后，可使用 Node.js 和 sharp 重新生成 ICO。

```powershell
npm install --prefix build/icon-tools --no-save sharp
$env:NODE_PATH = Join-Path $PWD 'build/icon-tools/node_modules'
node scripts/generate-icons.cjs
```
