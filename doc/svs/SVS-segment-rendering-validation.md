# SVS 空拍分段增量渲染验收

状态：PASS。A0～A4 已分别提交推送，本次仅实施用户新增的空拍分段、当前段/总段进度、参数配色和 SDK 补充。DirectML B 阶段未实施。

## 实现

- 可选 `synthesis.segmented={split:"rests",version:1,paddingSeconds:0.65}` 声明启用分段。连续音符归为同段，空拍或显式休止分隔；缺少声明或旧 `false` 保持原调度。上下文长度限制 0～2 秒。
- 每段保留原始 tick、工程原点和冻结参数，曲线按上下文切片并保留精确边界/切线。身份包含实际输入依赖，沿用 `cache/SVS/<engine>`，DiffSinger 为 `cache/SVS/DiffSinger`，WAV 与 tensor 均使用完整 SHA256。
- 局部音符/曲线修改即时移除失效段，其他段 PCM 保留并直接复用；内存复用不依赖磁盘缓存命中。共同参数、声库、工程位置/offset/tempo 等依赖变化失效相应段。
- PCM 按绝对时间合并，空拍中重叠的前后余音在空拍中点切换归属。缺少的段以静音表示，反馈音素/读音/音高/只读曲线按位置合并。部分结果只用于预览，导出必须拿到完整冻结结果。
- Song Editor 显示 `Rendering 当前段/总段`。取消和失败保留其他已验证段，失败诊断包含 `segment=i/n`，旧请求仍通过原 revision/request gate 丢弃。
- DiffSinger 可调曲线和标签使用不同颜色；offset 为同色系浅色，配对只读参考结果使用基础颜色。其他引擎未声明颜色时使用宿主参数配色。
- SDK 文档补充分段能力、参数颜色、上下文/缓存依赖、进度和完整导出约定；Conformance 校验可选字段，六包 ABI 检查声明。无必需 C ABI 函数或字段变更，ABI 1.0～1.3 保持兼容。

## 自动结果

| 验收 | 结果与证据 |
| --- | --- |
| 原位完整 Release 构建 | PASS，`validation/SVS-segments-final-build.log`；最终发布时序补修见 `SVS-segments-publication-fix-build.log`。58 个普通 DLL 原位存在，主程序 `build/Release/lmms.exe`，原位文件清单与 SHA 见 `SVS-segments-release-deployment.json` / `SVS-segments-release-package.json` |
| 完整 SVS 回归 | PASS，70 passed / 0 failed / 2 GUI-only skipped，`validation/SVS-segments-full-QtTest.txt` |
| 三段真实 CPU | PASS，第一段音符/局部 breathiness 曲线编辑只更新该段，其他两段 cached=true 且 PCM 相同；共同 gender 改动全部失效；取消保留有效段；无效音素的失败仅影响第一段；冻结三段导出完整并与修改前 PCM 相同 |
| 合并/拆分与时间 | PASS，计划用例检查关闭空拍合并、显式休止、非零 position/offset、PCM 对齐和缺段静音；完整回归复验 tempo、导出、恢复缓存及生命周期 |
| 真实 Windows 窗口 | PASS，5 passed / 0 failed / 0 skipped，`validation/SVS-segments-final-native-QtTest.txt`。进度、参数颜色、原 A4 显示选项和实际 Release/空目录启动通过 |
| 独立 SDK 构建/安装 | PASS，`validation/SVS-segments-sdk-build.log` / `SVS-segments-sdk-install.log`，使用原 `build/svs-sdk-external` 和 `build/svs-sdk-install` |
| 安装后旧示例与六包 ABI | PASS，`SVS-segments-sdk-full.log` / `SVS-segments-sdk-minimal.log` / `SVS-segments-sdk-ABI.log`，有限 PCM、ownership/cancel/origin、ABI 1.0～1.3 和六包分段/颜色声明 |

GUI 均使用真实 Windows Qt 窗口和 QScreen 截图，没有 offscreen。`SVS-segments-native-progress.png` 清楚显示 Song Editor 的 Rendering 1/3；`SVS-segments-native-colors.png` 显示输入和对应只读曲线、彩色参数标签。两图含私人声库 artwork，只保留本机，不提交。官方四个初始轨道见 `SVS-native-default-project.png`。

首次完整回归暴露单段未标记音频被提前发布，以及新导出测试误收集其他用例轨道。已修正最终发布顺序和测试取样范围，保留 `SVS-segments-full-first-failure.*`，随后全套通过；没有改无关 Song/VST 文件。测试使用有界的独立 gender 输入避免历史磁盘命中让“本次实际推理”断言失真。

用户随后要求英文主 README 对比表、AI 辅助开发说明、独立分支同步上游和全量覆盖包，另行完成。默认工程空白来自开发个人 `templates/default.mpt` 覆盖；已备份该文件并复制官方模板，未修改生产初始化逻辑。详细路径/哈希见 `SVS-default-template-restore.json`。主观听感仍为 MANUAL/PENDING，A3 试听保留。
