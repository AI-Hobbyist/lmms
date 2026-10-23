# LMMS MCP 工具补充计划书（列表发现 · SVS/SVC 选取与输入）

> **文档状态**：v0.4（M1、M2、M3 已实施并通过自动化验证）
> **编写日期**：2026-10-23（Asia/Seoul）
> **源码基线**：`44ccf432e4ba0e0a818e7a22a42a8624732c0141`，分支 `master`
> **勘察依据**：CodeGraph 符号检索 + 源码核对；现有实现结论见 [MCP 实现分析](MCP_IMPLEMENTATION_ANALYSIS.md)，接续约束见 [Agent 交接手册](AGENT_HANDOFF.md)
> **一句话目标**：在不改变现有 MCP 工具语义的前提下，新增“可发现、可选取、可输入”的资源列表与 SVS/SVC 编辑工具，让外部 Agent 能列出并选用乐器插件、歌声合成声库、歌声转换模型、我的预设、我的采样和本机目录文件。

---

## 目录

1. [背景、目标与边界](#1-背景目标与边界)
2. [现状核查（含 SVS 编辑结论）](#2-现状核查含-svs-编辑结论)
3. [设计约定](#3-设计约定)
4. [M1：六类列表发现工具](#4-m1六类列表发现工具)
5. [M2：SVS 选取与输入（编辑/填词/音符）](#5-m2svs-选取与输入编辑填词音符)
6. [M3：SVC 选取与转换输入](#6-m3svc-选取与转换输入)
7. [接入方式、文件范围与授权清单](#7-接入方式文件范围与授权清单)
8. [测试与验证计划](#8-测试与验证计划)
9. [风险与对策](#9-风险与对策)
10. [里程碑、验收与提交节奏](#10-里程碑验收与提交节奏)
11. [待用户决策](#11-待用户决策)
12. [附录](#12-附录)

---

## 1. 背景、目标与边界

### 1.1 用户需求

用户要求在现有 LMMS MCP 基础上补充以下几类工具，先形成计划书：

| # | 需求 | 本次目标 |
|---|------|----------|
| R1 | 乐器插件列表 | 列出可用乐器插件及其子插件键，供 `instrument.load` 选取 |
| R2 | 歌声合成（SVS）模型/声库列表 | 列出声库引擎与声库，供 SVS 轨道选取 |
| R3 | 歌声转换（SVC）模型列表 | 列出转换引擎、模型、权重、说话人，供 SVC 轨道选取 |
| R4 | 我的预设列表 | 列出用户与工厂预设，返回可直接传给 `instrument.loadPreset` 的路径 |
| R5 | 我的电脑（需手动指定目录） | 列出盘符/已知根目录，并按显式绝对路径列出目录内容 |
| R6 | 我的采样列表 | 列出用户与工厂采样，返回可直接传给 `sample.setFile` / `import.sampleToTrack` 的路径 |

用户同时要求：这些列表要“方便后续通过 MCP 加载、选取等”，因此本计划不仅包含只读列表，还规划了让列表结果真正可用的选取/输入命令（M2、M3）。

### 1.2 为什么需要新增，而不是复用现有工具

现有 MCP 已有 159 个工具（164 个注册命令），其中与本次需求最接近的是：

| 现有工具 | 能做什么 | 不能做什么 |
|----------|----------|------------|
| `effect.listAvailable(kind=instrument)` | 列出乐器插件与子插件键 | 名称为 effect 域；无搜索/分页/来源过滤；无 VST 扫描状态；不返回支持的扩展名 |
| `instrument.loadPreset` | 按绝对路径加载 `.xpf/.xml` 预设 | 不能发现预设；不标识 `.xiz/.lv2` 等插件私有预设 |
| `sample.setFile` / `import.sampleToTrack` | 按路径加载采样 | 不能发现“我的采样” |
| `svs.*` / `svc.*` | **不存在** | 不能列出/选取声库、转换模型，也不能输入歌词、音符、音素 |
| 文件浏览 | `FileBrowser` 仅是 GUI | MCP 无“我的电脑”目录浏览 |

因此新增工具不与现有工具重名，也不修改现有工具行为；重叠部分（乐器插件列表）在文档中明确“以新工具为更完整的只读视图，`effect.listAvailable` 保持原样”。

### 1.3 非目标

- 不修改现有 159 个工具的语义、参数、返回值和事务边界。
- 不实现 GUI 自动化、截图、Qt offscreen 验证；本计划全部命令均为非 GUI 数据路径。
- 不新增云端账号、聊天、模型客户端、审批/审计子系统、MCP resources/prompts/tasks 扩展。
- 不新增媒体推送或自动播放；选取后仍由调用方决定是否加载/合成/试听。
- 不在本计划内重构 `CoreCommands.cpp` 的既有实现；需要触碰时单独列授权项（§7.3）。
- 不承诺“我的电脑”工具可以写文件、递归整盘扫描或绕过操作系统权限。

### 1.4 完成定义

本计划的“计划阶段”完成定义：本文档经用户确认，明确 M1 范围、命名、旧文件授权清单和验收标准。

M1 实施完成定义：7 个只读发现工具注册成功，`tools/list` 可见且 annotations 为只读；不改动既有工具语义；单元测试与 SDK 冒烟通过；文档按授权更新。

M2/M3 完成定义见 §10。

---

## 2. 现状核查（含 SVS 编辑结论）

### 2.1 现有框架事实

| 项 | 现状 | 证据 |
|----|------|------|
| 命令注册 | 每个域在 `CommandBus` 构造时注册；工具目录由描述符自动生成 | `src/agent/CommandBus.cpp`、`src/agent/ToolRegistry.cpp` |
| 工具过滤 | `agent.*` 仅暴露 `agent.runScript` | `ToolRegistry::tools()` |
| 参数校验 | `CommandSchema` 统一校验；`additionalProperties:false` 由注册 helper 设置 | `src/agent/CommandSchema.cpp` |
| 事务 | `Mutability` + `TxScope`；只读命令不创建工程快照 | `include/agent/CommandBus.h`、`CommandBus::execute` |
| dryRun | 仅对可变命令自动加入 schema；执行后回滚 | `CommandBus::registerCommand`、`execute` |
| 文档 | `data/agent/command-reference.md` 由 `AgentHarness --manual` 生成 | `data/agent/README.md` |
| 工具数量断言 | `tests/src/agent/mcp/client_smoke.py` 写死 159 | 该文件第 36 行 |

### 2.2 六类数据源与现有覆盖

| 需求 | 权威数据源 | 现有 MCP 覆盖 | 缺口 |
|------|------------|---------------|------|
| R1 乐器插件 | `PluginFactory::descriptors(Plugin::Type::Instrument)`；子插件 `SubPluginFeatures::listSubPluginKeys`；Windows VST 目录 `Engine::vstCatalog()` | `effect.listAvailable(kind=instrument)` | 域名称、搜索/分页、来源过滤、扩展名、VST 扫描状态 |
| R4 我的预设 | `ConfigManager::userPresetsDir()` + `factoryPresetsDir()`；`*.xpf *.xml *.xiz *.lv2` | 无 | 列表、来源区分、可加载性标识 |
| R6 我的采样 | `ConfigManager::userSamplesDir()` + `factorySamplesDir()`；音频扩展名 | 无 | 列表、来源区分、递归与分页 |
| R2 SVS | `svs::Registry`：`engines()` / `voices()` / `scanning()` / `diagnostics()` | 无 | 引擎/声库列表、扫描状态 |
| R3 SVC | `svc::Catalog`：`engines()` / `capabilities["models"]` / `status(id)` | 无 | 引擎/模型/权重/说话人列表、在线状态 |
| R5 我的电脑 | `QDir::drives()`、`QDir::homePath()`、`QDir` 目录枚举 | 无 | 盘符/根、显式目录列表、分页与过滤 |

### 2.3 SVS 编辑、填词、音符输入的现状结论

**结论：目前没有任何 SVS 专用 MCP 工具；现有 MIDI/采样命令也不能操作 SVS 数据。** 具体证据如下：

| 操作 | 现有工具/代码路径 | 实际结果 | 结论 |
|------|-------------------|----------|------|
| 创建 SVS 轨道 | `track.create(type=...)` → `trackTypeFromName` | 只接受 `instrument/sample/automation`，报 `unsupported_track_type` | 未实现 |
| 创建 SVS 片段 | `clip.create` → `executeClipCreate` | 只处理 Instrument/Sample/Automation，其他轨道报 `unsupported_clip_type` | 未实现 |
| 选择声库 | 无 `svs.*` | 无入口 | 未实现 |
| 填写歌词 | `midi.addNotes` / `midi.updateNote` | 先 `resolveMidiClip`，`SVSClip` 不是 `MidiClip`，报 `wrong_clip_type` | 未实现 |
| 添加/删除/编辑音符 | 同上 | 同上 | 未实现 |
| 音素/发音覆盖 | 无 | 无入口 | 未实现 |
| SVS 参数/曲线 | 无 | 无入口 | 未实现 |
| 导入工程词典 | 无 | 无入口 | 未实现 |
| 触发/取消合成 | 无 | 无入口 | 未实现 |
| 查询 SVS 合成状态 | `track.get` / `clip.get` | `trackTypeName` 有 `svs`；`clipTypeName` 无 `svs`，返回 `type:"unknown"`；不含合成状态 | 部分/不足 |
| 移动/删除/改长度/静音片段 | `clip.move/setLength/setMute/remove` | 基于通用 `Clip` API，可作用于 SVS 片段 | 部分可用 |
| 重命名/静音/独奏/颜色/高度 | `track.*` | 基于通用 `Track` API，可作用于 SVS 轨道 | 部分可用 |

补充事实：

- `src/agent/` 中与 SVS/SVC 相关的代码只有 `trackTypeName` 的 `case Track::Type::SVS`；没有 `svs.` 或 `svc.` 命令注册。
- `trackTypeName` 没有 `Track::Type::SVC` 分支，因此 `track.get`/`track.list` 对 SVC 轨道会显示 `type:"unknown"`。这是既有显示缺口，本计划记录为后续修正项，不在未授权时改动。
- `clipTypeName` 没有 SVS/SVC 分支，`clip.get` 对这两类片段返回 `type:"unknown"`。同理。
- SVC 片段不是 `SampleClip`，因此 `sample.setFile` 也不能给 SVC 片段设置源音频；需要专用命令。

### 2.4 SVS/SVC 已有、可供 MCP 复用的内核能力

这些能力已经实现并有测试，不需要重写；MCP 层只需做参数校验、寻址、事务和 JSON 映射。

| 能力 | 现有 API |
|------|----------|
| SVS 声库注册与扫描 | `svs::Registry::instance()`：`voices()`、`engines()`、`scanning()`、`diagnostics()`、`plugin(id)` |
| SVS 轨道绑定声库 | `SVSTrack::bindVoice(pluginId, voiceId)`、`voice()`、`language()`、`capabilities()`、`capabilitiesReady()` |
| SVS 片段音符 | `SVSClip::notes()`、`setNotes()`、`setEditorData(notes, curves)`、`setNoteParameter()` |
| SVS 参数/曲线 | `SVSTrack::setParameter()`、`SVSClip::setParameter()`、`setGlobalParameter()`、`curves()` |
| SVS 词典/发音 | `SVSClip::importDictionary()`、`projectDictionaryData()`；`svs::resolvePronunciation()`；GUI 公共 helper `gui::editorPronunciation()` |
| SVS 填词分词 | `gui::SVSLyricEditor::splitLyrics()`（与钢琴窗“批量歌词”完全同一规则） |
| SVS 合成/状态 | `SVSClip::synthesize()`、`cancelSynthesis()`、`status()`、`audio()`、`synthesisSegments()` |
| SVC 模型目录 | `svc::Catalog::instance()`：`engines()`、`engine(id)`、`status(id)`、`capabilities["models"]` |
| SVC 选择规范化 | `svc::Catalog::requestSelection(saved, error)`（补默认值、校验权重/说话人/参数） |
| SVC 轨道选择 | `SVCTrack::setSelection()`、`selection()`、`setChunkConfig()` |
| SVC 片段源音频 | `SVCClip::setSourceFile()`、`sourceFile()`、`playback()`、`status()` |
| SVC 转换 | `svc::ConversionService::instance()`：`render(track)`、`renderClip(clip)`、`cancel(track)` |

### 2.5 SVS 音符/曲线的既有 JSON 形状（直接复用）

SVS 钢琴窗剪贴板已使用固定字段，MCP 不应另造一套：

```json
{
  "id": "uuid",
  "tick": 0,
  "duration": 48,
  "pitch": 60,
  "lyric": "la",
  "language": "",
  "pronunciation": "",
  "parameters": {},
  "phonemes": {},
  "xmlExtras": {}
}
```

- 剪贴板外层为 `{"schemaVersion":1,"anchor":tick,"notes":[...]}`；MCP 直接使用 `notes[]` 数组。
- 校验规则与钢琴窗粘贴一致：`tick/duration/pitch` 有限，`duration > 0`，`pitch` 在 `0..127`；MCP 额外要求 `tick >= 0` 且不超出工程最大长度。
- 曲线 JSON 使用 `svs::Curve::toJson()` / `fromJson()` 的同一对象（`id/unit/scope/type/mode/interpolation/points/gaps`）。

---

## 3. 设计约定

### 3.1 通用原则

1. **只读优先**：M1 全部为只读命令，`mutability=ReadOnly`、`scope=TxScope::None`，不创建工程快照，不修改工程。
2. **不重复造第二份注册表**：列表直接读取现有运行期数据源（`PluginFactory`、`ConfigManager`、`svs::Registry`、`svc::Catalog`、`QDir`），不维护静态清单。
3. **返回可执行的“手柄”**：每个条目都返回后续加载/选取所需的完整参数（插件 id + subKey、绝对路径、`{engine_id, model_id, weight_id, speaker_id}`、`{pluginId, voiceId}`）。
4. **稳定排序**：同一输入两次调用返回相同顺序；排序键写入文档，测试断言。
5. **分页**：文件型列表统一 `page`（从 0 开始）+ `pageSize`（默认 100，最大 500），返回 `total`/`returned`/`truncated`。
6. **不隐式做重活**：列表不实例化插件、不解码音频、不触发阻塞扫描；只报告当前快照与扫描状态。
7. **异步状态显式化**：SVS 声库扫描、SVC 引擎发现都是异步的；列表返回 `scanning`/`status`/`hint`，客户端轮询，不阻塞主线程等待。
8. **不改变既有事务语义**：工程编辑仍走 `TxScope::Single`；文件读取和异步派生工作不进入工程撤销。
9. **路径**：输入接受绝对路径（Windows 可含反斜杠）；输出使用 `QFileInfo::absoluteFilePath()`（Qt 在 Windows 上通常为 `/` 分隔）。所有输出为 UTF-8 JSON，中文路径不得损坏。
10. **错误可诊断**：失败返回稳定 `code`、可读 `message`、可选 `hint`；不吞掉底层错误。

### 3.2 命名与域

| 域 | 用途 | 新增命令 |
|----|------|----------|
| `instrument.*` | 乐器插件发现 | `instrument.listPlugins` |
| `preset.*` | 预设发现 | `preset.list` |
| `sample.*` | 采样发现 | `sample.list` |
| `svs.*` | 歌声合成 | `svs.listVoices` 等 |
| `svc.*` | 歌声转换 | `svc.listModels` 等 |
| `fs.*` | 本机目录浏览（只读） | `fs.listRoots`、`fs.listDirectory` |

命名理由：

- 域前缀与现有 `track/clip/midi/sample/effect` 风格一致。
- `instrument.listPlugins` 与既有 `effect.listAvailable` 区分，避免把乐器继续挂在 effect 域。
- `preset.list` / `sample.list` 语义直接；不叫 `listMine`，因为 `source` 参数可以区分用户/工厂。
- `fs.*` 明确是文件系统只读视图，不与工程对象混淆。

### 3.3 统一分页参数

```json
{
  "page": 0,
  "pageSize": 100
}
```

- `page`: integer，`minimum: 0`，默认 0。
- `pageSize`: integer，`minimum: 1`，`maximum: 500`，默认 100。
- 超出范围的页返回空 `items`，仍带 `total`，不报错。
- `keyword` 最大 128 字符；空串等同于未提供。
- 扩展名过滤只接受 `^[A-Za-z0-9]{1,16}$`，不接受通配符或路径分隔符。

### 3.4 统一错误码（M1）

| code | 触发条件 | hint 建议 |
|------|----------|-----------|
| `invalid_arguments` | 类型/范围/枚举错误 | 指向具体字段 |
| `directory_not_found` | `fs.listDirectory` 路径不存在 | 先用 `fs.listRoots`/上级目录确认 |
| `not_a_directory` | 路径存在但不是目录 | 使用父目录 |
| `access_denied` | 无读取权限 | 换目录或检查系统权限 |
| `plugin_factory_unavailable` | 插件工厂不可用 | 确认应用已初始化 |
| `catalog_unavailable` | SVC 目录不可用 | 检查 SVC 插件/设置 |
| `unsupported_extension_filter` | 扩展名含非法字符 | 只传字母数字 |
| `result_too_large` | 单页超过响应上限 | 减小 `pageSize` |

### 3.5 安全与隐私边界

- MCP 服务本身已是本机回环 + Bearer token + Host/Origin 校验（见 `HttpMcpServer`）。
- 现有工具已经可以读写用户显式指定的本机路径（`song.load/save`、`export.audio`、`import.*`、`sample.setFile`、`instrument.loadPreset/savePreset`）。新增的 `fs.*` 是**只读枚举**，不扩大写能力，但扩大了“可发现性”；必须在 `data/agent/MCP.md` 明确写出。
- `fs.listDirectory` 只列出直接子项，不递归；不自动展开符号链接目录。
- `sample.list` / `preset.list` 的递归扫描限制深度与条目数，跳过符号链接目录，避免环路。
- 不返回文件内容；不读取音频/工程内容（只读元数据）。
- 不把 token、环境变量、配置文件内容放进列表输出。

---

## 4. M1：六类列表发现工具

M1 共 **7 个命令**（6 类需求中的“我的电脑”拆成根目录与目录列表两个）。

### 4.1 `instrument.listPlugins`

**用途**：列出可加载的乐器插件，供 `instrument.load` 选择。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `keyword` | string | 否 | 空 | 不区分大小写匹配 plugin id / 显示名 / 描述 / 作者 / 子插件名与属性值 |
| `source` | enum `all/builtin/vst/lv2` | 否 | `all` | `builtin`=LMMS 原生描述符；`vst`=`vestige`；`lv2`=`lv2instrument` |
| `includeSubKeys` | boolean | 否 | `true` | 是否返回子插件键（VST、LV2 等） |
| `page` | integer | 否 | 0 | 分页 |
| `pageSize` | integer | 否 | 100 | 1..500 |

**返回**

```json
{
  "page": 0,
  "pageSize": 100,
  "total": 2,
  "returned": 2,
  "plugins": [
    {
      "plugin": "tripleoscillator",
      "name": "TripleOscillator",
      "description": "Three powerful oscillators you can modulate in several ways",
      "author": "Tobias Doerffel <tobydox/at/users.sf.net>",
      "version": 272,
      "supportedFileTypes": [],
      "source": "builtin",
      "hasSubPlugins": false,
      "subKeys": []
    },
    {
      "plugin": "vestige",
      "name": "VeSTige",
      "description": "VST-host for using VST(i)-plugins within LMMS",
      "author": "Tobias Doerffel <tobydox/at/users.sf.net>",
      "version": 256,
      "supportedFileTypes": ["dll", "vst3"],
      "source": "vst",
      "hasSubPlugins": true,
      "subKeys": [
        {
          "name": "Example Synth",
          "attributes": {
            "file": "C:/VST3/Example.vst3",
            "binarypath": "C:/VST3/Example.vst3/Contents/x86_64-win/Example.vst3",
            "fingerprint": "…",
            "format": "vst3",
            "classid": "0123456789abcdef0123456789abcdef",
            "architecture": "64",
            "identity": "vst3:64:0123…",
            "version": "1.0.0",
            "vendor": "Example"
          }
        }
      ]
    }
  ],
  "vstCatalog": {
    "available": true,
    "scanned": true,
    "busy": false,
    "entries": 1,
    "failures": 0,
    "published": 3
  }
}
```

**实现要点**

- 数据源：`PluginFactory::instance()->descriptors(Plugin::Type::Instrument)`。
- 子插件：`descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys)`；字段与 `effect.listAvailable` 保持一致，避免第二套键协议。
- VST 状态：仅 `#ifdef LMMS_BUILD_WIN32` 读取 `Engine::vstCatalog()->snapshot()`；其他平台 `available:false`、`scanned:false`。
- 排序：`name` 不区分大小写，其次 `plugin`；子插件按 `name`、再 `attributes["identity"]`。
- `source` 分类按描述符名称：`vestige`→`vst`，`lv2instrument`→`lv2`，其余→`builtin`；未知类型不猜测，按 `builtin` 处理并在描述中保留原始 `plugin`。
- 过滤 `source` 在分页前执行；`keyword` 同样在分页前执行。
- 不实例化插件、不扫描磁盘；VST 子插件只读已发布快照。
- 当 `vestige` 存在但未扫描时，返回 `subKeys: []` 且 `vstCatalog.hint` 提示先在 LMMS 中扫描 VST 目录；不自动触发扫描（避免主线程阻塞）。
- 与 `effect.listAvailable(kind=instrument)` 的关系：后者保持冻结；本工具是更完整的只读视图，文档中说明二者不互相替代。

### 4.2 `preset.list`

**用途**：列出“我的预设”（用户 + 工厂），返回可直接用于 `instrument.loadPreset` 的绝对路径。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `keyword` | string | 否 | 空 | 匹配文件名/相对路径 |
| `source` | enum `all/user/factory` | 否 | `all` | 对应侧栏的 User content / Factory content 过滤 |
| `extensions` | string[] | 否 | `["xpf","xml","xiz","lv2"]` | 覆盖默认扩展名；`cs.xml` 由 `xml` 覆盖 |
| `page` | integer | 否 | 0 | 分页 |
| `pageSize` | integer | 否 | 100 | 1..500 |

**返回**

```json
{
  "page": 0,
  "pageSize": 100,
  "total": 1129,
  "returned": 100,
  "truncated": false,
  "roots": [
    {"source": "user", "path": "C:/Users/me/lmms/presets/", "exists": true},
    {"source": "factory", "path": "D:/LMMS/data/presets/", "exists": true}
  ],
  "presets": [
    {
      "name": "Kick-4-your-Subwoofer",
      "path": "D:/LMMS/data/presets/AudioFileProcessor/Kick-4-your-Subwoofer.xpf",
      "relativePath": "AudioFileProcessor/Kick-4-your-Subwoofer.xpf",
      "source": "factory",
      "extension": "xpf",
      "size": 1234,
      "modified": "2026-10-04T01:23:11Z",
      "pluginHint": "AudioFileProcessor",
      "loadableBy": ["instrument.loadPreset"]
    },
    {
      "name": "0001-Arpeggio1",
      "path": "D:/LMMS/data/presets/ZynAddSubFX/Arpeggios/0001-Arpeggio1.xiz",
      "relativePath": "ZynAddSubFX/Arpeggios/0001-Arpeggio1.xiz",
      "source": "factory",
      "extension": "xiz",
      "size": 5678,
      "modified": "2026-10-04T01:23:11Z",
      "pluginHint": "ZynAddSubFX",
      "loadableBy": ["pluginPreset"]
    }
  ]
}
```

**实现要点**

- 根目录：`ConfigManager::inst()->userPresetsDir()`、`factoryPresetsDir()`；`exists:false` 时仍返回根，便于诊断。
- 递归扫描，跳过符号链接目录；最大深度 8、最大收集 20000 条；超出置 `truncated:true`。
- 扩展名匹配不区分大小写；`.cs.xml` 通过 `xml` 命中。
- `loadableBy`：
  - `xpf` / `xml` → `["instrument.loadPreset"]`（`DataFile::validate` 只接受这两种）。
  - `xiz` / `lv2` / 其他 → `["pluginPreset"]`，并提示由对应插件自己的预设界面加载，不能用 `instrument.loadPreset`。
- `pluginHint`：相对路径第一段（LMMS 保存预设时按乐器名建子目录）；没有子目录时为空。
- 排序：`source`（user 在前）→ `relativePath` 不区分大小写。
- 不做 XML 解析、不验证预设内容；`instrument.loadPreset` 仍会在加载时验证 `instrumenttrack` 根节点与本地插件路径。

### 4.3 `sample.list`

**用途**：列出“我的采样”（用户 + 工厂），返回可直接用于 `sample.setFile` / `import.sampleToTrack` 的绝对路径。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `keyword` | string | 否 | 空 | 匹配文件名/相对路径 |
| `source` | enum `all/user/factory` | 否 | `all` | 用户/工厂过滤 |
| `extensions` | string[] | 否 | 见下 | 音频扩展名 |
| `page` | integer | 否 | 0 | 分页 |
| `pageSize` | integer | 否 | 100 | 1..500 |

默认扩展名：`wav, ogg, flac, ds, spx, voc, aif, aiff, au, raw`；编译启用 MP3 时追加 `mp3`（与 `FileItem::defaultFilters()` 的音频集合一致）。

**返回**

```json
{
  "page": 0,
  "pageSize": 100,
  "total": 1005,
  "returned": 100,
  "truncated": false,
  "roots": [
    {"source": "user", "path": "C:/Users/me/lmms/samples/", "exists": true},
    {"source": "factory", "path": "D:/LMMS/data/samples/", "exists": true}
  ],
  "samples": [
    {
      "name": "kick_01",
      "path": "D:/LMMS/data/samples/kick_01.wav",
      "relativePath": "kick_01.wav",
      "source": "factory",
      "extension": "wav",
      "size": 44144,
      "modified": "2026-10-04T01:23:11Z",
      "loadableBy": ["sample.setFile", "import.sampleToTrack"]
    }
  ]
}
```

**实现要点**

- 根目录：`userSamplesDir()`、`factorySamplesDir()`；同样返回 `exists`。
- 递归扫描，跳过符号链接目录；深度上限 8、条目上限 20000。
- 不做音频解码、不读取采样率/时长；避免列表阶段引入 libsndfile 解码开销。需要元数据时由调用方加载后使用 `sample.getInfo`。
- 排序：`source`（user 在前）→ `relativePath` 不区分大小写。
- 文件名含中文/空格不做任何转义改写；JSON 原样输出。

### 4.4 `svs.listVoices`

**用途**：列出 SVS 引擎与声库，供 SVS 轨道选取。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `engine` | string | 否 | 空 | 只返回指定引擎 id |
| `keyword` | string | 否 | 空 | 匹配声库名/id/引擎名 |
| `language` | string | 否 | 空 | 只返回声库声明的语言（`Voice::language`） |
| `includeMetadata` | boolean | 否 | `false` | 是否返回完整 `Voice::metadata`（可能含音域等大对象） |
| `page` | integer | 否 | 0 | 分页（对声库） |
| `pageSize` | integer | 否 | 100 | 1..500 |

**返回**

```json
{
  "page": 0,
  "pageSize": 100,
  "total": 6,
  "returned": 6,
  "catalog": {
    "scanning": false,
    "scanningEngines": []
  },
  "engines": [
    {
      "id": "org.lmms.svs.example",
      "name": "SVS Example",
      "type": "example",
      "package": "D:/LMMS/build/Release/svs/SVSExample",
      "voiceCount": 6,
      "scanning": false,
      "diagnostics": []
    }
  ],
  "voices": [
    {
      "pluginId": "org.lmms.svs.example",
      "id": "voice-1",
      "name": "测试声库",
      "version": "…",
      "language": "zh",
      "defaultLyric": "a",
      "avatar": "…",
      "portrait": "…",
      "package": "D:/LMMS/build/Release/svs/SVSExample",
      "metadata": {},
      "selection": {
        "pluginId": "org.lmms.svs.example",
        "voiceId": "voice-1"
      }
    }
  ],
  "diagnostics": []
}
```

**实现要点**

- 首次调用会触发 `svs::Registry::voices()` 的目录扫描；扫描是异步的，可能先返回空列表。
- 返回 `catalog.scanning`、`scanningEngines`、`diagnostics` 和顶层 `hint`：
  - `scanning:true` → `"Voicebank scan in progress; retry after scanning is false."`
  - 扫完但为空 → `"No voicebanks found. Check SVS voicebank directories and rescan in Settings."`
- 不调用阻塞的 `Registry::refreshCatalog`；不等待扫描完成。
- 声库过滤在分页前执行；引擎列表始终完整返回（便于展示扫描状态）。
- `includeMetadata:false` 时不返回 `metadata` 字段（示例中为空对象仅表示结构；实现可省略该字段）。
- 排序：引擎 `name` → id；声库 `pluginId` → `name` → id，均不区分大小写。

### 4.5 `svc.listModels`

**用途**：列出 SVC 引擎、模型、权重、说话人，供 SVC 轨道选取。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `engine` | string | 否 | 空 | 只返回指定引擎 |
| `keyword` | string | 否 | 空 | 匹配模型名/id/分类/权重/说话人 |
| `category` | string | 否 | 空 | 匹配 `category_path` 中任一层 |
| `includeParameters` | boolean | 否 | `false` | 是否返回模型参数定义（可能较大） |
| `page` | integer | 否 | 0 | 分页（对模型） |
| `pageSize` | integer | 否 | 100 | 1..500 |

**返回**

```json
{
  "page": 0,
  "pageSize": 100,
  "total": 1,
  "returned": 1,
  "engines": [
    {
      "id": "reference",
      "name": "Reference identity",
      "status": "",
      "connected": true,
      "defaultAddress": "builtin://reference",
      "modelCount": 1
    }
  ],
  "models": [
    {
      "engine_id": "reference",
      "id": "identity",
      "name": "Reference identity",
      "category_path": [],
      "available": true,
      "reason": "",
      "multi_speaker": false,
      "weights": [],
      "speakers": [{"id": "0", "name": "Speaker 0", "available": true, "reason": ""}],
      "parameters": [],
      "selection": {
        "engine_id": "reference",
        "model_id": "identity",
        "speaker_id": "0"
      }
    }
  ],
  "diagnostics": []
}
```

**实现要点**

- 数据源：`svc::Catalog::instance().engines()`，每个 `EngineProfile::capabilities["models"]`。
- `connected`：`profile.api != nullptr`；`status`：`Catalog::status(id)`，模块引擎可能是 “Discovering backend capabilities”/“Not connected”/错误文本；内置 `reference` 引擎没有连接状态文本，此时为空字符串，客户端应以 `connected` 为准。
- 模型 `available`/`reason` 原样透传；`weights`/`speakers` 每个选项保留 `available`/`reason`。
- `selection` 只放稳定 id，不放连接 token；`parameters` 默认空数组，只有 `includeParameters:true` 时填充。
- 单个模型可能有多个权重；如果 `selection` 无法确定唯一权重，不擅自补 `weight_id`，由 `svc.setModel` 用 `requestSelection` 规范化。
- 引擎离线时仍返回上次已知模型（`capabilities` 保留），并带 `status` 提示；不把离线误报为“无模型”。
- 排序：引擎 `name`/id；模型 `category_path` 字典序 → `name` → id。

### 4.6 `fs.listRoots`

**用途**：对应侧栏“我的电脑/根目录”，返回盘符、Home 与 LMMS 已知根目录，便于后续 `fs.listDirectory` 手动指定目录。

**参数**：无。

**返回**

```json
{
  "roots": [
    {"name": "C:", "path": "C:/", "type": "drive"},
    {"name": "D:", "path": "D:/", "type": "drive"},
    {"name": "Home", "path": "C:/Users/me", "type": "home"}
  ],
  "knownBases": [
    {"name": "Working directory", "path": "C:/Users/me/lmms/", "type": "working"},
    {"name": "Data directory", "path": "D:/LMMS/data/", "type": "data"},
    {"name": "My Projects", "path": "C:/Users/me/lmms/projects/", "type": "projects"},
    {"name": "My Samples", "path": "C:/Users/me/lmms/samples/", "type": "samples"},
    {"name": "My Presets", "path": "C:/Users/me/lmms/presets/", "type": "presets"}
  ]
}
```

**实现要点**

- `QDir::drives()`（Windows）或 `/Volumes`（macOS，保持与 `MainWindow` 一致）；Home 用 `QDir::homePath()`。
- `knownBases` 使用 `ConfigManager` 的实际路径，不暴露 `data:/` 等 Qt 搜索路径前缀。
- 不检查每个目录是否可读（列表阶段不产生大量 stat）；`fs.listDirectory` 再报权限错误。

### 4.7 `fs.listDirectory`

**用途**：按用户显式指定的绝对目录列出直接子项（“我的电脑”浏览），供选择采样/预设/工程等。

**参数**

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `path` | string | **是** | — | 绝对目录路径；接受 `/` 或 `\` |
| `showHidden` | boolean | 否 | `false` | 是否包含隐藏项 |
| `directoriesOnly` | boolean | 否 | `false` | 只返回目录 |
| `extensions` | string[] | 否 | 空 | 只保留匹配扩展名的文件；目录始终保留 |
| `page` | integer | 否 | 0 | 分页 |
| `pageSize` | integer | 否 | 100 | 1..500 |

**返回**

```json
{
  "path": "D:/Music",
  "parent": "D:/",
  "exists": true,
  "total": 3,
  "returned": 3,
  "truncated": false,
  "entries": [
    {
      "name": "Samples",
      "path": "D:/Music/Samples",
      "type": "directory",
      "symlink": false,
      "size": 0,
      "modified": "2026-10-20T10:00:00Z",
      "readable": true
    },
    {
      "name": "voice.wav",
      "path": "D:/Music/voice.wav",
      "type": "file",
      "symlink": false,
      "extension": "wav",
      "size": 882000,
      "modified": "2026-10-21T09:30:00Z",
      "readable": true
    },
    {
      "name": "link-to-samples",
      "path": "D:/Music/link-to-samples",
      "type": "directory",
      "symlink": true,
      "size": 0,
      "modified": "2026-10-21T09:31:00Z",
      "readable": true
    }
  ]
}
```

**实现要点**

- 只列直接子项，不递归；排序：目录在前，文件在后，各自按名称不区分大小写。
- `path` 必须是绝对路径（`QFileInfo::isAbsolute()`），否则 `invalid_arguments`；不接受 `~`、环境变量或 `data:/` 前缀。
- 目录不存在 → `directory_not_found`；存在但不是目录 → `not_a_directory`；不可读 → `access_denied`。
- 符号链接：`symlink:true`，`type` 按目标类型给出；不自动进入链接目录（递归工具也不跟随）。
- `readable` 用 `QFileInfo::isReadable()`；不打开文件。
- `modified` 为 UTC ISO-8601（秒精度）；目录 `size` 为 0。
- 扩展名过滤只影响文件；扩展名列表非法时 `unsupported_extension_filter`。
- 大目录通过分页限制单次响应；不设置“整盘扫描”能力。

### 4.8 M1 工具总表

| # | 工具 | 类型 | 事务 | 数据源 |
|---|------|------|------|--------|
| 1 | `instrument.listPlugins` | ReadOnly | None | PluginFactory / VST catalog |
| 2 | `preset.list` | ReadOnly | None | user/factory presets dirs |
| 3 | `sample.list` | ReadOnly | None | user/factory samples dirs |
| 4 | `svs.listVoices` | ReadOnly | None | `svs::Registry` |
| 5 | `svc.listModels` | ReadOnly | None | `svc::Catalog` |
| 6 | `fs.listRoots` | ReadOnly | None | QDir / ConfigManager |
| 7 | `fs.listDirectory` | ReadOnly | None | QDir |

M1 后：MCP 工具 159 → **166**；注册命令 164 → **171**。

---

## 5. M2：SVS 选取与输入（编辑/填词/音符）

### 5.1 目标与缺口

M1 只能“看见”声库；要真正使用，需要能创建 SVS 轨道/片段、绑定声库，并输入歌词、音符、音素、参数、词典和曲线。§2.3 已确认这些目前都没有 MCP 入口。

M2 的核心原则：**不重写 SVS 内核**，只把 `SVSTrack` / `SVSClip` / `svs::Capabilities` 已有能力按 MCP 命令语义包装。

### 5.2 命令表

| # | 命令 | 类型 | 事务 | 说明 |
|---|------|------|------|------|
| 1 | `svs.createTrack` | Mutating | Single | 创建 SVS 轨道，可选同时绑定声库 |
| 2 | `svs.createClip` | Mutating | Single | 在 SVS 轨道创建片段 |
| 3 | `svs.setVoice` | Mutating | Single | 校验并绑定声库 |
| 4 | `svs.getTrack` | ReadOnly | None | 轨道/声库/参数/片段状态 |
| 5 | `svs.getCapabilities` | ReadOnly | None | 参数、语言、音素集、词典、诊断 |
| 6 | `svs.getNotes` | ReadOnly | None | 读取音符（可选解析发音） |
| 7 | `svs.addNotes` | Mutating | Single | 追加密音符 |
| 8 | `svs.updateNote` | Mutating | Single | 按稳定 id 局部更新 |
| 9 | `svs.removeNotes` | Destructive | Single | 按 id/范围删除 |
| 10 | `svs.setLyrics` | Mutating | Single | 批量填词（与钢琴窗同规则） |
| 11 | `svs.setNoteParameter` | Mutating | Single | 音符/音素参数 |
| 12 | `svs.setParameters` | Mutating | Single | 轨道/片段/全局参数 |
| 13 | `svs.importDictionary` | Mutating | Single | 导入工程词典 |
| 14 | `svs.getPronunciation` | ReadOnly | None | 解析歌词→读音/音素/候选 |
| 15 | `svs.synthesize` | Mutating | None | 触发合成（异步） |
| 16 | `svs.cancelSynthesis` | Mutating | None | 取消合成 |
| 17 | `svs.getStatus` | ReadOnly | None | 合成状态/缓存/片段 |
| 18 | `svs.getCurves` | ReadOnly | None | 读取可编辑曲线 |
| 19 | `svs.setCurve` | Mutating | Single | 替换一条曲线 |

> #18/#19 为“曲线输入”可选范围；若本阶段收紧，可延后到 M2b，不影响 #1–#17 的验收。

**推荐调用顺序**（能力/扫描是异步的，必须轮询）：

```text
svs.listVoices
  → svs.createTrack（可同时绑定声库）
  → svs.getTrack，轮询 capabilitiesReady == true
  → svs.createClip
  → svs.addNotes / svs.updateNote / svs.removeNotes
  → svs.setLyrics（可先 svs.getPronunciation 预览）
  → svs.setParameters / svs.setNoteParameter / svs.importDictionary
  → svs.synthesize
  → svs.getStatus（轮询到 Ready / Failed / Cancelled）
```

未绑定声库时允许建轨和写音符（便于先编排），但合成状态会明确提示缺声库；此时 `setLyrics` 需要声库能力，返回 `svs_capabilities_pending`。

### 5.3 关键 schema

#### 5.3.1 轨道/片段

```json
// svs.createTrack
{"name": "Lead vocal", "index": 2, "pluginId": "org.lmms.svs.example", "voiceId": "voice-1"}

// svs.createClip
{"track": 2, "position": 0, "length": 384, "name": "Verse"}
```

- `createTrack` 的 `pluginId`/`voiceId` 必须成对出现；出现即调用与 `setVoice` 相同的校验，并在同一事务内绑定。
- `createClip` 调用 `SVSTrack::createClip(TimePos(position))`，再设置长度与名称；返回 `clip` 详情与 `noteCount`。
- 不使用 `track.create`/`clip.create` 的现有分支，避免改动 `CoreCommands.cpp`（见 §7.3 的备选方案）。

#### 5.3.2 `svs.setVoice`

```json
{"track": 2, "pluginId": "org.lmms.svs.example", "voiceId": "voice-1"}
```

- 校验轨道是 `SVSTrack`；否则 `wrong_track_type`。
- 在 `svs::Registry::voices()` 中查找完全匹配；找不到时：
  - 若 `Registry::scanning()` 为真 → `svs_catalog_scanning`，hint 要求重试；
  - 否则 → `svs_voice_not_found`，返回可用 `pluginId/voiceId` 数量（不返回整表）。
- 成功调用 `bindVoice`；返回 `{pluginId, voiceId, name, version, language, capabilitiesPending:true}`。
- `dryRun` 由命令总线统一处理：执行后回滚工程；异步能力请求由轨道 generation 计数与 `QPointer` 保护，不会写回已回滚的对象。

#### 5.3.3 音符

```json
// svs.addNotes
{
  "track": 2,
  "clip": 0,
  "notes": [
    {"tick": 0, "duration": 48, "pitch": 60, "lyric": "你"},
    {"tick": 48, "duration": 48, "pitch": 62, "lyric": "好"}
  ]
}
```

- 字段与钢琴窗剪贴板一致（§2.5）；`id` 可省略，服务端用 `QUuid` 生成；省略 `lyric` 时用 `svs::Note` 的默认 `"la"`；重复 `id` 直接拒绝。
- 校验失败整批不写入（先全部校验，再 `setNotes` 一次）。
- `addNotes` 追加到现有向量末尾，不重排；返回新增 `ids` 与 `noteCount`。
- `updateNote` 参数：`{track, clip, id, fields:{...}}`；`id` 不存在 → `svs_note_not_found`。
- `removeNotes` 参数：`{track, clip, ids:[...]}` 或 `{track, clip, range:{start,end}}`；**必须**提供其一，避免误清空。范围选择“起点落入 `[start,end)` 的音符”，与 `midi.*` 的 range 语义一致；返回 `removed`/`ids`。
- `getNotes` 返回存储顺序、`index` 与稳定 `id`，并分页；客户端后续更新/删除应以 `id` 为准，不以 `index` 跨调用引用。
- 所有写入通过 `SVSClip::setNotes`，内部已调用 `addJournalCheckPoint()`，工程撤销可用。

#### 5.3.4 `svs.setLyrics`

```json
{"track": 2, "clip": 0, "text": "你好 世界", "noteIds": ["…", "…"], "skipContinuation": false}
```

- 分词直接调用 `gui::SVSLyricEditor::splitLyrics(text)`，与钢琴窗批量歌词完全一致（拉丁词、汉字、假名单位、`+`/`-`、数字）。
- 目标音符：`noteIds` 指定，或 `range` 选时间范围，缺省为片段内全部音符；按 `tick` 稳定排序后依序分配。
- `skipContinuation` 为真且 `capabilities.continuation` 非空时，跳过延音符与延音 token（与 GUI 相同）。
- 手动 `pronunciation`/`phonemes` 保留；只改 `lyric`。
- 返回 `{tokens, assigned, unusedNotes, remainingTokens, ids}`；不删多余音符、不补空歌词。
- 能力未就绪时返回 `svs_capabilities_pending`（因为需要延音标记）。

#### 5.3.5 参数/音素/词典/曲线

```json
// svs.setNoteParameter
{"track": 2, "clip": 0, "noteIds": ["…"], "parameter": "energy", "value": 0.7, "phoneme": false}

// svs.setParameters
{"track": 2, "clip": 0, "scope": "clip", "parameters": {"breathiness": 0.2}}

// svs.importDictionary
{"track": 2, "clip": 0, "path": "D:/dicts/zh.json"}

// svs.setCurve
{"track": 2, "clip": 0, "curve": {"id": "svs.pitch", "type": "float", "points": [], "gaps": []}}
```

- `setNoteParameter`/`setParameters` 先按 `capabilities.parameter(id, scope)` 校验 `writable/enabled/accepts`；未知参数、错误类型、越界值直接拒绝，不做静默截断。
- `scope`：`track` → `SVSTrack::setParameter`；`clip` → `SVSClip::setParameter`；`global` → `setGlobalParameter`。
- `importDictionary` 读取本地文件，大小上限 4 MiB（与 GUI 一致）；调用 `SVSClip::importDictionary`，返回 `{id, version, language, phonemeSet}`；错误原样返回（含语言/音素集不兼容）。
- `setCurve` 用 `svs::Curve::fromJson(object, curve, error, descriptor)` 校验；读取现有 `curves()`，替换同名项，再 `setEditorData(notes, curves)`。`curve:null` 表示删除该曲线（也走同一命令）。

#### 5.3.6 发音解析

```json
// 按已有音符解析（带上下文）
{"track": 2, "clip": 0, "noteId": "…", "candidates": true}

// 按原始歌词解析（可选 afterNoteId 提供前文上下文）
{"track": 2, "clip": 0, "lyric": "你好", "language": "zh", "afterNoteId": "…", "candidates": true}
```

- 有 `noteId`：调用 `gui::editorPronunciation(clip, note, candidates)`，复用 GUI 的上下文/词典逻辑。
- 只有 `lyric`：构造临时 `svs::Note`，按 `afterNoteId` 的 tick 排序定位，调用 `svs::resolvePronunciation`；项目词典与声库词典均参与。
- 能力未就绪（`capabilitiesReady()==false`）时返回 `svs_capabilities_pending`，不返回空音素冒充成功。
- 返回 `{text, source, diagnostic, phonemeSet, phonemes, candidates, generated, continuation}`。
- 只读，不改歌词或音素；调用方可先用它预览，再用 `setLyrics`/`setNoteParameter` 落地。

#### 5.3.7 合成与状态

```json
// svs.synthesize / svs.cancelSynthesis
{"track": 2, "clip": 0}

// svs.getStatus
{"track": 2, "clip": 0}
```

- `synthesize` 调用 `SVSClip::synthesize()`；`cancelSynthesis` 调用 `cancelSynthesis()`；二者不做工程快照（`TxScope::None`），但在项目批次内拒绝（`async_work_in_batch`），避免回滚与异步回调交叉。
- `getStatus` 返回：
  - 片段：`status`、`audioAvailable`、`cacheKey`、`segments`、`migrationDiagnostic`；
  - 轨道：`capabilitiesReady`、`capabilityDiagnostics`、`scanning`、每个片段的 `{clip, index, status, audioAvailable}`。
- 合成是异步的；命令返回时通常仍是 `Queued/Running`。客户端用 `svs.getStatus` 轮询，不新增 MCP tasks 协议扩展。

### 5.4 事务与撤销

| 操作 | 是否进入工程撤销 | 说明 |
|------|------------------|------|
| createTrack/createClip/setVoice/音符/歌词/参数/词典/曲线 | 是 | `TxScope::Single`，内部 checkpoint + 批次 |
| synthesize/cancelSynthesis | 否 | 派生数据；不改变工程序列化内容 |
| get*/list* | 否 | 只读 |

- `dryRun` 对所有 Mutating 命令可用：执行后回滚工程；异步派生工作通过 generation 校验失效。
- 批次内（`agent.runScript`）允许组合创建轨道→建片段→写音符→填词；`synthesize` 应放在批次提交之后（脚本层限制）。

### 5.5 M2 验收标准

1. 能在空工程上创建 SVS 轨道、绑定示例声库、创建片段、写入 ≥8 个音符与中文歌词，并通过 `svs.getNotes` 读回一致数据。
2. `svs.setLyrics` 与 GUI 批量歌词对同一输入产生相同分配（token 数、跳过延音、保留手动覆盖）。
3. `svs.getPronunciation` 返回音素/候选；`svs.setNoteParameter` 修改音素参数后 `getNotes` 可见。
4. `svs.importDictionary` 成功导入测试词典；不兼容语言/音素集返回可读错误。
5. `svs.synthesize` 在示例插件上进入 `Queued/Running`，轮询到 `Ready` 或明确失败；`cancelSynthesis` 能终止。
6. `history.undo` 能分别撤销建轨、绑定声库、歌词、音符、参数；`dryRun` 不留下工程修改。
7. 无 SVS 插件/声库时，命令返回结构化错误，不崩溃、不阻塞主线程。

---

## 6. M3：SVC 选取与转换输入

### 6.1 命令表

| # | 命令 | 类型 | 事务 | 说明 |
|---|------|------|------|------|
| 1 | `svc.createTrack` | Mutating | Single | 创建 SVC 轨道，可选同时设置模型 |
| 2 | `svc.createClip` | Mutating | Single | 在 SVC 轨道创建片段 |
| 3 | `svc.setModel` | Mutating | Single | 规范化并设置模型/权重/说话人/参数 |
| 4 | `svc.getTrack` | ReadOnly | None | 选择、分块配置、片段状态 |
| 5 | `svc.setSourceFile` | Mutating | Single | 给片段设置源音频 |
| 6 | `svc.setChunkConfig` | Mutating | Single | 设置三项分块参数 |
| 7 | `svc.convert` | Mutating | None | 启动转换（异步） |
| 8 | `svc.cancel` | Mutating | None | 取消转换 |
| 9 | `svc.getStatus` | ReadOnly | None | 转换状态/进度/缓存引用 |

**推荐调用顺序**：

```text
svc.listModels
  → svc.createTrack（可同时设置 selection）
  → svc.createClip
  → svc.setSourceFile
  → svc.setChunkConfig（可选）
  → svc.convert
  → svc.getStatus（轮询到完成/失败）
```

### 6.2 关键 schema

```json
// svc.createTrack
{"name": "RVC lead", "index": 3, "selection": {"engine_id": "ustc", "model_id": "model-1"}}

// svc.setModel
{"track": 3, "engine_id": "ustc", "model_id": "model-1", "weight_id": "w1", "speaker_id": "0",
 "parameters": {"pitch_shift": 0}}

// svc.setSourceFile
{"track": 3, "clip": 0, "path": "D:/vocals/take01.wav"}

// svc.setChunkConfig
{"track": 3, "silenceThresholdDbfs": -70, "lengthThresholdSeconds": 30, "forcedChunkSeconds": 10}

// svc.convert / svc.cancel / svc.getStatus
{"track": 3, "clip": 0}
```

**实现要点**

- `svc.setModel`：
  1. 校验 `engine_id` 存在、`model_id` 在该引擎的 `capabilities.models` 中。
  2. 若引擎 `api == nullptr`（离线）→ `svc_engine_offline`，返回 `status` 与 hint；不写入不可用的选择。
  3. 若模型声明了 `weights`/`speakers`，但调用方未给 `weight_id`/`speaker_id`：在只有一个可用候选项时自动补该 id；有多个候选且无 `fixed_speaker_id`/唯一默认时报 `svc_selection_ambiguous`，要求明确选择，不擅自猜。
  4. 调用 `svc::Catalog::requestSelection(saved, error)` 补默认参数、校验权重/说话人/枚举/范围/排除区间；错误映射为 `invalid_arguments` 或专用 code。
  5. 调用 `SVCTrack::setSelection(normalized)`；返回规范化 selection（客户端可直接复用）。
- `svc.getTrack`：返回 selection、引擎/模型名、`chunkConfig`、每个片段的 `{status, sourceFile, conversionComplete, conversionFailed}`、当前模型的参数定义（`parameterDefinitions`）。
- `svc.setSourceFile`：
  - 校验路径是可读文件；调用 `SVCClip::setSourceFile`（内部会解码音频，长文件可能耗时；文档注明这是阻塞操作）。
  - 返回 `{sourceFile, sampleRate, frames, durationSeconds}`（来自 `playback()->snapshot()->source`）。
  - 失败返回 `svc_source_decode_failed` + 状态文本。
- `svc.setChunkConfig`：三项写入 `svc::ChunkConfig`，`validate()` 不通过则拒绝（静音 −120..0 dBFS、长度 > 0、forced ≤ threshold、毫秒精度）；成功后 `invalidateClips()`。
- `svc.convert`：`clip` 指定时 `renderClip(clip)`，否则 `render(track)`；批次内拒绝（`async_work_in_batch`）；返回 `{status, generation?}`（若可读）。
- `svc.cancel`：`ConversionService::cancel(track)`；返回 `{cancelled:true}`。
- `svc.getStatus`：片段 `status/progress/finished/sourceFile/cacheReferences`；轨道汇总。轮询使用，不新增协议任务。

### 6.3 M3 验收标准

1. 创建 SVC 轨道、选择 `reference` 模型、创建片段、设置测试 WAV、启动转换并轮询到完成；音频数据非空。
2. `svc.setModel` 对未知模型/离线引擎/不可用权重/越界参数返回结构化错误，且不修改原选择。
3. `setSourceFile` 对缺失/不可解码文件返回错误，不留下半个片段状态。
4. `setChunkConfig` 的边界值（−120/0、forced=threshold、负值）按规则接受/拒绝。
5. `history.undo` 能撤销建轨、模型选择、源文件、分块配置；`svc.convert` 不进入撤销。
6. 无 SVC 引擎（只保留 reference）时，列表仍可用，错误可读。

---

## 7. 接入方式、文件范围与授权清单

### 7.1 新增文件（不改旧实现）

| 文件 | 里程碑 | 职责 |
|------|--------|------|
| `src/agent/DiscoveryCommands.h/.cpp` | M1 | 7 个列表命令与注册函数 |
| `src/agent/DiscoverySupport.h`（header-only） | M1 | 分页、扩展名过滤、时间格式、路径规范化 |
| `src/agent/CommandTargets.h/.cpp` | M2 | 轨道/片段寻址（index/name/path/parent），与 `CoreCommands` 语义一致 |
| `src/agent/SvsCommands.h/.cpp` | M2 | SVS 命令与注册函数 |
| `src/agent/SvcCommands.h/.cpp` | M3 | SVC 命令与注册函数 |
| `tests/src/agent/DiscoveryCommandsTest.cpp` | M1 | 列表命令回归 |
| `tests/src/agent/SvsCommandsTest.cpp` | M2 | SVS 输入/撤销/异步回归 |
| `tests/src/agent/SvcCommandsTest.cpp` | M3 | SVC 选择/转换回归 |
| `data/agent/mcp_discovery_example.py`（可选） | M4 | 只读列表示例，避免改现有 `mcp_client.py` |

### 7.2 必须改动的旧文件（最小）

| 文件 | 改动 | 行数量级 | 原因 |
|------|------|----------|------|
| `src/agent/CommandBus.cpp` | 增加 include；在构造函数末尾调用 `registerDiscoveryCommands(*this)`（M2/M3 同理） | 3～6 行 | 命令必须在总线上注册 |
| `src/agent/CMakeLists.txt` | 把新增 `.cpp` 加入 `LMMS_SRCS` | 3～6 行 | 构建接入 |
| `tests/CMakeLists.txt` | 把新增测试加入 `LMMS_TESTS`；为 SVS 测试设置 `LMMS_SVS_PLUGIN_DIR` 与依赖 | 6～10 行 | 测试接入 |
| `tests/src/agent/mcp/client_smoke.py` | 工具数量断言 159 → 166（M1）→ 183/185（M2）→ 192/194（M3） | 1 行/次 | 现有 SDK 冒烟断言 |
| `data/agent/command-reference.md` | 用 `AgentHarness --manual` 重新生成 | 生成文件 | 命令手册必须与注册表一致 |
| `data/agent/MCP.md` | 增加“发现类工具”和“本机目录只读浏览”边界说明 | 新增小节 | 客户端使用说明 |
| `data/agent/README.md` | 增加新域与异步轮询说明（可选） | 数行 | 开发说明 |

以上改动均**不改变**既有命令的名称、参数、返回值、事务和错误语义；仅追加注册/构建/测试/文档。按 [Agent 交接手册](AGENT_HANDOFF.md) 的冻结要求，实施前需要用户明确授权到具体文件范围。

### 7.3 可选旧文件改动（需单独决定）

| 文件/函数 | 改动 | 影响 | 建议 |
|-----------|------|------|------|
| `trackTypeFromName`（`CoreCommands.cpp`） | 增加 `svs`/`svc` | 让 `track.create` 直接创建两类轨道 | **不采用**；M2/M3 用独立 `svs.createTrack`/`svc.createTrack`，零改动 |
| `executeClipCreate`（`CoreCommands.cpp`） | 增加 SVS/SVC 分支 | 让 `clip.create` 支持两类片段 | **不采用**；用独立 `svs.createClip`/`svc.createClip` |
| `trackTypeName`（`CoreCommands.cpp`） | 增加 `Track::Type::SVC` | 修正 `track.get/list` 的 `type:"unknown"` | 可作为后续小修；需授权 |
| `clipTypeName`（`CoreCommands.cpp`） | 增加 SVS/SVC | 修正 `clip.get/list` 的 `type:"unknown"` | 可作为后续小修；需授权 |
| `CoreCommands` 的寻址 helper | 提取到共享文件 | 避免 `CommandTargets` 复制逻辑 | 需较大重构；本计划不采用，先复制并以测试保证一致 |

**决策建议**：M1/M2/M3 全部采用“新文件 + 独立 create 命令”，把旧文件改动限制在 §7.2 的注册/构建/测试/文档。这样即使 M2/M3 后续调整，也不会触碰 6 千行的 `CoreCommands.cpp`。

### 7.4 注册顺序与工具数量

```cpp
// CommandBus::CommandBus()
registerCoreCommands(*this);
registerExportCommands(*this);
registerMetadataCommands(*this);
registerScriptCommands(*this);
registerHighLevelCommands(*this);
registerDiscoveryCommands(*this); // M1
registerSvsCommands(*this);       // M2
registerSvcCommands(*this);       // M3
```

| 阶段 | 新增命令 | MCP 工具总数 | 注册命令总数 |
|------|----------|--------------|--------------|
| 现状 | 0 | 159 | 164 |
| M1 | 7 | 166 | 171 |
| M2（不含曲线） | 17 | 183 | 188 |
| M2（含曲线） | 19 | 185 | 190 |
| M3 | 9 | 192（含曲线 194） | 197（含曲线 199） |

> 数量按现有规则推导：`agent.*` 只暴露 `agent.runScript`；新增命令都不以 `agent.` 开头，因此 1:1 进入 MCP 目录。最终以运行时 `tools/list` 为准。

---

## 8. 测试与验证计划

### 8.1 测试原则

- 全部为无 GUI 测试（`QCoreApplication` 或 `AgentHarness`），不涉及 Qt 窗口；因此不触发 Qt GUI/offscreen 规则。没有 UI 变更，不需要真实窗口截图。
- 测试只证明其覆盖的数据路径；不把“注册成功”当成“真实插件/真实声库已验证”。
- 真实原生插件（SVSExample、SVC 引擎）能加载时做集成；不能加载时记录 `MANUAL/PENDING`，不伪造通过。

### 8.2 M1 测试用例

| 用例 | 断言 |
|------|------|
| 注册与注解 | 7 个名称存在、唯一、`readOnlyHint:true`、`inputSchema.type=="object"`、无 `dryRun` |
| `fs.listRoots` | 至少包含 Home；Windows 至少一个盘符；`knownBases` 路径非空 |
| `fs.listDirectory` 正常 | 临时目录内建子目录、`.wav`、`.txt`、隐藏文件；默认不返回隐藏；`directoriesOnly` 只返回目录；`extensions` 只保留匹配文件 |
| `fs.listDirectory` 分页/排序 | 页面边界正确；目录在前、文件在后；两次调用顺序相同 |
| `fs.listDirectory` 错误 | 相对路径 → `invalid_arguments`；不存在 → `directory_not_found`；文件 → `not_a_directory` |
| `preset.list` | `source:factory`、`LMMS_DATA_DIR` 指向仓库 `data` 时 `total > 0`；`xpf` 条目 `loadableBy` 含 `instrument.loadPreset`；`xiz` 不含 |
| `sample.list` | `source:factory` 时 `total > 0`；条目路径存在；`extensions:["wav"]` 全部为 wav |
| `instrument.listPlugins` | 插件目录可用时包含 `tripleoscillator`；`source:vst` 只返回 `vestige`；无 VST 目录时 `vstCatalog.available/scanned` 合理，不抛错 |
| 确定性 | 同一输入连续两次调用，除 `modified` 外 JSON 深度相等 |

### 8.3 M2 测试用例

| 用例 | 断言 |
|------|------|
| 建轨/建片段 | `svs.createTrack` 后 `track.get` 类型为 `svs`；`svs.createClip` 后 `clip.list` 可见 |
| 声库绑定 | 用 `LMMS_SVS_PLUGIN_DIR` 指向 `SVSExample`；`svs.setVoice` 成功；错误声库返回 `svs_voice_not_found` |
| 音符 CRUD | 添加 8 个音符；读回 id/tick/duration/pitch/lyric 一致；更新一个；删除一个；`noteCount` 正确 |
| 填词 | 中文/英文/假名混合文本；与 `SVSLyricEditor::splitLyrics` 的 token 数一致；`unusedNotes` 正确；手动音素保留 |
| 发音 | `getPronunciation` 返回非空音素集；未知歌词有 diagnostic 而非空成功 |
| 参数/词典/曲线 | 合法值写入并读回；未知参数/越界值拒绝；词典语言不兼容报错；曲线通过 `Curve::fromJson` |
| 合成 | 示例插件下轮询到 `Ready` 或明确失败；`cancelSynthesis` 后状态为取消/失败而非卡死 |
| 事务 | 每个 Mutating 命令 `dryRun` 无残留；`history.undo` 逐步恢复 |
| 无插件 | 清空 `LMMS_SVS_PLUGIN_DIR` 后列表为空、选择报错，不崩溃 |

### 8.4 M3 测试用例

| 用例 | 断言 |
|------|------|
| 建轨/建片段 | `svc.createTrack`/`createClip` 成功 |
| 默认模型 | `svc.setModel` 选择 `reference/identity` 成功；返回规范化 selection（含 speaker_id/parameters） |
| 错误模型 | 未知引擎/模型/权重/说话人/越界参数均拒绝且不修改原选择 |
| 源音频 | 临时 WAV 设置成功，返回采样率/frames；缺失文件报错 |
| 分块配置 | 默认值与边界值；非法值拒绝 |
| 转换 | reference 引擎完成转换；状态从进行中到完成；取消可用 |
| 事务 | 选择/源文件/分块可撤销；转换不进入撤销 |

### 8.5 构建与执行约束

- 所有 configure/build/test 按项目要求在前台 PowerShell 执行，先 dot-source `./buildtools/Enter-LmmsEnvironment.ps1`，使用 `2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8`，立即检查 `$LASTEXITCODE`。
- 复用现有 `build` 与 `build/Release/lmms.exe`、`build/Release/plugins`；不新建替代部署目录。
- 单测通过 CTest 运行；SDK 冒烟用 `tests/src/agent/mcp/client_smoke.py`。
- 记录命令、退出码、日志路径；失败先读 `build.log`。

### 8.6 文档与 SDK 回归

- `AgentHarness --manual` 重新生成 `data/agent/command-reference.md`，核对新增命令数量与 schema。
- `AgentHarness --tools` 导出 `tools/list`，核对 M1=166（M2=183/185，M3=192/194）。
- `client_smoke.py` 更新断言并增加一次只读发现调用（`instrument.listPlugins`、`sample.list`、`fs.listDirectory`）。
- 人工验收：真实客户端（官方 Python SDK）连接、列表、选取、简单工程编辑；界面/试听如无法执行记录 `MANUAL/PENDING`。

---

## 9. 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| SVS 声库扫描是异步的，首次调用可能为空 | 客户端误判“没有声库” | 返回 `scanning`/`hint`；文档要求轮询；测试用 `QTRY` 等扫描结束 |
| SVC 引擎离线，模型来自上次能力快照 | 选择失败或信息过期 | 返回 `status`/`connected`/`reason`；`setModel` 明确拒绝离线 |
| VST 目录未扫描 | `vestige.subKeys` 为空 | 返回 `vstCatalog.scanned/busy` 与 hint；不在列表命令里触发阻塞扫描 |
| 大目录/大采样库扫描耗时 | 主线程卡顿 | 限制深度/条目数、分页、跳过符号链接目录；记录耗时；必要时后续改为异步 |
| 非法/恶意扩展名过滤 | 路径遍历或 glob 注入 | 只接受字母数字扩展名；不使用通配符拼接 |
| 符号链接环路 | 递归扫描死循环 | 递归工具不跟随符号链接目录；`fs.listDirectory` 只列直接子项 |
| 中文/空格路径 | JSON 或文件系统失败 | 全程 `QString`/UTF-8；测试含中文文件名；不使用本地 8.3 名 |
| 复制 `CoreCommands` 寻址逻辑产生漂移 | 同一 track 在不同命令下解析不一致 | `CommandTargets` 与现有语义逐条对照测试（index/name/path/parent/歧义/越界） |
| `dryRun` 与异步合成/能力刷新交叉 | 回滚后仍有回调 | 轨道/片段 generation + `QPointer`；`synthesize/convert` 禁止在批次内；测试 dryRun |
| 现有 `client_smoke.py` 数量断言 | SDK 冒烟失败 | 每个里程碑同步更新断言并在同一提交记录 |
| 冻结文件改动 | 违反交接约束 | §7.2 列明最小改动并取得授权；不顺手重构；不修改既有命令语义 |
| `clip.get/track.get` 对 SVS/SVC 显示 `unknown` | 客户端误判类型 | 新域 `getTrack/getStatus` 返回正确类型；把通用显示修正列为后续授权项 |
| 音频解码阻塞（`setSourceFile`） | 大文件时命令耗时 | 文档明确；限制可解码文件大小？先记录耗时，必要时后续加异步/上限 |

---

## 10. 里程碑、验收与提交节奏

### 10.1 里程碑

| 里程碑 | 范围 | 交付物 | 验收 |
|--------|------|--------|------|
| **P0 计划确认** | 本文档 | 命名、范围、授权清单确认 | 用户确认 §11 决策 |
| **M1 列表发现** | 7 个只读命令 | 新文件 + 注册 + 测试 + 文档 | §8.2 全绿；159 个既有工具不变；SDK 166 |
| **M2 SVS 输入** | 17（+2 可选）个命令 | SVS 建轨/建片段/声库/音符/歌词/音素/参数/词典/合成 | §8.3 全绿；示例插件集成；撤销/dryRun |
| **M3 SVC 输入** | 9 个命令 | SVC 建轨/建片段/模型/源音频/分块/转换 | §8.4 全绿；reference 引擎闭环 |
| **M4 收尾** | 文档、SDK、全量回归 | 更新手册/交接/分析；完整 CTest | 全量 CTest + SDK 冒烟；平台限制记录 |

### 10.2 每个里程碑的提交节奏

按项目要求，每个里程碑通过自动化验证后：

1. 更新计划/进度记录（本文件或交接文档）。
2. 检查 `git status`、`git diff`，确认只包含本里程碑文件。
3. 单独提交，不把多个里程碑合并。
4. 推送当前分支并确认成功。
5. 再进入下一里程碑。

保留工作区中他人未跟踪文件，不清理、不纳入提交。

### 10.3 验证状态标注

- 自动化通过：记录命令、退出码、日志、工具数量。
- 真实插件/声库/引擎不可用：记录 `MANUAL/PENDING`，不报告通过。
- Qt5/Linux/macOS 未构建：记录环境限制，不以源码兼容代替构建通过。

### 10.4 实施记录

| 里程碑 | 状态 | 证据 |
|--------|------|------|
| M1 列表发现 | **已实施** | 7 个只读工具注册；`--tools` 导出 **166** 个工具；`--manual` 生成 **171** 个命令；`DiscoveryCommandsTest` 通过（CTest 1/1）；`client_smoke.py` 数量断言更新为 166 并增加发现调用 |
| M2 SVS 输入 | **已实施** | 17 个 `svs.*` 命令（曲线延后）；`--tools` 导出 **183** 个工具；`--manual` 生成 **188** 个命令；`SvsCommandsTest` 8 项通过（建轨/片段/声库/音符/填词/参数/撤销/合成）；`client_smoke.py` 更新为 183 |
| M3 SVC 输入 | **已实施** | 9 个 `svc.*` 命令；`--tools` 导出 **192** 个工具；`--manual` 生成 **197** 个命令；`SvcCommandsTest` 7 项通过（建轨/选型/源音频/分块/转换/撤销/类型校验）；`client_smoke.py` 更新为 192 |
| M4 收尾 | 待实施 | — |

M1 构建记录：`cmake --build build --config Release --target AgentHarness` 与 `DiscoveryCommandsTest` 均 `EXITCODE=0`；日志为 `build-m1.log`、`build-m1-test.log`、`test-m1.log`（本地未跟踪产物）。

M2 构建记录：`AgentHarness`、`SvsCommandsTest`、`SVSExample` 构建均 `EXITCODE=0`；`SvsCommandsTest` 8 passed / 0 failed / 0 skipped（含 `SVSExample` 合成到音频可用）；日志为 `build-m2.log`、`build-m2-test.log`、`test-m2.log`。`svs.getCurves/setCurve` 未在本阶段实施，留待后续。

M3 构建记录：`AgentHarness`、`SvcCommandsTest` 构建均 `EXITCODE=0`；`SvcCommandsTest` 7 passed / 0 failed / 0 skipped（reference 引擎转换完成）；日志为 `build-m3.log`、`build-m3-test.log`、`test-m3.log`。真实 RVC/USVC 后端未接入本机自动化，标为 `MANUAL/PENDING`。

---

## 11. 待用户决策

| # | 决策 | 推荐 |
|---|------|------|
| D1 | 工具命名是否采用 `instrument.listPlugins` / `preset.list` / `sample.list` / `svs.listVoices` / `svc.listModels` / `fs.listRoots` / `fs.listDirectory` | 采用 |
| D2 | 是否授权 §7.2 的最小旧文件改动（注册/CMake/测试/文档） | 授权；否则无法接入 |
| D3 | `fs.listDirectory` 是否允许任意绝对路径只读浏览 | 允许（只读、只列直接子项）；如只允许已知根，需要额外白名单设计 |
| D4 | SVS/SVC 创建轨道/片段：独立命令 vs 扩展 `track.create`/`clip.create` | 独立命令（零改动 `CoreCommands.cpp`） |
| D5 | 是否同时修正 `trackTypeName`/`clipTypeName` 的 SVS/SVC 显示 | 可延后；新域命令已返回正确类型 |
| D6 | M2 是否包含曲线 `getCurves/setCurve` | 建议包含；若范围收紧可延后 |
| D7 | 本阶段只做 M1，还是 M1–M3 一起实施 | 建议先 M1 验收，再按需启动 M2/M3 |

---

## 12. 附录

### 附录 A：现有工具逐项核对（避免重复造）

| 需求 | 现有工具 | 是否可替代新工具 | 结论 |
|------|----------|------------------|------|
| 乐器插件 | `effect.listAvailable(kind=instrument)` | 部分 | 保留；新增更完整视图 |
| 预设发现 | 无 | — | 新增 |
| 采样发现 | 无 | — | 新增 |
| SVS 列表 | 无 | — | 新增 |
| SVC 列表 | 无 | — | 新增 |
| 目录浏览 | 无 | — | 新增 |
| 预设加载 | `instrument.loadPreset` | 是（按路径） | 不重复 |
| 采样加载 | `sample.setFile` / `import.sampleToTrack` | 是（按路径） | 不重复 |
| 乐器加载 | `instrument.load` | 是（按 plugin/subKey/path） | 不重复 |

### 附录 B：SVS 声库字段（`svs::Voice`）

| 字段 | 说明 |
|------|------|
| `pluginId` | 声库所属 SVS 引擎 id |
| `id` | 声库稳定 id |
| `name` | 显示名 |
| `version` | 版本/内容指纹 |
| `language` | 默认语言 |
| `defaultLyric` | 新音符默认歌词 |
| `avatar` / `portrait` | 资源路径或 `svs-resource:` |
| `package` | 声库包目录（本机路径） |
| `metadata` | 插件声明元数据（作者、描述、音域等） |

### 附录 C：SVC 模型字段（能力 JSON）

| 字段 | 说明 |
|------|------|
| `models[].id` / `name` | 稳定 id / 显示名 |
| `models[].category_path` | 侧栏分组路径 |
| `models[].available` / `reason` | 可用性与原因 |
| `models[].multi_speaker` | 是否展开说话人 |
| `models[].weights[]` | 权重候选（id/name/available/reason） |
| `models[].speakers[]` | 说话人（id/name/available/reason） |
| `models[].parameters[]` | 模型级参数（可选） |
| `parameters[]` | 引擎级参数定义（type/scope/unit/default/min/max/step/options/visible_when/enabled_when） |
| `limits` | `min_seconds/max_seconds/max_upload_bytes` |

### 附录 D：SVS/SVC 编辑现状证据（文件与符号）

| 结论 | 证据 |
|------|------|
| 无 SVS/SVC MCP 命令 | `src/agent/` 仅 `trackTypeName` 的 SVS 分支；无 `svs.`/`svc.` 注册 |
| `track.create` 不支持 SVS/SVC | `trackTypeFromName` 仅 `instrument/sample/automation` |
| `clip.create` 不支持 SVS/SVC | `executeClipCreate` 仅三类分支 |
| `midi.*` 不能操作 SVS 音符 | `resolveMidiClip` 要求 `MidiClip`；`SVSClip` 继承 `Clip` |
| `sample.*` 不能操作 SVC 片段 | `resolveSampleClip` 要求 `SampleClip`；`SVCClip` 继承 `Clip` |
| SVS 内核可编辑 | `SVSClip::setNotes/setNoteParameter/setParameter/importDictionary/synthesize`；`SVSTrack::bindVoice/setParameter` |
| SVC 内核可选择/转换 | `SVCTrack::setSelection/setChunkConfig`；`SVCClip::setSourceFile`；`svc::ConversionService` |

### 附录 E：M1 命令 schema 摘要

| 命令 | 必填 | 主要字段 |
|------|------|----------|
| `instrument.listPlugins` | 无 | `keyword/source/includeSubKeys/page/pageSize` |
| `preset.list` | 无 | `keyword/source/extensions/page/pageSize` |
| `sample.list` | 无 | `keyword/source/extensions/page/pageSize` |
| `svs.listVoices` | 无 | `engine/keyword/language/includeMetadata/page/pageSize` |
| `svc.listModels` | 无 | `engine/keyword/category/includeParameters/page/pageSize` |
| `fs.listRoots` | 无 | — |
| `fs.listDirectory` | `path` | `path/showHidden/directoriesOnly/extensions/page/pageSize` |

---

**本计划书不构成对现有实现、测试、构建配置或产品行为的修改授权。** 实施范围、旧文件改动与里程碑启动均以用户明确授权为准。
