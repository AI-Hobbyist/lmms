# 分析：工程重载触发条件，及 Agent 执行后“新建工程”卡死闪退

日期：2026-11-17。性质：只读分析，未修改产品代码。供后续 Agent 修复使用。

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
- 顺带修复 `restoreProjectState` 中修改循环上界的计数缺陷。
- 验收：上述复现序列全部通过；新建、打开、撤销/重做 Agent 编辑无崩溃；Agent/MCP 既有回归（Chat 11、MCP 5、CommandBus/CoreCommands）通过；替换后真实应用操作验收记为 MANUAL/PENDING。
