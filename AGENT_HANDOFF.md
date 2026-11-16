# 当前接续：内置 Agent 聊天 M1 DONE（2026-11-16）

- 目标active，完整范围仍为根目录《内置Agent聊天框实施计划书.md》M1～M5。M1自动验证、原生布局核对及提交/推送检查点完成；M2～M5未开始。
- Penpot已恢复，实际读取文件509f8c9c-0e43-811a-8008-f310b05ce9db、页面509f8c9c-0e43-811a-8008-f310b05ce9dc，08/09/10画板核对成功。后续每次开始/恢复实施及新界面调整仍必须实时读取对应画板；不可连接停止实施。
- M1已实现原生右侧Dock、48像素收缩、1100×1000分离与回接、会话JSON与独立管理页、全局prompt路径及批准设置、默认data/system_prompt.txt、默认主题30个SVG。发送/附件/模型等M2能力仍明确禁用；M3/M4未接入。
- 最新Release增量2/2成功；CTest ChatStoreTest/ChatDockTest 2/2通过（3.55秒），QtTest合计8 passed、0 failed、0 skipped。真实Windows后端，未用offscreen或Computer Use。
- 验证覆盖会话导入新ID/导出脱敏、路径错误、设置磁盘恢复、默认请求批准、真实输入、收起/最小宽度不重叠、分离/回接、会话管理搜索/多选归档持久化。修复Qt6 QPoint重载歧义及主题滚动容器/浮动Dock边框1像素偏移。最终源码已编译，不再处于此前未复验状态。
- 日志build/agent-chat-m1-final-build.log与agent-chat-m1-final-tests.log；截图doc/agent/chat-{m1,floating,collapsed,sessions,settings,approval}-native.png。已查看真实截图，中文正常；之前分离窗口屏幕外黑区截图已由完整可见截图取代。
- 程序D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe，默认提示词在其旁data/system_prompt.txt。原生插件DLL本轮未重建/替换，生产D:/Program Files/LMMS未改，无新ZIP。
- 人工完整DAW操作、多屏/跨DPI验收MANUAL/PENDING，不作阻塞也不记PASS。组件测试不证明后续真实API、工具或下载集成通过。
- 当前master；实现提交fd978a60d075838a4d9238aae33d49c695bc8499已推送，git ls-remote确认同哈希。随后文档收尾提交包含本状态，以git log为准。保留未跟踪.codegraph/、build.log、build-driver.log。未清理他人修改。不要频繁列对话清单，必要的编译前并发检查只输出冲突数。
- 下一最小任务：实时探测Penpot进入M2，读取02/03/04/13及01附件/消息布局，按计划实现API配置、凭据保存、协议请求/流式及附件。不要重复开发M1，不提前接入M3/M4。复用现有Python进度脚本、SDK环境、原build目录与Windows工具链、jobs2及前台PTY完整日志。禁止WSL、Qt offscreen和无关重构。
- 进度记录doc/agent/chat-implementation-progress.md；无可靠额度剩余接口，不猜测。M1～M5目标尚未完成，不标记complete。

---

以下为历史进展，当前状态以上方记录为准。
# 当前任务：持续实施内置 Agent 聊天框（2026-11-15，历史进展）

- 用户已授权根据计划创建持续目标并执行，目标为 M1～M5 实施及 Windows 增量包交付。编译优先使用 buildtools/build.py 及独立进度窗口，保留完整前台日志。
- 新增搜索要求已写入计划并成功提交推送：SearXNG 默认选中，支持自部署实例 URL；Brave 为可选后端。计划提交 1cc9cb715，origin/master 推送成功。
- 最新计划补充已单独提交推送 cf8d8d140，HEAD与origin/master一致；包括精确排版、实时Penpot强制门槛、主题SVG、联网默认关闭及模型自带搜索。M1源码仍未提交，工作区保留，不得误认为阶段检查点已完成。
- 当前 M1 IN PROGRESS：用户最新要求“排版一比一复刻 Penpot”，取代仅作布局参考的近似排版解释。先重做精确布局核对，尚未提交 M1，禁止进入 M2。ChatStore、ChatDock、默认提示词及测试已实现，发送仍禁用。
- 左上新会话与列表按钮分行已 RED（实际 y=66 vs 21）→最小修复后 GREEN，日志 build/agent-chat-toolbar-red.log 与 build/agent-chat-m1-tests.log。该 GREEN 只证明两按钮同排，不证明已一比一复刻；按 Penpot 实际几何重新设计后须同步更新该断言的对应目标。
- M1 Release 构建及 2/2 CTest 通过，QtTest 共 7 passed、0 failed、0 skipped；设置默认批准、保存/磁盘恢复、会话存储/导出、提示词错误、原生收缩/分离/回接及输入已覆盖。实际 Windows 组件窗口已截图检查中文与 Penpot 排版；没有运行 offscreen。完整 DAW 集成及人工项目待后续阶段，不将组件测试冒充完整端到端。
- 无构建或测试仍在执行。构建命令 python buildtools/build.py --config Release --target lmms ChatStoreTest ChatDockTest --jobs 2 --window；最终增量 4/4、退出码 0。程序 build/Release/lmms.exe，提示词 build/Release/data/system_prompt.txt。日志 build/agent-chat-m1-build.log、build/agent-chat-m1-incremental.log、build/agent-chat-m1-tests.log；截图 doc/agent/chat-m1-native.png。
- build.py 新增完整输出回传，保留独立窗口 tqdm，以满足当前 AGENTS.md 前台日志规则。配置 build/configure.ps1 已成功（原有未跟踪本地脚本），未建立新构建目录。
- 编译进度修复：删除“vcxproj 修改时间晚于 obj 就需重编”的误判；首轮预估 312/实际 238 的证据后，实际增量 3/3、2/2、4/4 均准确。首次受限 MSBuild 依赖阶段无进展已 Ctrl+C 中止，不计通过；后续非沙箱前台构建正常。
- 下一步：M1 检查点提交并确认推送后进入 M2（协议、连接/凭据设置、流式与附件）。M1 提交哈希以 Git 为准，不用历史 HEAD 冒充当前状态。继续按里程碑验证/提交/推送。
- M2～M5 NOT STARTED，暂无可交付聊天功能 ZIP。生产安装未修改。未下载 aria2c/7-Zip。
- 用户新增强制界面核对：涉及界面构建必须与 Penpot 对照排版，已补入计划 §4。已重新导出查看主画板，按其结构将新会话移入左侧列表顶部、设置移至列表底部；构建后必须看原生截图再记录差异，不能仅凭代码或测试断言通过。
- 用户新增联网搜索开关：默认关闭，默认后端仍为 SearXNG；关闭时模型工具列表及执行门同时禁止搜索，关闭取消在途搜索。已补计划 §9/M4 验证。素材下载仍按明确请求与批准模式处理。
- 用户继续要求兼容模型自带搜索：已补计划，按实际厂商/模型能力启用；共用联网开关。托管搜索必须在请求发送前批准整轮搜索范围，不能远端执行完再补批准；关闭时移除托管搜索请求参数。
- 用户要求尽量用自绘 SVG 图标按钮，并接入 LMMS 默认主题现有图标位置。已新增 data/themes/default/agent_chat_*.svg（10 个），复用 embed::getIconPixmap/artwork/ToolButton，主题 CSS 仅加按钮尺寸。不要另建 agent-chat 图标目录或独立主题加载机制。
- 最新强制要求：后续排版必须持续按需请求 Penpot，不得脑补。每次调整前读取对应画板节点坐标/尺寸/层级，必要时导出，再原生截图复核。已实读主画板聊天区：640×878，头栏54，会话栏180×824；输入框相对聊天区(200,681,420,155)，底部操作y787，底部提示y847。实际数据已存 Penpot storage.chatLayout；需继续读取具体节点，不能将此前约560宽的近似布局记为通过。
- 用户要求先整理 Penpot：13 张画板内部内容已分别包进一个同名“界面内容”组，画板 ID 不变。后续读取不能只看 board.children 的直接元素，应先进入唯一内容组或递归遍历；主画板内容组 f437380c-ddd4-80ad-8008-f32bcbb5d246。代码布局重做尚未开始，当前无构建运行。
- 用户新增执行门槛：每次开始/恢复实施、进入新阶段前实际探测 Penpot；不可连接则立即停止实施，记录 BLOCKED（Penpot 不可连接），不得用缓存/记忆继续。此项是工具依赖，不属于人工验收。当前探测成功，文件 509f8c9c-0e43-811a-8008-f310b05ce9db，页面 509f8c9c-0e43-811a-8008-f310b05ce9dc。
- 最新实施：新增 ChatDockLayout.cpp，按主画板 640×878/头栏54/会话栏180/输入框(200,681,420,155)重排；QDock 自绘标题栏提供 sizeHint，已修复 Qt 原先不为头栏保留54像素的问题。ChatDockTest 在1600×878真实窗口中断言这些几何及头栏同行，2/2 CTest通过（2.34秒），QtTest仍7 passed。收起栏按画板09改为48宽、Agent/AI/展开入口；精确浮动布局仍待做。
- 最新无活动构建。程序及两个测试目标均构建成功；最后一次为 ChatTitleBar 尺寸修正1/1增量。截图 build/agent-chat-m1-native.png 为640×878原生聊天区截图，已同步 doc/agent/chat-m1-native.png。新图标现为19个SVG，部分直接使用 Penpot 自绘路径，仍放默认主题原位置。
- 下一最小工作：重新实际探测Penpot，读取画板05/06后将 showSettings 旧表单替换为其原生同排版页面；再按08实现会话管理布局、按10对齐分离窗口，核对09收起状态及窄宽度。未达最新一比一要求前，M1保持IN PROGRESS，不提交阶段代码、不进入M2。主画板实读几何已归档 doc/agent/chat-layout-baseline.json，不能用归档代替后续实时连接检查。
- 最新接续进展：已新增 ChatSettingsDialog.h/.cpp，替换 showSettings 旧表单；按实时画板05/06实现1600×1000客户端布局、232宽导航、提示词卡片与路径动作、两张批准选择卡、原生保存/取消按钮。M2/M4导航及网络开关暂禁用，联网开关显示默认关闭；不将未来能力当作已实现。
- 设置/批准页 Release 构建14/14成功，CTest 2/2通过（2.63秒）；原生窗口断言提示词框(306,352,610,43)、请求批准卡(280,208,850,222)，验证默认请求批准、切换无需批准、保存/磁盘恢复。修正主题头/底栏 palette(dark)误用后，仅重跑 ChatDockTest 1/1通过（2.39秒）。真实图已查看中文与排版并归档 doc/agent/chat-settings-native.png、chat-approval-native.png；几何档案 chat-settings-layout-baseline.json。当前无活动构建/测试。
- 下一最小工作改为：探测Penpot后实现画板08独立会话管理页（实时坐标已读，不能仅用现有右键菜单代替）；再对齐10浮动窗口、复核09收起栏及窄宽度/高度。当前全局/批准主结构已对齐，仍有06右侧“工程内部工具”图标使用聊天图标应替换成Penpot的midi路径。M1仍未提交，未进入M2。
- 图标当前新增7个（key/sliders/download/globe/play/check/close），总计26个agent_chat_*.svg，均放data/themes/default。后续编译仅在必要时做同工程并发检查，不向用户列出Codex对话清单，只报告冲突数。
- 本轮保留计划阶段及历史修改，不清理 .codegraph/、build.log、build-driver.log。额度接口未提供可靠剩余百分比，不猜测，不因人工验收阻塞。

---

# 已完成：内置 Agent 聊天框实施计划（2026-11-15）

- 用户已明确验收 Penpot 布局，要求编写实施计划，暂不实现。补充要求：Penpot 仅作布局参考，实际优先复用 LMMS 已有组件、主题及交互，缺少控件允许自绘。
- 已完成仓库根目录 `内置Agent聊天框实施计划书.md`，覆盖原生侧栏/分离、会话、附件、思考等级、API 格式、默认请求批准、内部工具直调、外部 prompt 路径、搜索及 aria2c/7-Zip 便携工具、阶段验收与交付。
- 计划 M1～M5 全部 NOT STARTED；未改源码/构建配置，未创建 data/system_prompt.txt，未下载工具，未编译测试。实际 GUI 和 API 尚未实施，不能报告通过。
- 当前分支 master，HEAD 9d3e541aa；本次计划及交接修改未提交/推送。保留原有未跟踪 .codegraph/、build.log、build-driver.log。
- 下一步：用户启动实施后从 M1 开始；先核对既有实现冻结范围及最小接入改动，严格逐阶段验证、提交、推送，不提前实施 M2。人工验收未执行记 MANUAL/PENDING，不作为受阻条件。
- 下方静态设计记录为历史基线，其中“等待设计验收/不得编写计划”已被本次用户验收及写计划请求取代；后续仍禁止未经启动请求开始实现。

---

# 已完成：内置 Agent 聊天界面静态设计（2026-11-15）

- 上一轮 MCP 重载修复：用户已明确反馈“验收成功”；旧记录中的 MANUAL/PENDING 已由该用户反馈完成验收。
- 本轮仅制作 Penpot 静态设计，不实现、不编译、不下载或安装工具，不提前编写实施计划书。等待用户验收设计后，才写计划书。
- Penpot 当前文件：新建文件 2；fileId 509f8c9c-0e43-811a-8008-f310b05ce9db；pageId 509f8c9c-0e43-811a-8008-f310b05ce9dc。
- 已保存版本：LMMS Agent 静态稿 V1 · 待用户验收。共 13 张画板、43 个自绘 SVG 图标；交互数 0、flow 数 0；文件 validate 无错误，画板边界与固定文本无溢出。已导出检查关键页面的中文和版式。
- 页面：01聊天工作区；02API设置；03厂商预设；04生成参数；05全局设置；06批准模式；07素材下载；08会话管理；09侧栏收起；10分离窗口/联网下载；11控件状态；12SVG图标；13请求格式。
- 默认右侧常驻，可收起、调宽、分离/吸附；支持附件、思考等级、会话列表和管理。静态稿中按钮均未绑定动作。
- 认证只做 API 地址+Key，不做 OAuth/订阅登录。参考 CherryHQ/cherry-studio 的 packages/provider-registry/data/providers.json，已展示 DeepSeek 等预设和常见协议；供应商、协议、模型能力分开。
- 工程工具直接调用已有内部工具接口，不通过 HTTP MCP 回环；允许按请求联网搜索及外部素材下载。
- 批准模式仅“请求批准 / 无需批准”，默认“请求批准”；工程操作、联网搜索、下载沿用当前模式。
- System Prompt 默认外置 data/system_prompt.txt，全局设置只设置路径、选择文件和重新读取，不输入全文。默认提示词草案已保存在 Penpot 文件插件元数据 lmms-agent-system-prompt-draft；尚未写入程序文件。
- 素材下载工具拟使用 aria2c，压缩包需要解压；工具放在 lmms.exe 旁 tools 目录按文件夹分开，优先绿色/便携版，例如 tools/aria2/aria2c.exe、tools/7zip/7z.exe 及配套文件。用户已允许实现阶段提前下载所需工具；当前未下载。
- 素材保存位置在设计中由请求单独指定；tools 被理解为下载/解压可执行工具目录。若用户另有目录含义，按验收反馈调整。
- 汇总要求保存在 Penpot 文件插件元数据 lmms-agent-design-requirements。聊天主画板 f437380c-ddd4-80ad-8008-f3157cd3a933；API f437380c-ddd4-80ad-8008-f3179ef136fc；全局 f437380c-ddd4-80ad-8008-f318e954b9da；下载 f437380c-ddd4-80ad-8008-f319b4f67a30。
- 当前阶段产出已完成，用户验收状态 PENDING；下一步仅根据反馈改设计，明确验收通过后再编写计划书。不要擅自开始实现。
- 本轮仓库仅更新此交接记录；未修改 LMMS 源码、配置、data/system_prompt.txt 或工具部署，未提交/推送设计阶段代码。

---

以下为上一轮已经交付的工程记录：
# 当前交接：MCP 工程重载修复与增量包已交付

更新：2026-11-15（Asia/Seoul）。目标已完成，没有剩余实施任务。

## 提交与生产状态

- 分支 master；编译脚本提交 d7984eaf8、修复提交 59b7d93c4 均已成功推送 origin/master。
- 本次收尾仅更新本手册和 doc/agent/MCP-reload-fix.md；最终记录提交哈希以 Git 为准。
- 生产目录 D:\Program Files\LMMS 未修改。用户自行关闭应用、备份旧 EXE 后安装。
- 保留未跟踪 .codegraph/（原有索引）、build.log 和 build-driver.log；未清理他人内容。

## 已完成和已验证

- 修复非 track 子节点加载死循环；清理/恢复期间加载状态保护；命令总线在校验及事务处理前返回 project_busy；加载失败恢复 loading/journalling；正常刷新工程长度。
- 新增 6 个针对性测试场景，含普通成功编辑保留对象、回滚期间未知命令重入和长度恢复。
- 受控 RED：旧源码的非轨道节点加载超过 8 秒不返回，加载失败残留状态，回滚重入以 0xC0000005 崩溃。finally 恢复修复源码后 GREEN。
- 最终 CommandBusTest、CoreCommandsTest、McpProtocolTest、McpQueueTest 4/4 passed（2.61 秒）；QtTest 合计 43 passed、0 failed、1 skipped。跳过项为未设置 LMMS_AGENT_PLUGIN_TEST_PATH 的原生插件 DLL 集成用例，未记为通过。
- Release 主程序 build/Release/lmms.exe 编译及 --version 实际执行成功；版本 1.3.0-alpha.1.1244+59b7d93，Qt 6.10.3 / Windows x64，7098368 字节。
- 新旧主程序各 3120 个导出，移除 0；生产中 59 个依赖主程序 DLL 的导入均满足；实际 LMMS_HAVE_* 能力差异 0。没有改公开类数据布局，没有重建或替换原生插件 DLL。
- 编译脚本支持 Debug/Release、目标和并发命令行参数、按本轮源码/对象文件更新计数、CPP 说明、独立窗口、前台 PowerShell 完整 build.log、完成或失败后 5 秒自动退出。实际 1/1、2/2、8/8、14/14 编译计数通过；未变化文件增量计划为 0；完成/失败窗口自动退出约 5.5/5.3 秒。
- 仅 Release 实际编译验证；未额外全量编译 Debug。先前用户取消的一轮不计为通过。

## ZIP 交付

- 路径：D:\UserData\Desktop\Project\lmms\build\packages\lmms-mcp-reload-20261115-213416-win64.zip
- 大小：2800234 字节。
- ZIP SHA256：3ED61182B81FCA98EBD21087887E49EEAA5D25C06D2DC426EBB916C6FF143A18。
- 新 EXE SHA256：C37F7CAE9BA47850DBF2BC2645A0F6F5C5815914A5B52394B2D08DDD70ADB2B1。
- 产品源码 59b7d93c4，打包时跟踪工作区无改动。基线来自实际安装的 367 个二进制 SHA256，未采用旧 manifest。
- 包仅替换 lmms.exe，移除文件数 0；插件、引擎、运行库、配置和工程保持原位。
- 现有安装器 VerifyOnly 通过，未写生产目录。安装说明在 ZIP 内；需要管理员权限覆盖 Program Files，安装前备份原 EXE 便于回滚。

## 未验证和实际后续

- 原生 GUI/用户实际操作验收：MANUAL/PENDING，不作为阻塞条件，不记为 PASS。没有使用 offscreen，没有启动生产 LMMS。
- 普通成功编辑直接操作当前模型；dryRun、失败回滚和快照撤销保留原有整工程恢复语义，没有擅自重写所有工具。
- 记录既有路径差异：RemoteCatalogIo 依赖产物在 build/plugins/RemoteCatalogIo.exe；本增量包不替换它，生产已有 helper 保留。其部署路径整改是独立后续，不属于本修复。
- 没有剩余实施；只需用户按包内说明安装并按需做人工验收，不编造下一阶段。

## 证据与约束

- 正式记录 doc/agent/MCP-reload-fix.md；脚本说明 buildtools/README-build.md；打包入口 buildtools/New-McpReloadPackage.ps1。
- build/final-agent-tests.log、build/baseline-red-test.log、build/release-build.log、build/release-version.log、build/production-abi-check.json、build/incremental-package.log、build/incremental-verify.log。
- 继续遵守 AGENTS.md：原生 Windows、禁止 WSL/offscreen、同工程任务检查、原部署路径、按需测试、前台 PowerShell Tee-Object 日志、范围控制。
- git status 使用 --ignore-submodules，diff 使用 -c diff.ignoreSubmodules=all，避免 doc/wiki 子模块权限错误。

---
以下保留此前任务交接记录：
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
- **M5b 通道重排与类型名（2026-10-24）**：新增 `mixer.moveChannel`（左右各一格，master 固定 0 位）；`trackTypeName`/`clipTypeName` 补 `SVC` 与 SVS/SVC 片段分支，`track.get`/`clip.get` 不再返回 `type:"unknown"`。新测试 `tests/src/agent/TrackClipTypeTest.cpp`。
- **M5c 轨道路由覆盖 SVS/SVC（2026-10-24）**：`track.setMixerChannel` 由“仅 instrument/sample”扩展为同时接受 SVS/SVC 轨道（复用各自 `mixerChannelModel()`），`track.get` 对这四类轨道都返回 `mixerChannel`；错误提示同步更新。按要求不再新增并行命令 `mixer.setTrackChannel`。这是 SVS/SVC 加效果的唯一可行路径（它们没有轨道级 EffectChain）。
- **M5d 寻址诊断与时间单位（2026-10-24）**：`resolveTrack` 按名字查找失败时不再返回 `Track -1 does not exist`，改为 `Track "<name>" was not found by name.` 并附索引/名字/路径提示（`CoreCommands.cpp` 与 `CommandTargets.cpp` 两处一致）；`data/agent/MCP.md` 新增「Time and tick units」，写明 `DefaultTicksPerBar = 192`（4/4 下 1 小节 192 tick、1 拍 48 tick、1 个 16 分步 12 tick）。
- **M5e 电平观测（2026-10-24）**：新增 `mixer.getLevels`（只读），返回单通道或全部通道的 `peakLeft`/`peakRight`/`peak`/`peakDb`（dBFS，静音钳到 −120）以及 mute/solo 状态，数据源为 `MixerChannel::m_peakLeft/m_peakRight`。此前 `mix.gainStaging` 必须由调用方提供 `peakDb`，agent 无法自行测量。
- **M5f 修复调音台结构命令引发 GUI 崩溃（2026-10-24）**：`mixer.addChannel`/`removeChannel`/`moveChannel` 原先只改模型，而 `MixerView` 用与通道号一一对应的 `m_mixerChannelViews` 向量；模型多出通道后该向量滞后，用户随后在界面点「+」或 Shift+Insert 触发 `MixerView::addNewChannel()` 时 `m_mixerChannelViews[newChannelIndex]` 越界，实测进程直接消失。现在三处结构变更在 GUI 存在时调用 `MixerView::refreshDisplay()`（同时经 `updateMaxChannelSelector()` 刷新各轨道通道取值范围），无 GUI 时保持仅模型路径。无头回归 15/15 通过；**GUI 真实窗口复验 MANUAL/PENDING**（开发版运行未启用 MCP 服务，未能在本轮驱动）。
- **M5g 钢琴窗音高线（2026-10-24）**：新增 `svs.setPitchCurve`（新文件 `src/agent/SvsPitchCommands.cpp`、测试 `SvsPitchCommandsTest`，8/8 通过）。`svs.pitch` 是编辑器曲线（`unit=semitone`、`scope=clip`、绝对半音），不在声库的 `capabilities.parameters` 里，因此既有 `svs.setCurve` 一律返回 `svs_parameter_not_found`——这是真实缺口。新命令直接按 `Curve::fromJson(..., descriptor=nullptr)` 写入 `clip->curves()`，`relative:true`（默认）把点值解释为相对持续音符的偏移，`clear:true` 删除曲线。
- 当前运行时目录：**197 个 MCP 工具 / 202 个注册命令**；`data/agent/command-reference.md` 已由 `AgentHarness --manual` 重新生成，`data/agent/MCP.md` 已补发现/Mixer/SVS/SVC 说明。
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
