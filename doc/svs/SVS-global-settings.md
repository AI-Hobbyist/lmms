# SVS / VST 设置分类与网格验证

日期：2026-10-06。范围限于本轮用户要求；实机操作体验为 MANUAL/PENDING，不阻塞提交。

## 实现

- Settings 新增独立 VST 和 SVS 分类。VST 窗口嵌入、旧工程目录和扫描目录移入 VST；配置键和扫描逻辑沿用原实现。
- 分类图标与其他设置分类按 48×48 加载。VST 使用按用户参考图绘制的白色 `setup_vst.svg`；SVS 使用白色 `svs_track.svg`。Theme QSS 包含图标路径和尺寸。
- SVS 后端选择 CPU / DirectML / LibTorch / Vulkan。CPU 时设备栏禁用并显示 CPU；其他项可选择但标注 Coming soon，仅验证设备栏启用和持久化。说明明确仅 AI 声库使用。实际合成输入始终声明 CPU，无 GPU 加速实现。
- 系统设备由 DXGI 枚举，排除软件适配器。本机枚举 CPU、AMD Radeon 780M Graphics、NVIDIA GeForce RTX 5060 Laptop GPU，与独立 WMI 查询一致。设备消失时回退 CPU；不包含测试用虚构 GPU。
- 各实际引擎按名称分栏，根据声明显示 AI / Traditional concatenation。SVSExample 是程序生成波形的测试引擎，诚实标为 Non-AI example；未声明类型明确显示 Type not declared。
- 额外 AI 示例页仅展示分类效果，包含渲染步数滑块 1–100，默认 20，实时数值显示，确认后持久化；不注册虚构合成引擎。
- 实际引擎可异步声明全局参数。SVSExample 提供 `example.outputGain`，确认后持久化，传入不可变渲染快照和缓存身份，更新相关 SVS clip。测试确认 0.5 与 1.0 的波形样本幅度为 1:2。
- SDK ABI 1.2 追加可选 `query_engine_settings`，提供 C++ 能力探测和 RAII 返回值包装；支持名称、引擎类型及全局参数声明。保持 ABI 主版本和必需前缀。详见 [EngineSettings.md](../../sdk/svs/docs/EngineSettings.md)。参数修改后的页面刷新排队执行，避免在 setter 执行期间替换自身。
- SVS 音符区和参数区增加独立拍线 / 小节线主题属性，使用原钢琴窗颜色 `#2d6b45` / `#42a065`。对齐工程拍号和 clip 偏移，支持滚动、粗量化和拍号更新；细分网格保持灰色。

## 自动验证

所有构建、测试、SDK 安装及开发版同步均使用前台 PowerShell，先加载 `Enter-LmmsEnvironment.ps1`，输出通过 Tee-Object 写入 build.log，并立即检查 LASTEXITCODE。

- Release 主程序和 SVSIntegrationTest 构建通过：[构建日志](validation/SVS-settings-build.log)。
- 最终原生 Windows Qt 回归：**59 passed / 0 failed**，包括设备启禁、引擎类型、AI 步数范围和持久化、真实输出增益、VST 控件归属、网格像素和 SDK 协商：[报告](validation/SVS-settings-regression.txt)。
- SDK 脱离 LMMS/Qt 独立构建，ABI 1.2 完整 SVSExample 和 ABI 1.0 最小插件均通过 conformance：[完整插件](validation/SVS-settings-sdk-full-conformance.log)、[旧 ABI 插件](validation/SVS-settings-sdk-minimal-conformance.log)。
- 共享 `SVSSDK_ROOT` 安装已更新 ABI 1.2，已核对安装的 svs.h 与源码哈希一致；所有 agent 可通过环境脚本刷新既有持久化 SDK 变量。
- 开发目录 `build/svs-lmms-clean-install` 同步 lmms.exe、lmms-svs-aligned.exe、测试程序、SVSExample DLL/manifest、默认主题和 SVG；逐文件哈希核对：[同步日志](validation/SVS-settings-deployment.log)。

初次完整回归中，设置窗口测试遇到输入法扩展访问异常；直接复现堆栈含 ImeExtension / PcDownloadPicIconAndNotify。测试进程设置 `QT_IM_MODULE=none` 后完成最终回归，平台仍为 `windows`、窗口真实显示。该设置只用于测试进程，开发版输入法支持未禁用；真实第三方输入法体验留作人工验收，不修改相邻输入法系统。

## 实窗证据

- [引擎参数分类](validation/SVS-settings-window.png)
- [AI 示例和放大的分类图标](validation/SVS-settings-ai-example.png)
- [独立 VST 分类](validation/SVS-settings-vst-window.png)
- [SVS 拍线 / 小节线](validation/SVS-grid-native-window.png)
- [真实设备列表](validation/SVS-settings-devices.json)

本轮未使用 Computer Use、offscreen 或原安装版 LMMS。最终实机观感和操作验收：MANUAL/PENDING。
