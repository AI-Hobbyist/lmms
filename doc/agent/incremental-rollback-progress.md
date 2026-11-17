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
