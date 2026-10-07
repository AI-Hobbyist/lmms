# SVS 音符／音素拉伸与只读参照曲线

2026-10-07。按 [TuneLab 实测记录](TuneLab-note-phoneme-stretch-2026-10-07.md) 完成本轮独立 SVS 编辑器修正。未改普通 PianoRoll、InstrumentTrack、Mixer 或音频框架。

## 拉伸操作

- 音符首尾使用鼠标相对边缘的偏移，默认量化、Alt 自由。首端保持尾端，尾端保持首端；延长覆盖相邻音符时修剪相邻边界，缩短留下间隔。手势预览不写模型，松开一次提交，撤销恢复原数据。
- 36px 音素条上半区处理音符起点／共享边界；共享边界同步伸缩两音符，外侧端点保持。默认自由、Alt 量化。
- 下半区处理音素起点；末端没有独立音素尾部拖动，末尾线转到音符尾端。前置辅音／元音交界可平移刚性前缀并重新分配相邻元音；跨音符元音／辅音交界调整两侧时长，音符矩形保持。
- 音符伸缩同步已有手动音素；原先填满的末元音继续填充到音符尾端或下一前置辅音，明确分离的尾部保持。保留额外 JSON 字段、能力声明的最短时长与最大提前量。Escape、工具切换、外部编辑或 tempo 变化取消预览。

这里实现的是本轮实测的边界和刚性前置辅音行为，未引入 TuneLab 的任意多组件加权求解器或运行依赖。后续复杂内部音素权重需求需另行确认，不扩展当前范围。

## 只读曲线

移除参数按钮前的 Read-only 标签，保留输入／结果竖线分界和横向滚动。只读参数曲线按插件颜色绘制 64/255 alpha 填充，覆盖缺口分别闭合，不连接缺口。可调曲线保持原显示和编辑语义。

每个只读参数按钮右键独立显示／隐藏，包括当前选中的结果；左键选择并显示。关闭后按钮变淡。状态按参数 ID 存入既有 editorState.lanes，不采用全局开关，也不改变合成输入或 PCM。

SVSExample full 新增两条实际有值的只读示例：紫色 Rendered level 为每 20ms 双声道 RMS，青色 Rendered peak 为同区间峰值。两者来自合成 PCM，声明范围为 0–1；原 Rendered energy 保留。返回已有 feedback.curves 格式，SDK 格式文档已补充，无公开 C ABI 变更、无本机图片依赖。

## 验证

- 前台 PowerShell + Tee-Object + build.log + LASTEXITCODE 构建通过；主程序 `build/Release/lmms.exe`，SVSExample `build/svs/SVSExample/SVSExample.dll`。
- 已启用的原生插件目标在既有 `build/Release/plugins` 重新链接，包括支持库；没有新增部署目录。
- Windows 原生完整 SVS 回归：61 passed / 0 failed / 0 skipped。
- 开发主题 GuiApplication 原生专项：4 passed / 0 failed / 0 skipped。覆盖八种音符首尾方向组合、四类音素起点／交界、共享边界、末端、Alt、取消、撤销和未知字段；只读专项覆盖两条实际数值反馈、单独显隐、选中结果隐藏、持久化、只读编辑门禁及 PCM 不变。
- 实际屏幕截图已检查：半透明填充和不同颜色可区分，关闭一条后另一条仍显示；没有方框字体。测试窗口已关闭。未使用 offscreen、Computer Use、TuneLab 或原安装版 LMMS。

证据：validation/SVS-stretch-reference-final-build.log、SVS-stretch-reference-regression.txt、SVS-stretch-reference-native.txt、SVS-reference-native-on.png、SVS-reference-native-off.png、SVS-stretch-native-window.png。

用户最终布局／操作手感验收：MANUAL/PENDING，不阻塞提交。既有原生测试加载其他插件的诊断、journal ID 重复和未运行 JACK server 的信息记录为 follow-up；本轮不修改相邻插件或音频系统。
