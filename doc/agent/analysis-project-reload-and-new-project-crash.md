# 分析：工程重载触发条件，及 Agent 执行后“新建工程”卡死闪退

日期：2026-11-17。第 1～5 节保留首次只读分析；第 6 节起记录后续实验证据与最小修复，以后者为当前结论。

## 1. 结论摘要

- 闪退位置已由两份真实崩溃转储确定：工具栏“新建” → `Song::createNewProject` → `createNewProjectFromTemplate` → `Song::loadProject(模板)` → 构造第一个 `PatternTrack` → `PatternStore::updateComboBox()` → 空指针/悬空指针访问（`0xc0000005`）。
- 直接缺陷：`PatternStore::updateComboBox()`（`src/core/PatternStore.cpp:198`）用 `numOfPatterns()`（Song 中 Pattern 轨道数）做循环上界，用 `PatternTrack::findPatternTrack(i)`（全局静态表 `PatternTrack::s_infoMap`）取对象，不判空即 `pt->name()`。两份数据只要失步就会崩溃。
- 根因方向：崩溃时 `s_infoMap` 与 Song 的 Pattern 轨道不一致（新轨道编号取 `s_infoMap.size()`，被残留或悬空条目顶高；21:53 的转储在遍历 `s_infoMap` 时读到垃圾指针 `0xff000000ff000000`）。正常启动后直接新建不崩溃，Agent 执行后才出现，说明失步由此前的操作留下。Agent 会大量走“快照整体重建”路径（见第 2 节），这是最主要的嫌疑。具体是哪一步残留了条目，尚需按第 5 节复现确认，尚未用实验证实。
- “卡死”：崩溃前用户感知的卡顿，与在同一次点击中同步执行模板加载（含 `QCoreApplication::processEvents/sendPostedEvents`）相符；转储中没有发现独立的死锁证据。

## 2. 会触发工程重载/重置的入口

`Song::clearProject()`（`src/core/Song.cpp:989`）会在开头同步发出 `projectAboutToReset`（聊天调度器据此判定“工程已变化”），随后删除全部轨道、清空混音器与控制器，并调用 `projectJournal()->clearJournal()`。下列入口会调用它：

| 类别 | 入口 | 是否整体重建 |
| --- | --- | --- |
| 用户 | 新建工程 `createNewProject`；从模板新建 → `loadProject` | 是 |
| 用户 | 打开工程、拖放工程文件 `loadProject`（`TrackContainerView.cpp:489`、`main.cpp`） | 是 |
| 用户 | 撤销/重做（Ctrl+Z / Ctrl+Y）到 Agent 提交的检查点：`ProjectJournal::undo/redo` 遇 `projectSnapshot` 检查点 → `Song::restoreProjectState`（`ProjectJournal.cpp:87/145`） | 是 |
| Agent 命令 | 任意写命令失败（handler 返回 `ok=false` 或抛异常）→ `CommandBus::execute` 调 `rollbackBatch()` → `ProjectJournal::rollbackTransaction` → `restoreProjectState`（`CommandBus.cpp:313–322`、`ProjectJournal.cpp:231`） | 是 |
| Agent 命令 | 批次进行中出现未知命令、参数校验失败、预检失败：整批回滚（`CommandBus.cpp:225/236/267`） | 是 |
| Agent 命令 | 任意写命令的 `dryRun`：执行后 `endBatch(false)` → `rollbackTransaction` → `restoreProjectState`（`CommandBus.cpp:378`） | 是，每次预演都整体重建 |
| Agent 命令 | `history.rollbackBatch`、`history.undo/redo`（经 Journal，命中快照检查点即整体重建） | 是 |
| Agent 命令 | `song.clearProject`、`song.load`（`CoreCommands.cpp:2764/2806`） | 是 |
| 进程 | 引擎销毁 `Engine::destroy`（`Engine.cpp:191`） | 是 |

补充：
- `ProjectJournal::beginTransaction` 对每个写命令都会先 `saveProjectState` 整工程快照，提交后把它作为撤销检查点压栈。因此 Agent 每次成功编辑后，用户的一次撤销也会整体重建工程。
- 典型实例：引用了安装目录缺失采样（`samples/empty.wav`）的出厂预设，`instrument.loadPreset` 会失败并触发整体回滚。

## 3. 崩溃证据（真实转储，只读解析）

- 事件日志（Application，WER 1000/1001）：
  - 2026-11-17 22:08:09，`lmms.exe` 时间戳 `0x6afc5030`，故障模块 `lmms.exe`，偏移 `0x8587c`，进程 `0x8B50`；转储 `%LOCALAPPDATA%\CrashDumps\lmms.exe.35664.dmp`。
  - 2026-11-17 21:53:26，`lmms.exe` 时间戳 `0x6afc497d`，故障模块 `Qt6Core.dll`，偏移 `0xf76dc`，进程 `0x2B68`；转储 `lmms.exe.11112.dmp`。
- 生产版 `D:\Program Files\LMMS\lmms.exe` 与本地 `build/Release/lmms.exe` 时间戳均为 `6AFC5030`，与 22:08 的崩溃版本一致；Release 构建没有 PDB，用导出表最近符号定位（RVA `0x8587c` ∈ `PatternStore::updateComboBox`，`0x85820`–`0x858E0`）。
- 35664：异常读取地址 `0x98`（空指针成员访问）。栈（返回地址最近导出符号）：`updateComboBox` ← `PatternTrack::PatternTrack` ← `Track::create(Type)` ← `Track::create(QDomElement)` ← `TrackContainer::loadSettings` ← `JournallingObject::restoreState` ← `Song::loadProject` ← `createNewProjectFromTemplate` ← `createNewProject` ← `QAbstractButton::clicked`（工具栏按钮）。
- 11112：同一调用链，但崩溃发生在 `findPatternTrack` 遍历 `s_infoMap` 期间，寄存器为 `0xff000000ff000000`，说明表中存在悬空或损坏的条目。
- 解析方法：用 Python 标准库按 minidump 格式读取异常线程上下文与栈，再结合 `dumpbin /exports` 映射；脚本未入库，可按此思路重做。

## 4. 相关代码事实

- `PatternTrack::PatternTrack`（`src/tracks/PatternTrack.cpp:44`）：编号取 `s_infoMap.size()`，随即调用 `updateComboBox()`。这个编号假定 `s_infoMap` 里只有当前 Song 的 Pattern 轨道，且编号连续。
- `PatternTrack::~PatternTrack`（同文件 :63）：先移除条目并把更大的编号减一，再 `removeTrack`、`updateComboBox`。
- `PatternStore::numOfPatterns()` = `Song::countTracks(Pattern)`，与 `s_infoMap` 是两个独立来源。
- `Song::restoreProjectState`（`Song.cpp:1212`）与 `loadProject` 是两套独立的重建代码。次要缺陷：其计数循环里 `count += ...` 修改了 `for` 循环自身的上界（`Song.cpp:1243–1260`），会使索引越界（`at()` 返回空节点，不会崩溃），加载进度计数也不正确。
- 聊天侧：`ChatToolDispatcher` 在 `projectAboutToReset` 时使回合失效。自 f5528ac94 起，命令执行期间的失败回滚不再取消整轮。回合结束后，聊天代码对工程重置不执行任何工程命令。转储栈中没有聊天模块帧，聊天模块不是崩溃的直接执行者。

## 5. 建议的复现与修复方向（待后续 Agent 执行）

复现（建议写成原生 QtTest，真实窗口，禁止 offscreen）：
1. 用含 Pattern 轨道的工程（或默认模板）执行若干 Agent 写命令：成功、失败回滚（例如加载不存在的预设）、`dryRun`、`history.undo`。
2. 每一步之后断言不变量：`s_infoMap.size() == Song::countTracks(Pattern)`，编号恰为 `0..n-1`，且每个 key 都属于 Song 的轨道列表。
3. 调用 `createNewProject()`（模板路径）验证不崩溃。先定位是哪一步首先破坏了不变量，再修复根因。

修复方向（需按根因选择，不要只做防御）：
- 根因修复：确保整体重建（`clearProject`/`restoreProjectState`）后 `s_infoMap` 与 Song 一致，例如重建后从 Song 的轨道顺序重排编号，或在 `clearProject` 中校验并清理残留条目。
- 防御（与根因修复一并做）：`updateComboBox` 及其他 `findPatternTrack` 调用点（`PatternStore.cpp:86/149/165`）对空指针判空，避免 UI 刷新直接导致进程崩溃。
- `restoreProjectState` 的计数缺陷另列后续事项，不在本次崩溃修复范围内。
- 验收：上述复现序列全部通过；新建、打开、撤销/重做 Agent 编辑无崩溃；Agent/MCP 既有回归（Chat 11、MCP 5、CommandBus/CoreCommands）通过；替换后真实应用操作验收记为 MANUAL/PENDING。

## 6. 已复现的根因与最小修复

使用实际 `GuiApplication`、MainWindow、PatternEditor 与 CommandBus 的原生 Windows QtTest 复现，未使用 offscreen，也未改生产安装。

1. `PatternTrackView` 析构/关闭以 `s_infoMap[m_patternTrack]` 读取编号。模型先删除、视图延迟销毁时，QMap 下标操作会重新插入已删除模型的 key。最小用例“创建 Pattern → clearProject → 处理 DeferredDelete”明确失败于登记表不变量，日志 `build/agent-pattern-view-repro-red.log`；改用 `constFind` 后同一用例通过，日志 `build/agent-pattern-view-green.log`。
2. 原 `Song::clearProject` 先清理已有视图，再处理排队事件。尚未执行的 trackAdded 回调会在视图清理之后创建旧轨道视图，随后模型被删除。完整窗口复现还在 InstrumentTrackView 定时更新与析构处访问失效模型；链接 MAP 定位用于确认调用点，没有保留临时调试代码。修复把已有 `sendPostedEvents` 移到视图清理之前，使排队视图在模型存活时建立，并随即一起清理。
3. `TrackContainerView` 原来只有 SVC 的排队创建回调携带 QPointer，其他轨道携带裸指针。现在统一复用已有 QPointer 检查，模型已删除则不创建视图。
4. `PatternStore::updateComboBox` 对 `findPatternTrack` 返回空值判空，避免不完整登记状态直接解引用；这是根因修复之外的调用点保护。

修改限上述四处生产文件及既有 `ChatProjectLifetimeTest`。未改事务、撤销语义、聊天布局、插件 ABI 或独立的计数缺陷。测试检查登记表连续编号、当前 Song 轨道成员关系，并实际经过成功、dryRun、撤销、重做、失败批次回滚后的模板新建与直接新建入口。名称断言同时检查成功编辑保留、回滚恢复，避免用“未崩溃”代替功能正确。

最终验证：Release 构建通过（现有 Python 进度脚本、jobs=2，126 秒），`build/Release/lmms.exe` 已更新；生命周期测试连续三轮各 12 passed/0 failed/0 skipped（18.78 秒）；既有 Chat 11、MCP 5、命令 3 的 CTest 19/19 通过（45.05 秒）。日志 `build/agent-new-project-final-build.log`、`build/agent-new-project-final-lifetime-tests.log`、`build/agent-new-project-final-regression-tests.log`。临时诊断日志代码已移除。生产替换后的整机操作仍为 MANUAL/PENDING。

## 7. 调用工具时能否永远不重载工程

结论：常规成功编辑已可直接操作现有对象；按现有事务与撤销实现，不能让全部失败调用、预演及历史操作永远不重建，同时保留现有原子回滚能力。

| 调用情况 | 当前行为 |
| --- | --- |
| 普通只读、普通成功写操作 | 不因快照保存/提交而重建；写操作先保存快照，直接编辑当前对象 |
| 没有活动批次时，未知命令、参数校验或 preflight 失败 | 直接返回错误，不进入回滚重建 |
| 活动批次中发生上述失败 | 整批回滚，可能包含此前已成功的写操作，因此恢复快照 |
| 事务性 handler 执行失败/抛异常 | 有活动事务时回滚快照，重建工程；执行前检查不能代替执行中失败恢复 |
| 多数事务性写操作 dryRun | 真正执行后恢复快照，因此重建；纯只读调用不按此路径处理 |
| 撤销/重做 Agent 快照检查点 | 恢复快照，重建工程 |
| 显式新建、清空、载入工程 | 按命令本意重置工程 |

依据为 `CommandBus::execute` 的 schema/preflight、ownsBatch、handler 失败及 `endBatch(!dryRun)` 分支，和 `ProjectJournal` 的 projectSnapshot 恢复路径。即使当前失败调用本身没有写入，若它结束的是已有批次，也不能省略对前序写入的回滚。第 2 节“任意写命令失败/每次预演”的概括应以上表条件为准。

可行选择：

- 最小方案：保留快照事务，修复重建时的生命周期；对已有证据支持的可预测失败使用纯 preflight，可减少无效重建。本次只落实生命周期修复，未新增命令预检。
- 全面取消隐式重建：写命令逐个实现可靠的逆操作/增量撤销，并覆盖脚本、插件状态与批次失败；或在隔离工程上预演，再设计安全提交。两种方案都涉及 CommandBus、ProjectJournal 和写工具边界，属于独立设计与实施范围。

不能以删除 rollback、把失败标为成功、禁止工具能力或跳过 dryRun 来实现“永不重载”。本次不实施事务架构改造；失败后的回滚仍会触发重置，但应由修复后的真实窗口生命周期正确承接。
