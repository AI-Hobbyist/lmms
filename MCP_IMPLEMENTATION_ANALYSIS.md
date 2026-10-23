# LMMS MCP 实现分析

分析日期：2026-10-23（Asia/Seoul）。源码基线：`44ccf432e4ba0e0a818e7a22a42a8624732c0141`，分支 `master`；M1～M3 实施后 HEAD 为 `2e1749e52`（2026-10-23）。

## 1. 结论与证据边界

LMMS 已有一套随应用进程运行的本机 HTTP MCP 服务器，并非仅有工具定义或计划。传输、协议、命令派发、脚本、导出任务、服务设置和测试入口均有实现。`AGENT_SUPPORT_PLAN.md` v0.7 将 A0～A4、B0～B4 标为已实施。

本次只进行源码与文档静态分析，没有启动服务、编译、执行测试或检查真实窗口。下文的“已实现”指找到代码路径；“历史通过”来自计划书 2026-10-04 的记录，不代表当前 HEAD 已重新验收。未发现需要在本次文档任务中修复的阻塞；本次不修改实现。

> **2026-10-23 更新（M4/M2b）**：M1（7 个只读发现工具）、M2（17 个 SVS 命令）、M2b（`svs.getCurves/setCurve`）、M3（9 个 SVC 命令）已实施并推送，当前运行时目录为 **194 个 MCP 工具 / 199 个注册命令**。M1～M3/M2b 的自动化测试与官方 Python SDK 发现回归已通过；真实 RVC/USVC 后端与真实窗口人工验收仍为后续项。原始静态分析内容保留在下方，数量与“尚无 svs/svc 工具”等结论以本节和 §4/§5/§6 的更新为准。

**用户约束：接手 Agent 不得修改已有实现。** 本报告列出的差异和后续方向仅供分析、验证与提出补充方案，不构成修改源码、既有测试、构建配置或产品行为的授权。执行规则见 [交接手册](AGENT_HANDOFF.md)。M1～M3 是用户明确授权后新增的实现；它们已成为新的既有实现基线，后续不得改写其工具名、参数或语义。

## 2. 实际实现边界

分析对象是 `include/agent/`、`src/agent/` 及其 LMMS 集成入口。`refs/tl_ref/TuneLab.Cli/Mcp/` 的 C# MCP 是参考项目代码，不能算作 LMMS 已接入的第二套服务。

| 层 | 已实现内容 | 主要证据 |
|---|---|---|
| 构建开关 | `WANT_AGENT` 控制命令 API，依赖开关 `WANT_AGENT_MCP` 控制服务；默认构建开启，运行时默认关闭 | `CMakeLists.txt`、`src/agent/CMakeLists.txt` |
| HTTP | `QTcpServer/QTcpSocket`，固定 IPv4 回环地址，HTTP/1.1 `/mcp`，JSON 响应，每次响应关闭连接 | `src/agent/mcp/HttpMcpServer.cpp` |
| 连接检查 | 16～512 个无空格可打印 ASCII 字符的 Bearer token；Host 校验；有 Origin 时检查本机来源 | 同上 `start/read` |
| MCP 协议 | 固定返回 `2025-11-25`，initialize、initialized、ping、工具发现和调用、通知 202、错误映射 | `src/agent/mcp/McpProtocol.cpp` |
| 工具定义 | 从命令描述符生成名称、描述、inputSchema 与只读/破坏性等 annotations | `src/agent/ToolRegistry.cpp` |
| 执行 | 主线程事件队列派发至 CommandBus；参数校验、事务、异常转错误、dryRun、撤销 | `McpProtocol::drain`、`src/agent/CommandBus.cpp` |
| 脚本 | `agent.runScript`，变量/循环/条件/断言/seed/内置脚本与批次回滚 | `src/agent/ScriptRunner.cpp`、`ScriptExpression.cpp` |
| 音频任务 | export.audio/status/cancel、render.preview；服务记录自己启动的任务，停服取消该任务 | `ExportCommands.cpp`、`HighLevelCommands.cpp`、`McpProtocol.cpp` |
| 应用接入 | 非渲染启动应用配置，引擎销毁前 shutdown；设置中启用、端口、状态、复制地址和即时应用 | `src/core/Engine.cpp`、`src/gui/modals/SetupDialog.cpp` |
| 客户端示例 | 官方 Python MCP SDK 连接、查询、编曲、预览与导出 | `data/agent/mcp_client.py` |

真实调用链：外部客户端 → HttpMcpServer 校验 HTTP/来源/token → McpProtocol 校验会话与工具 → 有界队列 → CommandBus → CoreCommands/ExportCommands/ScriptRunner/HighLevelCommands/DiscoveryCommands/SvsCommands/SvcCommands → LMMS 模型；返回 `content` 文本、`structuredContent:{ok,data,error?}` 和 `isError`。

## 3. 协议与生命周期细节

- 初始化生成 `MCP-Session-Id`，只维护一个本地客户端会话；再次 initialize 会替换旧会话并清空排队请求。多个 HTTP 连接不代表多个隔离会话。
- 工具使用前需要有效会话及 `notifications/initialized`。`tools/list` 不分页，传 cursor 返回参数错误；名称沿用 `song.setTempo` 等带点内部命令名，没有另造工具分组。
- tools/call 只允许已导出的工具；未知工具返回协议错误，执行失败返回工具结果 `isError:true`。
- 队列最多 16 项；检查排队中的重复请求 ID；`notifications/cancelled` 取消尚未执行的请求。它不是执行完成后的全局去重或写入幂等保证。
- 当前事务在主线程同步执行，HTTP 收发使用事件循环；不能据此保证长脚本期间 GUI 或网络始终及时响应。10 秒网络计时也不等于脚本执行的硬中断机制。
- stop 关闭监听和连接、清空队列、使旧回复失效；只取消该服务追踪的音频任务。断开 HTTP 连接本身不撤销已经提交的修改，不自动重试写入。
- GET/DELETE 返回 405；只返回 JSON，不提供独立 SSE 流。未声明 resources、prompts、sampling、MCP tasks 扩展。`export.*` 是业务工具任务，不是协议任务扩展。
- 没有 LMMS 内置聊天、模型客户端或本地审批面板；用户确认由外部客户端负责。服务没有按工具划分的账号权限层。

实际限额：请求头 16 KiB、请求体 4 MiB、连接 32 个、排队工具调用 16 个、响应 8 MiB、网络超时 10 秒。HTTP 要求 Content-Length，不支持 chunked 请求或 pipelining。

## 4. 目前开放的业务能力

命令手册 `data/agent/command-reference.md` 现有 **199 个命令标题**。ToolRegistry 过滤掉除 runScript 外的全部 `agent.*`，运行时 `tools/list` 为 **194 个 MCP 工具**（M1 后 166，M2 后 183，M2b 后 185，M3 后 194）。该数量由 `AgentHarness --tools/--manual` 于 2026-10-23 实际导出核对；`client_smoke.py` 的断言已同步为 194。

| 能力 | 范围与限制 |
|---|---|
| 工程/传输 | 速度、拍号、主音量/音高、工程加载/保存/清空、播放模式、定位、循环和 Clip 预览 |
| 轨道/乐器 | 建删/克隆/排序/属性、乐器加载与参数、预设、MIDI 端口、琶音/和弦堆叠；VST3 选择支持见 §6 |
| Clip/MIDI/采样 | 创建/复制/移动/长度/属性、批量音符、查询/量化/移调/人性化、采样路径/偏移/反转 |
| Pattern/自动化 | Pattern 管理与歌曲引用，目标绑定、节点/曲线、模型枚举与读写 |
| 混音/效果/控制器 | 通道与发送、效果链、L1 序列化参数、部分插件 L2 参数模型、控制器连接；不等于所有插件参数都有反射 |
| 导入/导出 | MIDI/Hydrogen/采样导入，MIDI 和音频导出，查询/取消；文件覆盖要求显式 overwrite，外部写入不属于工程撤销 |
| 高层创作 | 和弦、鼓、低音、琶音、变奏、段落增删/复制、编辑封装、增益处理和预览 |
| 查询/脚本/历史 | 工程/轨道/Clip/混音摘要，脚本批次、dryRun、undo/redo；索引随当前工程顺序变化 |
| 发现（M1） | `instrument.listPlugins`、`preset.list`、`sample.list`、`svs.listVoices`、`svc.listModels`、`fs.listRoots/listDirectory`；只读、分页，返回绝对路径/稳定选择 id |
| SVS（M2/M2b） | 建轨/建片段、声库绑定、音符 CRUD、批量填词、发音/音素、参数、词典、曲线、合成/取消/状态 |
| SVC（M3） | 建轨/建片段、模型/权重/说话人选择规范化、源音频、分块配置、转换/取消/状态 |

九个内置脚本：four_on_floor_drums、pop_chord_progression、arpeggio_16th、bassline_root_octave、humanize_groove、scale_snap、arrange_verse_to_chorus、mix_gain_staging、render_preview。

脚本位置和长度使用原生 ticks（四分音符 48 ticks）；对象变量取 `$track.index`、`$clip.index`。文件、配置和播放操作要求独立单命令脚本，不能与多步工程编辑混成可回滚事务。预览返回本机路径，播放和清理文件由调用方完成。

## 5. 验证状态

| 证据 | 状态 | 能证明什么 |
|---|---|---|
| 本次源码/配置检查 | 已完成，静态 | 服务、协议、工具派发、设置与测试入口存在 |
| 2026-10-04 计划书 B4 | 历史记录：Windows MSVC/Qt 6.10.3，CTest 13/13，五组 MCP 测试共 25 项 QtTest | 当时环境下的自动化验收；未在当前 HEAD 重跑 |
| 官方 Python SDK 2.2.0 | 2026-10-23：对 `AgentHarness` 实际连接，发现 **194** 个工具/schema，ping、JSON 传输与 `data/agent/mcp_client.py` 示例通过；未执行 `--execute --render`（当前无合规 Harness 原生插件部署） | 当前 HEAD 的协议互操作与工具目录；不代表真实主程序界面或音频渲染 |
| 构建开关矩阵 | 历史记录通过；本次 build/CMakeCache.txt 为 Agent ON/MCP ON | 历史开关验证与本地配置值 |
| M1 发现工具 | `DiscoveryCommandsTest` 通过；工厂预设 1129、工厂采样 1004、reference SVC 模型可列出 | 列表/分页/过滤/只读/错误路径 |
| M2 SVS 工具 | `SvsCommandsTest` 10 passed / 0 failed / 0 skipped（含 M2b 曲线）；`SVSExample` 合成到音频可用 | 建轨/片段/声库/音符/填词/参数/曲线/撤销/合成/词典错误 |
| M3 SVC 工具 | `SvcCommandsTest` 7 passed / 0 failed / 0 skipped；reference 引擎转换完成 | 建轨/选型/源音频/分块/转换/撤销 |
| Agent + MCP 回归 | 2026-10-23：13/13 passed（CommandBus、Core、A3、A4、MixAutomation、三个新测试、五组 Mcp 测试） | 新工具未破坏既有命令与协议路径 |
| 全量 CTest | 2026-10-23：**76/80 passed**；失败/超时为 `SVSIntegrationTest`（60s 超时）、`SVCIntegrationTest`（0xcfffffff）、`UiBaselineCapture`（120s 超时）、`ThemeWidgetTest`（0xcfffffff）。Agent/MCP 子集全部通过。 | 不能声称全量全绿；失败集中在 GUI/既有集成测试，需单独复验，标 `MANUAL/PENDING` |
| 设置真实窗口/中文字体/人工试听 | MANUAL/PENDING；本次未改 GUI，未做真实窗口验收 | 没有本次真实窗口或听觉验收证据 |
| Qt5/Linux/macOS | 未验证（历史记录缺少环境） | 源码兼容设计不等于平台构建通过 |

现有测试入口：McpServerLifecycleTest（监听/重启/限额/延迟回调）、McpProtocolTest（发现与会话）、McpSecurityTest（token/来源/配置）、McpQueueTest（串行/限额/排队取消）、McpExportLifecycleTest（任务所有权）；外部互操作在 `tests/src/agent/mcp/client_smoke.py`。

本地 `build/Release/lmms.exe` 已于 2026-10-23 由当前 HEAD（M3）重新构建（`cmake --build build --config Release --target lmms`，`EXITCODE=0`）。产物存在仍不代替真实窗口交互与人工试听。

## 6. 已观察到的差异与补充方向

1. **生成手册有参数差异**：CoreCommands.cpp 的 instrument.load schema 包含 `classid/architecture`，并在 vestige 加载 .vst3 时校验扫描目录中的 class/架构/路径和加载状态；当前 command-reference.md 未列出这两个字段。说明文档快照可能滞后，不证明运行时 schema 缺少字段。接手者先只读核对 tools/list；更新已有手册也先取得明确文件范围授权。
2. **计划中的示意命名不完全等于代码**：架构图仍有 McpAdapter、src/gui/agent 等规划名；实际协议类为 McpProtocol，设置直接在 SetupDialog.cpp。不要为对齐计划命名重构已有代码。
3. **早期阶段记录有历史语境**：B0 的“POST 返回 501”是当时未接协议的记录；当前 service() 已创建 McpProtocol，正常应用路径不能据早期文字判为只完成骨架。
4. **SVS/SVC 专用能力已从“未暴露”变为专用工具域（M2/M2b/M3）**：当前有 20 个 `svs.*` 和 10 个 `svc.*` 工具（含 M1 的发现命令），覆盖声库/模型选择、音符/歌词/音素/参数/曲线、词典、合成/转换与状态。仍未验证真实 RVC/USVC 后端与人工试听；不能把工具存在等同于全后端可用。`track.get`/`clip.get` 对 SVC/SVS 的类型显示缺口仍保留（新域 `getTrack` 返回正确类型），修正通用显示需要另行授权。
5. **其他平台、更多客户端与全面参数反射**属于候选后续事项，尚未授权实现；不能自行扩大已经完成的 B 阶段 Definition of Done。

## 7. 静态工具清单

以下由现有 command-reference.md 标题按 ToolRegistry 的 agent 域过滤规则整理。它是文档快照的完整清单，参数以运行时 tools/list 与当前源码为准；别名也各占一个工具。

| 域 | 数量 | 工具名称 |
|---|---:|---|
| agent | 1 | `agent.runScript` |
| arrange | 3 | `arrange.deleteBars`、`arrange.duplicateSection`、`arrange.insertBars` |
| automation | 10 | `automation.addClip`、`automation.addTarget`、`automation.createTrack`、`automation.listTargets`、`automation.putValue`、`automation.putValues`、`automation.removeNode`、`automation.removeNodes`、`automation.setProgression`、`automation.setTension` |
| clip | 14 | `clip.create`、`clip.delete`、`clip.duplicate`、`clip.get`、`clip.list`、`clip.move`、`clip.remove`、`clip.resize`、`clip.setAutoResize`、`clip.setColor`、`clip.setLength`、`clip.setMute`、`clip.setPosition`、`clip.setStartTimeOffset` |
| compose | 5 | `compose.arpeggio`、`compose.bassline`、`compose.chordProgression`、`compose.drumPattern`、`compose.melodyVariation` |
| config | 2 | `config.get`、`config.set` |
| controller | 5 | `controller.add`、`controller.connect`、`controller.list`、`controller.remove`、`controller.setParam` |
| edit | 3 | `edit.humanize`、`edit.quantize`、`edit.transpose` |
| effect | 8 | `effect.add`、`effect.getParams`、`effect.listAvailable`、`effect.move`、`effect.remove`、`effect.setEnabled`、`effect.setParam`、`effect.setWetDry` |
| export | 4 | `export.audio`、`export.cancel`、`export.midi`、`export.status` |
| fs | 2 | `fs.listDirectory`、`fs.listRoots` |
| history | 4 | `history.redo`、`history.rollbackBatch`、`history.status`、`history.undo` |
| import | 3 | `import.hydrogen`、`import.midi`、`import.sampleToTrack` |
| instrument | 17 | `instrument.getParams`、`instrument.listPlugins`、`instrument.load`、`instrument.loadPreset`、`instrument.savePreset`、`instrument.setArpeggio`、`instrument.setBaseNote`、`instrument.setMidiIn`、`instrument.setMidiOut`、`instrument.setNoteStacking`、`instrument.setPanning`、`instrument.setParam`、`instrument.setParameters`、`instrument.setPiano`、`instrument.setPitch`、`instrument.setPitchRange`、`instrument.setVolume` |
| midi | 10 | `midi.addNotes`、`midi.clearNotes`、`midi.getNotes`、`midi.humanize`、`midi.quantize`、`midi.removeNotes`、`midi.setClipType`、`midi.setSteps`、`midi.transpose`、`midi.updateNote` |
| mix | 1 | `mix.gainStaging` |
| mixer | 12 | `mixer.addSend`、`mixer.clearChannel`、`mixer.getChannel`、`mixer.getMaster`、`mixer.listChannels`、`mixer.removeSend`、`mixer.setColor`、`mixer.setMute`、`mixer.setName`、`mixer.setSendAmount`、`mixer.setSolo`、`mixer.setVolume` |
| model | 4 | `model.getValue`、`model.list`、`model.search`、`model.setValue` |
| pattern | 8 | `pattern.create`、`pattern.get`、`pattern.list`、`pattern.placeInSong`、`pattern.remove`、`pattern.removeFromSong`、`pattern.rename`、`pattern.setLength` |
| preset | 1 | `preset.list` |
| query | 6 | `query.clipDetail`、`query.mixerState`、`query.modelSearch`、`query.notes`、`query.songSummary`、`query.trackDetail` |
| render | 1 | `render.preview` |
| sample | 5 | `sample.getInfo`、`sample.list`、`sample.setFile`、`sample.setOffset`、`sample.setReversed` |
| scale | 3 | `scale.get`、`scale.set`、`scale.snapNotes` |
| song | 9 | `song.clearProject`、`song.getInfo`、`song.load`、`song.save`、`song.setMasterPitch`、`song.setMasterVolume`、`song.setPlayMode`、`song.setTempo`、`song.setTimeSignature` |
| svc | 10 | `svc.cancel`、`svc.convert`、`svc.createClip`、`svc.createTrack`、`svc.getStatus`、`svc.getTrack`、`svc.listModels`、`svc.setChunkConfig`、`svc.setModel`、`svc.setSourceFile` |
| svs | 20 | `svs.addNotes`、`svs.cancelSynthesis`、`svs.createClip`、`svs.createTrack`、`svs.getCapabilities`、`svs.getCurves`、`svs.getNotes`、`svs.getPronunciation`、`svs.getStatus`、`svs.getTrack`、`svs.importDictionary`、`svs.listVoices`、`svs.removeNotes`、`svs.setCurve`、`svs.setLyrics`、`svs.setNoteParameter`、`svs.setParameters`、`svs.setVoice`、`svs.synthesize`、`svs.updateNote` |
| track | 14 | `track.clone`、`track.create`、`track.delete`、`track.get`、`track.list`、`track.move`、`track.remove`、`track.setColor`、`track.setHeight`、`track.setMixerChannel`、`track.setMute`、`track.setMuted`、`track.setName`、`track.setSolo` |
| transport | 9 | `transport.getPosition`、`transport.play`、`transport.playSong`、`transport.previewClip`、`transport.seek`、`transport.setLoopRange`、`transport.setPosition`、`transport.stop`、`transport.togglePause` |

合计：194 个静态工具条目。五个仅本地接口为 agent.commandHelp、agent.diffPreview、agent.getContext、agent.listCommands、agent.searchCommands。
