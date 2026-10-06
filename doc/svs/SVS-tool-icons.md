# SVS 白色工具及轨道 SVG 图标

- 七个钢琴窗工具：选择、铅笔、音高画笔、锚点、平滑、直线、擦除，分别使用 `svs_tool_select/pencil/pitch/anchor/smooth/line/erase.svg`。
- SVS 轨道无头像时使用 `svs_track.svg`；实际歌手头像仍优先显示。
- 全部图标为 24×24、白色、透明背景的独立 SVG，无字体或外部资源依赖。
- 工具按钮保留名称提示及原有快捷键、切换行为；默认主题 CSS 显式配置七个 SVG 路径。C++ 使用主题资源路径，并回退到默认主题 SVG。
- Release 编译、原生 Windows Qt 的主题资源/工具切换/窗口生命周期测试通过。测试检查八个 SVG 可解码、尺寸、白色像素与透明区域，以及按钮图标和提示。
- 原生窗口截图：`validation/SVS-icons-native-window.png`。测试结束关闭窗口；未使用 offscreen 或 Computer Use。
- 开发安装同步二进制、CSS 和八个 SVG，并校验文件哈希。主观实机体验：MANUAL/PENDING。

## 关闭回嵌及工具栏入口修复（2026-10-06）

- SVS 创建宿主子窗时遗漏 `WindowCloseButtonHint`，弹出后 Windows 原生关闭按钮被禁用。仅在 SVS 接入处补齐标志，沿用现有关闭回嵌处理。
- Song Editor 的“添加 SVS 轨道”入口此前仍使用 `add_sample_track`，现改为白色 `svs_track.svg`。
- 原生窗口回归先复现关闭标志缺失，再验证 Windows 系统菜单关闭项可用，发送 `WM_SYSCOMMAND/SC_CLOSE` 后成功回嵌；三种 detachbehavior 下重复弹出关闭均保留选择。
- 验证入口图标与主题 SVG 一致；Release 编译和两项针对性测试通过（含初始化/清理共 4 passed）。截图及前后测试记录见 `validation/SVS-close-button-*`。开发安装同步并校验哈希，测试窗口已关闭。
