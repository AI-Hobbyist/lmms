# Agent 增量回滚实施进度

## P1 DONE：边界盘点与 RED（2026-11-17）

用户已授权按 incremental-rollback-implementation-plan.md 持续执行，每阶段验证后提交推送。Penpot 实时 file/page 已核对一致；本阶段无布局和生产代码修改。

### 命令与边界

从实际初始化后的 CommandBus::descriptors 导出 204 条注册命令，逐条记录见 incremental-rollback-command-inventory.csv。完整 schema 原始数据保存在 build/agent-incremental-command-inventory.json，可通过 ChatProjectLifetimeTest 的 exportsRegisteredCommandInventory 重建，输出路径由 LMMS_COMMAND_INVENTORY 指定。

分类：51 只读、114 基础工程编辑、12 事务型组合编辑、2 脚本/预览、3 历史、2 禁止的工程替换（song.load / song.clearProject）、20 独立非事务操作。分类来自真实描述的 mutability/scope，工程替换及脚本/历史逐项列明；分类不是安全许可，未迁移的写命令不得据此前缀获准执行。

可信入口：ChatToolDispatcher::execute；McpProtocol::drain。两者目前直接调用 CommandBus::execute。ScriptRunner::Execution::dispatch 及 HighLevelCommands 的组合操作继续调用 execute；ScriptRunner::run 和命令外层会分别建立批次，因此组合 dry-run 可能恢复两次。MCP 关闭时 export.cancel 也需使用相同 Agent 约束。用户原生 GUI 不经这些入口，保留原有行为。

P2 接口决策：CommandBus 新增可信 Agent 执行入口并传播进程内上下文，模型参数不能设置来源；ProjectJournal 在原历史栈增加局部正反操作记录，原普通 execute 的历史快照兼容行为保留。工程重置公共入口在任何修改/通知之前检查 Agent 上下文。未迁移写操作在写入前拒绝，不退回快照。

### 当前契约与预期

- 单次成功写操作保留对象；事务性失败撤回整个活动批次。未知命令、schema/preflight 失败也终止已有批次。
- 正常 dry-run 使用子事务，成功只撤回本次预演；失败由 rollbackBatch 撤回全批。脚本用 BatchGuard 处理退出，不能重复撤销已清理的批次。
- 外部非事务操作按既有单步规则处理；导出、播放、配置及异步合成不混入工程原子批次。不能只看 scope=None 就认定无工程修改：例如 instrument.setPiano、transport.seek 仍需逐项审查。
- 新增 Agent 增量历史可撤销/重做；Agent 不恢复旧 projectSnapshot。旧快照只供用户原生手动历史使用，Agent 应在出栈前拒绝。
- 删除对象的实际析构会销毁子片段并更改容器；Track::loadSettings 同样删除并重建片段。因此不能用整轨 XML 恢复所有属性操作，也不能把删除后重造对象作为原对象仍存活的证据。P3 需要有限的对象暂存/挂回能力。

### 验证

- Release ChatProjectLifetimeTest 编译成功（17 秒），原生 Windows MainWindow、隔离工作目录和 Dummy 音频/MIDI。
- RED：agentOperationsNeverResetProject 共 9 个数据行均失败于 reset.count()!=0：dryRun、failed-batch、script-failure、composition-preview、undo、clear、load、script-clear、script-load。组合预演计数 2，其余 1。日志 build/agent-incremental-p1-red.log；未使用 QEXPECT_FAIL、跳过或屏蔽失败。此为 P1 要求的旧行为复现，不是新需求已通过。
- 既有基线 12 passed/0 failed/0 skipped，日志 build/agent-incremental-p1-baseline.log；新 RED 不作为旧基线退化。
- 生产代码和 build/Release/lmms.exe 未改；仅重建测试。全套 CTest 当前包含新增 RED，不能报告全部通过。P2 开始时将这些用例切到可信 Agent 入口，未迁移的组合编辑留待 P3/P4 转绿。

P1 提交 d78ec841f，已推送并核对远端一致。

## P2 DONE：增量事务基础与硬限制（2026-11-17）

- CommandBus::executeForAgent 建立程序设置的上下文，聊天执行和 MCP drain/export.cancel 进入该入口；脚本及组合命令的内部 execute 继承限制。普通 execute 与用户原生 GUI 保留原有快照兼容路径。
- Agent 直接调用 song.load/clearProject 返回 agent_project_reset_forbidden，工具目录隐藏两者；Song 的 clearProject/createNewProject/createNewProjectFromTemplate/loadProject/restoreProjectState 在任何写入与重置信号之前检查上下文，防止内部 handler 绕过命令名限制。两种批准模式均不能解禁。
- ProjectJournal 原历史栈新增局部正反回调记录，支持嵌套事务合并、局部 rollback/savepoint、modified 状态恢复及增量 undo/redo；QPointer 有效性检查拒绝失效目标。失败/预演不清 redo、不提交历史；事务结束失败不丢弃批次状态。不引入第二套聊天历史栈或自动快照 fallback。
- 首批支持 track.setName、song.setTempo：调用原处理器，在写入前记录原值和正反 setter。其余事务性写命令暂时在写入前返回 incremental_rollback_unavailable，必须继续 P3 迁移；这是过渡状态，不是最终能力验收。Agent 遇旧快照历史在出栈前拒绝，用户手动 undo 仍可用。

验证：

- 首次依赖重编译成功（35 cpp，259 秒），定向改动后的增量构建成功（3 cpp，32 秒）；dispatcher 测试目标构建成功（1 cpp，12 秒）。仅构建测试，lmms.exe 和生产安装未更新。
- 原生 Windows 完整 MainWindow 定向测试 29 passed/0 failed/0 skipped：P1 的八个基础场景转绿；实际写入后返回失败/抛异常/调用五个重置公共入口均局部恢复且零重置；预演保留对象/redo/父保存点，旧快照拒绝且不出栈；原有 12 项生命周期基线保留。日志 build/agent-incremental-p2-tests.log。
- 聊天真实调度/批准定向测试 10 passed/0 failed/0 skipped：两模式批准脚本在改名后试图清空工程均拒绝、恢复原名、零重置、不使回合失效；原批准/取消/身份/脚本基线通过。日志 build/agent-incremental-p2-dispatcher-tests.log。
- 初次故障探针漏写 mode schema，三项止于 invalid_arguments，已补夹具后转绿；保留 build/agent-incremental-p2-fixture-red.log，不将其当产品 RED。
- 格式审查撤回 formatter 对原有 include 顺序的无关调整，仅保留新 include；逻辑未变，git diff --check 通过。没有调试日志/offscreen/插件目录迁移。

未验证/未完成：组合预演（P1 的 composition-preview）和其他编辑工具留待 P3；完整 Chat/MCP 回归与能力覆盖留待 P5，当前不能宣称全套 CTest 通过。外部操作的逐项审查及完整脚本/历史契约仍在 P3/P4。人工安装验收 MANUAL/PENDING，无新 ZIP。

下一步：P2 提交推送核对后，P3 按命令清单迁移基础编辑与对象生命周期；不通过降低最终覆盖标准收尾。

P2 提交 afa15f095，已推送并核对远端一致。

## P3 IN PROGRESS：首批属性迁移

- 首批 25 条精确名称及别名列于 CoreCommands::registerDescriptor 的 incrementalCommands 集合；尚未验证通过，不能计为 P3 DONE。
- 因 AutomatableModel::setValueInternal 会更新关联模型，value() 可能读控制器值，改为在真实局部写入点捕获原始值/插值状态与范围/步长；回放时禁止新日志与命令重入。Track 的名称、颜色、独奏前静音标记，以及 Clip 的位置/长度/偏移/颜色同样记录局部逆操作；删除 P2 中重复的速度/名称外层捕获。
- Track 高度保留原始 storedHeight，避免把未指定的 -1 恢复成显示默认值而改变工程；Core 的 autoresize 属性单独记录。Song 的禁止重置检查按 WANT_AGENT 条件编译，避免没有启用 Agent 的构建链接其入口。
- 新增 50 个属性失败/预演数据行（25×2），检查原始音高范围收窄后的恢复、独奏关联标记、工程 XML、modified/历史深度及无关对象身份。当前正在现有目录编译 ChatProjectLifetimeTest，因头文件依赖重编译 134 cpp；尚未运行这些新数据行。日志完成后为 build/agent-incremental-p3-properties-build.log。
- 后续先验证本批属性，再继续音符/自动化、对象创建删除/连接、预设及 SVS/SVC。对象构造失败产生的临时模型、删除保留所有权和旧插件实例保留仍未实现；不得提前放开相关命令。普通 XML/整轨 restoreState 会破坏实例，不能代替这些能力。
- 首批依赖构建已成功（134 cpp，906 秒）。首次 50 行失败含夹具问题：saveProjectState 本身会置 modified 并创建两个检查点，已用定向日志证实，因此事务前在基线准备结束后清理历史/modified，历史断言移到后置序列化之前；instrument 模型路径补 /instrument。原序列化行为另记后续，不在本任务重构。日志 build/agent-incremental-p3-properties-initial-red.log、build/agent-incremental-p3-dirty-diagnostic.log。
- 校正后，48 行通过工程 XML 对比，仍因后置读取产生检查点而停在历史断言；独奏两行确有 XML 差异，来自增量回放 solo 值时再次执行 Track::toggleSolo 前向逻辑。已增加仅增量回放使用的标记并跳过该前向槽；同时增加直接静音/latent flag/范围断言和命令重入验证。修正正在编译（22 cpp），尚未转绿；日志 build/agent-incremental-p3-properties-fixture-tests.log 为修正前失败证据，不能按通过引用。
- 属性修正后原生窗口定向回归已通过：80 passed / 0 failed / 0 skipped（50 属性 + 重入保护 + P2/生命周期场景），日志 build/agent-incremental-p3-properties-green.log。修正构建 22 cpp、154 秒成功；没有活动旧构建或测试。P3 仍未完成，不能据此交付整个目标。
- MIDI 子组先覆盖现有音符的属性、位置排序、片段类型/步数；新增 15 个失败/预演/历史数据行。为保留 Note 指针，采用局部字段与顺序交换，不替换整个音符集合；新增/删除和 detune 自动化仍需后续所有权迁移。当前新数据行尚未运行，正在建立 RED；新辅助方法尚未接入处理器，不能宣称功能通过。

## 用户暂停（2026-11-17）

目标已由用户设为 paused。P3 未完成，本轮未提交推送；不继续开发或启动新测试。仅安全完成已在执行的 MIDI 测试目标编译并保存交接。已通过的 80 项属性/P2 回归与尚未运行的 15 项 MIDI 新用例必须区分，恢复后从 MIDI RED 开始。
- 暂停收尾：上述 MIDI 测试目标编译成功（18 cpp、183 秒、exit 0），日志 build/agent-incremental-p3-midi-red-build.log；没有启动新测试，15 个 MIDI 数据行未运行。目前没有活动构建/测试，git diff --check 通过；源码最后一批格式化与功能验证待恢复。本次不提交推送。
- 2026-11-18 恢复：MIDI 子组 RED 15/15 失败（failure 行原值未恢复，preview/history 行 incremental_rollback_unavailable），日志 build/agent-incremental-p3-midi-red.log。该次手动运行未设 LMMS_DATA_DIR，测试 exe 位于 build/tests/Release 时 initDevelopmentWorkingDir 找不到 CMakeCache，默认主题图标缺失（173 条 QWARN）；后续运行按 Capture-LmmsUi.ps1 设置 LMMS_DATA_DIR=data 与 LMMS_PLUGIN_DIR=build/Release/plugins，主题告警为 0。未修改生产路径逻辑。
- MIDI 子组接入：updateNote/transpose/quantize/setSteps/setClipType 在写入前调用 MidiClip::recordIncrementalNoteEdit 并加入增量许可；removeNotes 共用 handler 但不记录、不放开（所有权变更另行迁移）。测试构建 exit 0（build/agent-incremental-p3-midi-green-build.log）；定向 GREEN 17/17（build/agent-incremental-p3-midi-green.log）；完整 ChatProjectLifetimeTest 96 passed / 1 failed，唯一失败为已知未迁移的 agentOperationsNeverResetProject(composition-preview)，日志 build/agent-incremental-p3-midi-regression.log。P3 仍未完成。
- P3 局部状态子组（19 条）：mixer.setName/setColor/setVolume/setMute/setSolo/setSendAmount、instrument.setArpeggio/setNoteStacking/setMidiIn/setMidiOut、sample.setReversed/setOffset、automation.setProgression/setTension、pattern.rename/setLength、controller.setParam、song.setTimeSignature、transport.setLoopRange。新增 recordIncrementalOwnedValue（非 QObject 子部件按所有者+身份校验）；混音器独奏与轨道独奏同样在增量回放时跳过 toggledSolo 前向槽并记录 m_lastSoloed/m_muteBeforeSolo；MIDI 端口订阅表在 handler 内记录并经原接口重放；effect/instrument.setParam 的序列化整状态分支在 Agent 事务内写入前拒绝（反射模型分支照常）。
- 拒绝列表中暂缓：effect.setEnabled/setWetDry/setParam —— build/Release/plugins 不存在、无可加载效果插件，无法真实验证，保持拒绝。
- 测试 incrementalLocalStateRestores：19×failure/preview/history，检查对象身份、混音器通道身份与 m_muteBeforeSolo、局部序列化（快照期间暂停 journalling 并恢复 modified，规避已记录的序列化副作用）、整工程 XML、modified、历史深度，以及恢复后下一次独奏前向迁移。RED 57/57（build/agent-incremental-p3-local-red.log，均为 incremental_rollback_unavailable）。
- GREEN 过程中发现并修复真实缺陷：拍号回放会再次触发 SampleClip::updateLength / MidiClip::changeTimeSignature，把已恢复的片段长度改回；且 changeTimeSignature 修改 m_steps 未记录。现两槽在增量回放时跳过，m_steps 前向记录。GREEN 59/59（build/agent-incremental-p3-local-green.log），完整 ChatProjectLifetimeTest 153/154（唯一失败仍为未迁移 composition-preview，build/agent-incremental-p3-local-regression.log）。ChatToolDispatcherTest 全量 13/15：失败为 svs.removeNotes 与 instrument.loadPreset 的过渡拒绝（P3 SVS/预设子组待迁移），build/agent-incremental-p3-dispatcher-tests.log。主题告警均为 0。
- 后续（不在本计划范围）：GUI 存在时 mixer.setSolo handler 显式 toggledSolo 与 MixerView 直连槽重复执行，设置独奏会被第二次迁移取消（原有前向行为）。测试夹具改为直接设置模型。
- P3 音符所有权子组：midi.addNotes/removeNotes/clearNotes 在 MidiClip 原语（addNote/removeNote/clearNotes）内登记成员变化；事务内删除不 delete，所有权移交记录，撤销/重做/失败回滚挂回同一 Note 对象（后续历史中引用该指针的记录仍有效）；checkType 记录 m_clipType。登记时 journal 会先校验 valid，新增记录带 registeringAddition 标志。midi.humanize：位置/力度与顺序复用 recordIncrementalNoteEdit；对原本无失调的音符整值交换（新建 DetuningHelper 由记录持有）；已有失调对象（共享并原地修改）在 Agent 事务写入前精确拒绝。
- 组合命令 compose.*/edit.*/arrange.*/mix.gainStaging/compose.melodyVariation 标记 incremental：它们只经 ScriptRunner 发出步骤，嵌套步骤逐条受增量门控，未迁移步骤仍被拒绝并整批回滚。
- 验证：incrementalNoteValuesKeepIdentity 35/35（新增 addNotes/removeNotes/clearNotes/humanize/edit.humanize/edit.transpose 行），incrementalNoteMembershipKeepsLaterHistory 3/3（新增→编辑→撤销两次→重做两次，同一对象；clearNotes 失败回滚挂回同一对象）。日志 build/agent-incremental-p3-membership-*.log、build/agent-incremental-p3-humanize-green.log。完整回归（humanize 之前）163/164，唯一失败 composition-preview（需 track.create 等对象创建，下一子组）。
- 测试运行脚本：build/Run-LifetimeTest.ps1 <日志名> [测试函数] [可执行名]，设置 QT_QPA_PLATFORM=windows、LMMS_DATA_DIR=data、LMMS_PLUGIN_DIR。
- P3 轨道成员子组：track.create/delete/remove/clone/move。摘下=发 destroyedTrack（关闭视图）+removeTrack，所有权交记录；挂回=addTrack+moveTrack 原位，视图重建，对象身份与片段不变。记录销毁已摘下对象时保持 modified（~Track 删除片段会 setModified）。精确拒绝：Pattern 容器首条轨道（LMMS 会副作用新建 PatternTrack）、Pattern 轨道的删除/克隆/移动（Pattern 登记表）。
- 为此修正的 GUI 行为：TrackContainerView 排队建视图时确认轨道仍在容器且无未关闭视图，并按模型索引放置挂回轨道的控件（只动布局）；SampleTrackWindow 程序填入名称时屏蔽 textChanged（原行为会在任意采样轨道视图创建或改名回放时 setModified）。
- 验证：incrementalTrackMembership 17/17（5×3 行，含 GUI 轨道视图数量/顺序、对象身份、XML、modified、历史），日志 build/agent-incremental-p3-track-green.log；完整回归 187/188，唯一失败 composition-preview（需 clip.create）。
- 片段成员子组进行中：clip.create/delete/remove/duplicate，新增 Track::restoreClip(clip, index)（按原位置挂回并发 clipAdded）；Pattern 编辑器片段精确拒绝。Track.h 变更触发大范围重编译，构建日志 build/agent-incremental-p3-clip-build.log。
- 片段成员子组完成：clip.create/delete/remove/duplicate。修正：TrackView::createClipView 为排队槽，解引用前确认片段仍属该轨道且无未关闭视图。
- 关键所有权修正：Clip 是 Track 的 QObject 子对象、Track 是容器的子对象。摘下记录改为 QPointer+detached 标志（记录销毁时对象仍存活才删除，删除时保持 modified）；~Track 先删除已摘下的子片段、~TrackContainer 先删除已摘下的子轨道（二者析构都会回调父对象）。此前 composition-preview 回滚后丢弃记录时崩溃 0xC0000409。
- 验证：incrementalTrackMembership 35/35（轨道 5 + 片段 6 类 × 3 模式，含片段视图数量），build/agent-incremental-p3-clip-green.log；完整 ChatProjectLifetimeTest 206/206（含 P1 全部 9 个无重置场景，composition-preview 已转绿），build/agent-incremental-p3-clip-regression.log。日志中有一条 "JO-ID … already in use by instrumenttrack" 警告，待查是否与挂回/克隆有关。
- P3 自动化子组：automation.createTrack/addClip/putValue/putValues/removeNode/removeNodes。节点编辑在写入前快照整张 timeMap（值与切线），回放在音频引擎保护下交换；长度另有记录。automation.addTarget 暂保持拒绝（AutomationClip 无移除目标对象的接口，需改头文件，另议）。
- 发现并修复死锁：记录可能在容器持读锁时被销毁（saveProjectState 序列化产生检查点→清空 redo），而 ~Track 调 removeTrack 请求写锁。已摘下轨道的最终删除改为排队到事件循环（保持 modified），事件循环不再运行时由 ~TrackContainer 兜底。
- 验证：完整 ChatProjectLifetimeTest 227/227（局部状态 25×3、音符 11×3+跨历史、轨道/片段 12×3、P1/P2/生命周期基线），build/agent-incremental-p3-automation-regression.log，主题告警 0。
- P3 剩余（仍在写入前拒绝）：automation.addTarget；mixer.addChannel/removeChannel/moveChannel/clearChannel/addSend/removeSend；pattern.create/remove/placeInSong/removeFromSong；controller.add/remove/connect；effect.*（无可用插件，无法验证）；instrument.load/loadPreset；import.*；sample.setFile；scale.set/snapNotes；svs.*；svc.*。
- P3 发送/Pattern 放置/采样文件：mixer.addSend/removeSend（按通道对记录存在性，重做按原数量重建；路由对象为新对象、可能排到发送列表末尾，路由语义不变）；pattern.placeInSong（创建记录）/removeFromSong（复用片段删除）；sample.setFile（写入前保存旧 Sample 值，共享已解码缓冲区，回放不读文件）。incrementalLocalStateRestores 91 行通过（build/agent-incremental-p3-sample-green.log）。非音频文件解码失败时 LMMS 原有逻辑弹模态对话框，无法自动化，未纳入测试（handler 写后失败路径由记录覆盖，记为 MANUAL/PENDING）。
- P3 SVS 片段本地编辑：svs.addNotes/updateNote/removeNotes/setLyrics/setNoteParameter/setCurve/importDictionary 在 SvsCommands 注册处包装，Agent 事务内先对目标片段 saveSettings 快照，回放在同一对象上 loadSettings 交换（保持对象身份，不读文件或声库；用 saveSettings 而非 saveState，避免 journallingObject 元数据被当作未解析内容保留）。测试 incrementalSvsClipState 11/11（addNotes/updateNote/removeNotes × 3 模式；setLyrics/setNoteParameter 需声库能力，夹具无声库，未纳入自动测试）。完整生命周期 251/251，ChatToolDispatcherTest 14/15（svsRangeAndStableIdTargets 已转绿；剩 instrument.loadPreset 过渡拒绝）。日志 build/agent-incremental-p3-svs-*.log。
- P3 SVS 创建：svs.createTrack/createClip 复用轨道/片段创建记录（CoreCommands.h 新增 recordAgentCreatedTrack/recordAgentCreatedClip）；事务内声库绑定失败不再直接 delete（交由回滚摘下）。incrementalSvsClipState 17/17。
- 退出安全修复：历史中已摘下的轨道原先在 ~TrackContainer 才被删除，此时混音器/音频引擎已拆除，单独运行 history 行时退出崩溃。现由 TrackContainer::clearAllTracks（clearProject 与析构均调用，引擎仍在）删除带 Track::DetachedByHistoryProperty 标记的子轨道（只删历史摘下的，不误删 m_globalAutomationTrack 等其他子轨道）。三个 history 行单独运行均正常退出；完整生命周期 257/257，ChatToolDispatcherTest 14/15。日志 build/agent-incremental-p3-exit-*.log。
- 需要决策（计划书 5.2：现有对象生命周期不支持可逆挂接时须先请求批准）：instrument.load/loadPreset 会删除并替换运行中的乐器插件实例；可靠恢复需让 InstrumentTrack 保留旧实例并可换回，涉及插件生命周期。另外 build/Release/plugins 不存在，乐器/效果插件均无法真实验证。
- 其余未迁移、需要改核心头文件（各约 15 分钟全量重编译）但不涉及插件体系：mixer.addChannel/removeChannel/moveChannel/clearChannel（Mixer.h，需可摘下的 MixerChannel）、controller.add/remove/connect（Song.h，removeController 直接删除）、automation.addTarget（AutomationClip.h，无移除目标接口）、pattern.create/remove（Pattern 编辑器成列片段与索引）、svs.setVoice/setParameters、svc.*、import.*、scale.*、effect.*。
- P3 scale：scale.set 在写入前保存旧 Scale/Keymap（不可拷贝，按其构造参数重建等值定义）；scale.snapNotes 复用 recordIncrementalNoteEdit。完整生命周期 263/263（build/agent-incremental-p3-scale-green.log）。
- P3 SVC 创建：svc.createTrack/createClip 复用创建记录；事务内选择失败不再直接 delete（deleteUnlessRecorded）。仅编译通过（build/agent-incremental-p3-svc-build.log），**未运行测试**：用户 2026-11-18 要求后续不再运行耗时的 lifetime 测试，svc 行为验证标记 PENDING。
- svs.setParameters/setVoice 依赖声库能力，当前无声库，保持拒绝。
- 2026-11-18 用户决定：除可快速单元测试的项目外，其余一律注明人工验收并跳过。按此口径 P3 收尾：
  - MANUAL/PENDING（跳过，Agent 调用仍在写入前以 incremental_rollback_unavailable 拒绝，无整工程重载）：instrument.load/loadPreset（插件实例生命周期，未获改动批准）、effect.*（无可用插件）、mixer.addChannel/removeChannel/moveChannel/clearChannel、controller.add/remove/connect、automation.addTarget、pattern.create/remove、svs.setParameters/setVoice（无声库）、import.*。
  - MANUAL/PENDING（已实现未测试）：svc.createTrack/createClip。
  - 人工验收：sample.setFile 解码失败模态框路径；有界面时 mixer.setSolo 重复切换（范围外）。
- P3 检查点：最近一次完整自动验证 ChatProjectLifetimeTest 263/263、ChatToolDispatcherTest 14/15（失败项 instrument.loadPreset 属上述 MANUAL/PENDING）。
- P4（按用户口径）：已由自动测试覆盖——脚本后续步骤失败撤回前序（各 failure 行）、预演保存点与 redo 共存（preview 行、P2 incrementalPreviewPreservesRedoAndParentChanges）、Agent undo/redo（history 行）、旧快照历史拒绝且不出栈（P2）、用户手动历史保留（P2 基线）。MANUAL/PENDING：嵌套 call 与 diffPreview 的专门验证、迁移目录能力说明文案。
- P5（按用户口径）：覆盖清单见本文件各子组与 MANUAL/PENDING 列表。MANUAL/PENDING 跳过：全量 Chat/MCP/命令回归、开发版 lmms.exe 与插件部署（build/Release/plugins 不存在）、增量 ZIP/SHA256/VerifyOnly、真实安装验收。
