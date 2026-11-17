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

P2～P5 NOT STARTED。下一步在 P1 提交推送检查点后实施 P2，不扩展为全面重写用户历史系统。
