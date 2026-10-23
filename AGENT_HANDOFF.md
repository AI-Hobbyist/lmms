# Agent 交接手册：LMMS MCP 补充工作

更新：2026-10-23（Asia/Seoul）。本次任务：在 `MCP工具补充计划书.md` 授权下完成 M1～M3 与 M2b，并整理 M4 文档与验证记录。M1～M3/M2b 已实现、提交并推送；M4 更新文档与验证状态。

## 1. 首要约束：已有实现冻结

**用户明确要求：其他 Agent 不能修改已有实现。** 本条约束适用于本交接的所有接手 Agent，优先于“补充完整”“修复顺手问题”或与旧计划对齐的动机。

- 现有源码、头文件、命令注册、schema、协议行为、UI、资源、既有测试与构建配置均属于冻结范围；不能改写、删除、移动、重命名或重新格式化。
- 保留现有工具名、参数、返回值、别名、错误和事务语义；不得覆盖同名命令或通过新文件、包装器、补丁、生成器间接替换已有实现。
- M1～M3 新增的 33 个工具（发现、SVS、SVC）已经成为新的既有实现基线，同样冻结；后续只能在其上追加或经用户明确授权修改。
- 可以只读分析、记录差异、提出新增接口方案。获得后续明确任务授权后，可在该任务允许的**新文件**中补充独立文档、示例、测试或扩展；本手册本身不授权启动开发。
- 接入新功能若需要编辑旧 CMakeLists、CommandBus 构造/注册入口、现有测试、现有生成手册等，必须先停止这一步，列明必要性、文件/符号、最小改动及不改的结果，等用户明确授权具体范围。授权只覆盖获批部分，其余实现继续冻结。
- 发现既有缺陷，先记录证据与影响边界，不自行修复、不削弱测试断言。独立新增测试若发现失败，报告失败并等待相应修改授权。

完成判据：接手后的任何交付都列出新增文件和获批修改文件；diff 中不能出现未经批准的既有实现变更。

## 2. 接手顺序

1. 读取项目 `AGENTS.md`、本手册、[MCP 实现分析](MCP_IMPLEMENTATION_ANALYSIS.md) 和 [MCP 工具补充计划书](MCP工具补充计划书.md)。
2. 读取计划书 §7～§10，以及 [连接文档](data/agent/MCP.md)。旧阶段中列出的“缺项”必须结合后续验收记录理解，不能直接重新实现。
3. 检查 `git status`、当前分支/HEAD 与本手册基线。保留他人未跟踪文件；文档结论漂移时标明差异。
4. 对准备补充的**具体目标**先用 CodeGraph 查入口和现有注册，确认现有 API 是否已经能完成；不重复造工具。
5. 明确新增能力、验收条件和文件范围。必须修改已有实现时先请求具体授权；未获授权只完成分析/方案。
6. 按授权范围推进最小任务并验证，记录静态证据、本次执行、MANUAL/PENDING 和失败状态。

## 3. 已完成与未完成

### 已完成（2026-10-23）

- **M1 发现工具（7）**：`instrument.listPlugins`、`preset.list`、`sample.list`、`svs.listVoices`、`svc.listModels`、`fs.listRoots`、`fs.listDirectory`。全部只读、分页，返回绝对路径或稳定选择 id。
- **M2 SVS（17）**：建轨/建片段、声库绑定、音符 CRUD、批量填词（复用 `SVSLyricEditor::splitLyrics`）、发音解析、音素/参数、词典、合成/取消/状态。
- **M2b SVS 曲线（2）**：`svs.getCurves`、`svs.setCurve`，复用 `svs::Curve::fromJson/toJson`，支持替换与 `curve:null` 删除。
- **M3 SVC（9）**：建轨/建片段、`requestSelection` 规范化选型、源音频、分块配置、转换/取消/状态。
- **M5 调音台通道（2，2026-10-24）**：`mixer.addChannel`、`mixer.removeChannel`，补齐“调音台相关轨道添加”缺口（此前 MCP 只能改 master，无法新建通道，因此 `track.setMixerChannel` 只能指向 0）。新文件 `src/agent/MixerCommands.h/.cpp`、`tests/src/agent/MixerCommandsTest.cpp`；旧文件仅追加注册/构建/测试/文档。master 受保护、在用通道需 `force:true` 才能连带改线后删除；两者均支持 `dryRun` 与 undo。
- 当前运行时目录：**196 个 MCP 工具 / 201 个注册命令**；`data/agent/command-reference.md` 已由 `AgentHarness --manual` 重新生成，`data/agent/MCP.md` 已补发现/Mixer/SVS/SVC 说明。
- 新增测试：`DiscoveryCommandsTest`、`SvsCommandsTest`、`SvcCommandsTest`。Agent + MCP 回归 **13/13 passed**。
- 官方 Python MCP SDK 2.2.0 对 `AgentHarness` 实际连接：发现 **194** 个工具/schema、ping、JSON 传输与 `data/agent/mcp_client.py` 示例通过。

### 未完成 / 验证缺口

- 真实 RVC/USVC 后端、真实声库人工试听、真实 LMMS 设置页/MCP 连接人工验收：`MANUAL/PENDING`，本次按用户要求注明并跳过。
- `client_smoke.py --execute --render` 未执行：当前没有同时具备 `AgentHarness` 与原生插件 DLL 的合规部署；按项目规则未复制一个新的 Harness 部署目录。
- 全量 CTest 已尝试：**76/80 passed**，失败/超时为 `SVSIntegrationTest`（60 秒超时）、`SVCIntegrationTest`（退出码 `0xcfffffff`）、`UiBaselineCapture`（120 秒超时）、`ThemeWidgetTest`（退出码 `0xcfffffff`）。所有 agent/MCP 测试通过。本次未改 GUI/SVC 设置页，相关失败记录为 `MANUAL/PENDING`，不能据此声称全量全绿。
- 通用 `track.get`/`clip.get` 对 SVC/SVS 的 `type` 显示缺口仍保留；新域 `getTrack` 已返回正确类型。修正通用显示需另行授权。
- Qt5/Linux/macOS 未验证。

## 4. Git 与文件状态

- 工作区：`D:\UserData\Desktop\Project\lmms`。
- 分支：`master`；M3 HEAD：`2e1749e52`。M4 文档提交紧随其后，以 `git log` 为准。
- origin：`https://github.com/AI-Hobbyist/lmms.git`；upstream 为 LMMS 上游。
- 已推送提交：
  - `386b62c7a` agent: add M1 read-only discovery tools
  - `307764ee5` agent: add M2 SVS editing tools
  - `2e1749e52` agent: add M3 SVC selection and conversion tools
  - `49dbbdaf1` docs: complete M4 MCP handoff and validation records
  - M2b（`svs.getCurves/setCurve` 与本次文档更新）：本手册随该提交更新，哈希见 `git log`。
- 工作区仍有大量他人未跟踪日志、截图、SDK/参考代码和其它产物；不得清理或归入本任务提交。
- 本手册与 `MCP_IMPLEMENTATION_ANALYSIS.md` 在 M4 一并纳入提交；此前它们保持未跟踪状态。

## 5. 代码入口与兼容约定

| 要了解的行为 | 入口 |
|---|---|
| 本机 HTTP、监听、token、来源和限额 | `src/agent/mcp/HttpMcpServer.cpp`、对应 `include/agent/mcp` 头文件 |
| 初始化、单会话、发现/调用、队列和取消 | `src/agent/mcp/McpProtocol.cpp` |
| 工具导出/annotations/本地帮助过滤 | `src/agent/ToolRegistry.cpp` |
| 校验、事务、dryRun、回滚、history | `src/agent/CommandBus.cpp`、`CommandSchema.cpp`、`ProjectSnapshot.cpp` |
| 业务命令和参数 schema | `src/agent/CoreCommands.cpp` |
| 只读发现（M1） | `src/agent/DiscoveryCommands.cpp`、`DiscoverySupport.h` |
| SVS 命令（M2/M2b） | `src/agent/SvsCommands.cpp`、`CommandTargets.cpp` |
| SVC 命令（M3） | `src/agent/SvcCommands.cpp`、`CommandTargets.cpp` |
| 导出任务 | `src/agent/ExportCommands.cpp` |
| 脚本/高层组合 | `ScriptRunner.cpp`、`ScriptExpression.cpp`、`HighLevelCommands.cpp` |
| 设置和应用生命周期 | `src/gui/modals/SetupDialog.cpp`、`src/core/Engine.cpp` |
| 测试与 Harness | `tests/CMakeLists.txt`、`tests/src/agent/AgentHarness.cpp`、`tests/src/agent/mcp/` |

关键约定：本机 IPv4 HTTP `/mcp`；协议版本 2025-11-25；单会话替换；运行时默认关闭；令牌环境变量默认 `LMMS_MCP_TOKEN`。业务工具目录由总线自动生成，不手工维护第二份注册表。

新增命令命名约定：

- `instrument.listPlugins`、`preset.list`、`sample.list`、`fs.*` 为只读发现。
- `svs.*`、`svc.*` 的建轨/建片段/选择/编辑为 `Mutating + TxScope::Single`；`synthesize/convert/cancel` 为 `Mutating + TxScope::None` 且禁止在项目批次内执行；`get*` 为 `ReadOnly`。
- SVS 能力/声库扫描和 SVC 引擎发现都是异步的，客户端必须轮询 `scanning`/`status`/`capabilitiesReady`。

工程编辑、文件操作和播放的事务边界不同。不要把文件输出视为能被 `history.undo` 删除；不要把本机文件路径当成远程媒体资源。断连/响应丢失不证明写入失败，先查询状态再决定是否重发。

## 6. 建议后续最小任务

按依赖顺序，等待用户选择/授权后执行：

1. **真实后端验证**：配置本机 RVC/USVC 服务，验证 `svc.listModels/setModel/convert` 的真实选择、转换、取消与试听；再验证真实 DiffSinger 声库的合成与填词。
2. **合规的 SDK 执行/渲染冒烟**：在具有当前插件集的真实 `build/Release/lmms.exe` 上启动 MCP 服务，或在用户批准的合规 Harness 部署上运行 `client_smoke.py --execute --render`。不要新建替代部署目录。
3. **全量 GUI/VST 复验**：单独重跑 `SVSIntegrationTest`、`UiBaselineCapture`、`SVCIntegrationTest`、`VstEntryPointsRealtime`，判断超时/布局断言是否既有环境问题。GUI 必须真实 Windows 窗口，禁止 offscreen。
4. **通用类型显示小修（需授权）**：`trackTypeName` 补 `Track::Type::SVC`，`clipTypeName` 补 SVS/SVC，使 `track.get/clip.get` 不再返回 `unknown`。

以上是接续建议，不是待执行命令列表，也不授予实现范围。已有 B 阶段默认不新增聊天、LLM 客户端、OAuth、多用户隔离、审批/审计子系统、媒体推送或全插件反射。

## 7. 验证与运行约束

2026-10-23 实际执行：

- `cmake --build build --config Release --target AgentHarness/lmms/SvsCommandsTest/SvcCommandsTest`：`EXITCODE=0`。
- `AgentHarness --tools`：194；`--manual`：199（生成 `data/agent/command-reference.md`）。
- `ctest -R "CommandBus|Core|MixAutomation|A3|A4|Discovery|SvsCommands|SvcCommands|Mcp"`：13/13 passed；`SvsCommandsTest` 另计 10 passed（含 M2b 曲线）。
- 官方 SDK 2.2.0（venv `build/sdk-venv`）：194 schemas、ping、JSON 传输、`mcp_client.py` 通过。
- 全量 CTest：**76/80 passed**；失败/超时为 `SVSIntegrationTest`（60s 超时）、`SVCIntegrationTest`（0xcfffffff）、`UiBaselineCapture`（120s 超时）、`ThemeWidgetTest`（0xcfffffff）。失败集中在 GUI/既有集成测试；未使用 Qt offscreen。

后续验证时：

- 编译前通过 Codex 任务列表检查同工程任务；有其他任务运行时不编译，记录 `PENDING（同工程其他任务运行中）`。
- 在会话内前台 PowerShell 执行所有 configure/build/test/package；先 dot-source `./buildtools/Enter-LmmsEnvironment.ps1`，使用 PATH 中 cmake/ctest 与配置好的 SDK 环境变量。
- 每条命令用 `2>&1 | Tee-Object -FilePath "build.log"`，立即捕获 `$LASTEXITCODE`。当前会话的 PowerShell 5.1 不支持 `Tee-Object -Encoding utf8`；不要为加该参数而改用 offscreen 或后台静默执行。
- 复用 `build` 与既有验证部署，生产开发可执行文件为 `build/Release/lmms.exe`，插件为 `build/Release/plugins`。不要为测试新建替代部署目录或覆盖开发 `lmms.exe` 冒充 Harness。
- SDK 脚本是 `tests/src/agent/mcp/client_smoke.py`，需要 Python `mcp` 与 `httpx2`；本次在 `build/sdk-venv` 安装 `mcp==2.2.0`、`httpx2==2.16.0`。不擅自升级服务端。
- Qt GUI 验证必须真实 Windows 窗口：构建 → 显示 → 稳定 → 交互/真实截图 → 检查 → 关闭，检查 CJK 渲染。严禁 Qt offscreen；QCoreApplication 的协议 Harness 只验证非 GUI 行为，不作视觉证明。
- 不执行与本任务无关的构建或格式化。新增/获批修改检查可读性和 `git diff --check`，保留他人改动。

## 8. 已知风险、差异与失败状态

- **冻结基线已扩展**：M1～M3/M2b 的 35 个工具是新的既有实现；后续不得改其名称、参数或语义。
- **曲线已实现**：`svs.getCurves/setCurve` 支持声明参数的曲线替换与删除；未知/只读/不可曲线化参数返回结构化错误。
- **异步目录**：`svs.listVoices`/`svc.listModels` 首次调用可能返回空或提示扫描中；必须轮询，不应把它当成“无资源”。
- **真实后端缺口**：reference SVC 引擎和 SVSExample 已通过自动化；RVC/USVC/DiffSinger 真实后端与人工试听未验证。
- **全量 CTest 失败**：`SVSIntegrationTest`/`UiBaselineCapture`/`ThemeWidgetTest` 超时或异常退出、`SVCIntegrationTest` 设置页断言与异常退出；本次未改相关 GUI，记录为 `MANUAL/PENDING`，不声称全绿。
- **通用类型显示**：`track.get/clip.get` 对 SVC/SVS 仍可能返回 `unknown`；使用新域 `svs.getTrack`/`svc.getTrack` 获取正确类型。
- **SDK 执行/渲染未跑**：缺少合规 Harness 原生插件部署；不要通过新建目录或覆盖开发 exe 绕过。
- **文档差异**：`instrument.load` 源码 schema 的 `classid/architecture` 已包含在重新生成的手册中；若未来文档漂移，以运行时 `tools/list` 为准。
- **其他未跟踪文件**：不属于本任务，不能清理或归入提交。

相关文档：`AGENTS.md`、`AGENT_SUPPORT_PLAN.md`、`MCP_IMPLEMENTATION_ANALYSIS.md`、`MCP工具补充计划书.md`、`data/agent/MCP.md`、`data/agent/README.md`、`data/agent/command-reference.md`。交接以本手册为当前有效版本，后续更新保留仍有效信息，不自动删除。
