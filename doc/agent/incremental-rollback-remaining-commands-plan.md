# Agent 增量回滚剩余 28 条命令实施计划

2026-11-18。状态为待实施。范围以当前命令注册和本页清单为准，承接原增量回滚计划。

## 共用执行约束

复用 CommandBus、ProjectJournal、ScriptRunner 和现有命令处理器。在首次写入前完成校验和局部逆操作登记。失败和 dry-run 按逆序恢复，成功提交一个增量历史记录。回放使用已保留的对象或数据。历史栈、modified 状态和未受影响对象应保持一致。

每条命令通过对应单元测试后才启用增量许可。迁移前继续在写入前拒绝。Agent 禁止整工程重载的规则持续有效，用户手动操作路径保留。

## 依赖与阶段

| 阶段 | 命令 | 实施范围 | 快速单元测试 |
|---|---|---|---|
| R1 局部数据（7） | `automation.addTarget`、`svs.setParameters`、`svs.setPitchCurve`、`svs.setVoice`、`svc.setChunkConfig`、`svc.setModel`、`svc.setSourceFile` | 自动化保存目标顺序、信号连接、首节点与切线。SVS/SVC 先验证候选配置，保留旧选择、参数和受影响数据，缓存按实际写入范围恢复或失效。回放不触发合成、转换、模型下载或重新读文件。 | 验证目标重复、节点校验失败、配置部分更新失败、曲线及缓存状态恢复。用内存数据覆盖配置逻辑。 |
| R2 工程成员与引用（9） | `mixer.addChannel`、`mixer.clearChannel`、`mixer.moveChannel`、`mixer.removeChannel`、`controller.add`、`controller.connect`、`controller.remove`、`pattern.create`、`pattern.remove` | 混音器恢复通道顺序、发送/接收、轨道路由和效果链。控制器恢复原目标与双向连接。Pattern 恢复编号、列片段和歌曲引用。复用现有成员变更记录，按对象身份定位。混音器涉及效果链的分支依赖 R3，须在 R3 通过后才放开相关整条命令。 | 用小型内存模型验证重排、双向引用、编号与索引、失败后状态等价。 |
| R3 插件与参数（9） | `instrument.load`、`instrument.loadPreset`、`instrument.setParam`、`effect.add`、`effect.move`、`effect.remove`、`effect.setEnabled`、`effect.setParam`、`effect.setWetDry` | 插件实例管理见下节。参数优先复用现有模型记录，对序列化参数分支先确认完整的局部逆操作。效果重排恢复原槽次和对象身份。 | 用轻量替身验证候选准备失败、交换失败和逆序回放。验证参数原始值、槽次与路由。真实插件行为记为 `MANUAL/PENDING`。 |
| R4 导入（3） | `import.hydrogen`、`import.midi`、`import.sampleToTrack` | 依赖已验证的轨道、片段、自动化、路由及所用插件能力。解析和资源准备在工程写入前完成，再把新增内容纳入一个批次。核对导入器的间接写入，禁止进入整工程替换路径。 | 用小型内存输入和替身验证解析错误无写入、中段失败撤回已添加内容、预演保持历史和原工程数据。 |

四阶段合计 **28** 条。R1 与 R2 的独立子组可先完成。R3 未获授权时继续实施可独立验证的子组，依赖 R3 的分支保留写入前拒绝。

## 需要改插件实例管理的命令

- **直接需要** `instrument.load`、`instrument.loadPreset`。在替换前准备候选，保留运行中的原实例。预设同时核对效果链、模型参数、路由等关联写入。撤销和重做换回已保留的实例，无须重新读取原预设。
- **效果链直接需要** `effect.add`、`effect.remove`。记录新实例归属，移除时保留原实例及槽位以便挂回。先明确挂接和记录销毁时的所有权。
- **间接依赖** `mixer.clearChannel`、`mixer.removeChannel` 的含效果器分支复用效果链实例保留能力。导入命令按实际调用路径复用插件准备与提交能力。
- **先审计再决定** `instrument.setParam`、`effect.setParam` 的序列化写入分支。先证明可对同一实例恢复受影响状态。如必须替换实例，纳入同一授权范围。普通模型参数、效果排序、启用和干湿比不预设需要更换实例。

插件实例管理开始前按原计划确认改动范围。其余命令可独立推进。

## 验证及检查点

- 自动验证只安排快速单元测试。用内存夹具和少量故障注入覆盖成功、已写入后失败、dry-run、撤销和重做。验证实际恢复的数据、引用与历史，不以只检查调用次数代替。
- 生命周期测试统一记录为 `SKIPPED（用户禁止）`。真实插件、声库、整机和真实导入环境尚未验收的行为记为 `MANUAL/PENDING`。单元测试替身的结果仅证明它覆盖的逻辑。
- 每阶段通过适用的快速单元测试后更新覆盖记录，检查差异，按原计划提交推送并核对远端。依赖未满足或只通过编译的命令应单独标记。

相关文件：`incremental-rollback-implementation-plan.md`、`incremental-rollback-progress.md`、`incremental-rollback-command-inventory.csv`。

## 实施检查点

- R1 的七条命令已接入。Release 构建通过，三个无界面测试目标的六个定向用例或数据行全部通过。覆盖目标绑定失败后的连接与节点恢复、SVC 三项配置的预演及回放、SVS 音高曲线与本地声库字段和宿主音量恢复。日志为 `build/agent-remaining-r1-final-*.log`。
- SVS 真实声库的能力刷新与合成仍为 `MANUAL/PENDING`。生命周期测试 `SKIPPED（用户禁止）`。
- R2～R4 待实施。R3 插件实例管理已单独发出确认请求。
