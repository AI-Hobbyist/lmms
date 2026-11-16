# 内置 Agent 聊天实施进度

依据：仓库根目录《内置Agent聊天框实施计划书.md》。2026-11-15 用户已授权持续实施；编译优先使用带进度条的 Python 脚本。

## 当前状态

- 计划已提交推送：`1cc9cb715`，`origin/master`。
- M1：DONE。实现提交fd978a60d已推送origin/master，远端哈希一致；最新Release增量2/2成功，CTest 2/2通过（3.55秒），QtTest合计8 passed、0 failed、0 skipped。
- M2～M5：NOT STARTED。尚无模型聊天、工程工具批准或素材下载的可运行交付。
- 搜索要求：联网搜索可开关且默认关闭；默认后端 SearXNG，可指定自部署实例 URL；Brave 及受支持模型自带搜索可选。托管搜索须在发送启用它的模型请求前批准，M4 实现。

## M1 范围与复用

新增 ChatStore、ChatDock、默认外置提示词、ChatStoreTest、ChatDockTest。主窗仅追加停靠入口，没有新增 MainWindow 数据字段；不改现有工程命令或 HTTP MCP。按钮复用 LMMS ToolButton，输入、列表、分割区、消息与对话框使用当前主题下的原生 Qt Widgets。

会话位于 QStandardPaths 的用户应用数据目录，使用原子 JSON 写入；支持新建、搜索、重命名、置顶、归档、导出、删除及多选管理。当前发送禁用，界面明确标记 API 尚未接入。

## Penpot 排版核对

用户要求所有涉及界面构建的阶段均核对 Penpot。主画板 ID：`f437380c-ddd4-80ad-8008-f3157cd3a933`，已重新导出查看。

| 对照项 | 当前落实 | 原生窗口结果 |
| --- | --- | --- |
| 01 左会话、右消息、底部输入 | 按最新一比一要求，读取实时几何后重排640×878聊天区，头栏54、会话栏180 | 真实窗口关键坐标PASS；新建(14,70)、输入框(200,681,420,155)，不再以先前近似同行断言代替原型 |
| 05/06 全局设置/批准 | 已替换为ChatSettingsDialog，按实时画板实现1600×1000页面、导航、提示词卡、批准选择卡和底部操作区 | 关键坐标及设置保存/恢复自动PASS，真实截图已与Penpot对照；API/下载页面及运行能力属于M2/M4，仍未实现 |
| 08 会话管理 | 原生列表、存储与右键菜单已有，独立管理页尚未按原型完成 | 实施未完成 |
| 09 收缩 | 窄 Agent 入口，恢复展开宽度 | 原生窗口自动通过 |
| 10 分离 | Qt 停靠浮动与回接，共享同一视图与状态 | 浮动与回接通过；1100×1000原型排版实施未完成 |
| 11 控件状态 | API 未接入时禁用发送，并显示状态 | 截图核对通过 |

必要差异：采用 LMMS 当前主题和原生控件，不照搬设计稿配色、字体及固定像素。设置先以原生对话框承载 M1 字段，M2 接入连接及参数页；不将尚未实现的画板功能计为通过。原生截图为 `chat-m1-native.png`（同目录），来自生产聊天组件在真实 Windows 测试窗口中的显示；左侧空白为组件测试容器，并非完整 DAW。完整应用集成在 M5 验证。

按用户补充，10 个自绘 SVG 放入默认主题原有图标位置 `data/themes/default/agent_chat_*.svg`，复用 embed/artwork 及 ToolButton。主题 CSS 只添加图标按钮尺寸，继承原有状态与配色；未新增单独图标加载系统。

## 构建与验证

- 原目录 configure 已成功。
- 首次沙箱内 MSBuild 停留在依赖阶段、无 CPP 编译；已前台 Ctrl+C 中止，不计通过。
- 原目录非沙箱前台构建通过，命令：`python buildtools/build.py --config Release --target lmms ChatStoreTest ChatDockTest --jobs 2 --window`。最终增量 4/4，退出码 0。
- 运行记录保持前台完整 stdout/stderr，build.log 捕获；独立窗口显示 tqdm 与 CPP。
- ChatStoreTest、ChatDockTest：2/2 CTest passed（1.65 秒），QtTest 合计 7 passed、0 failed、0 skipped。实际 Windows 后端及窗口 exposed 断言通过，截图已检查，无 offscreen。
- 构建脚本修正：CMake 重写 vcxproj 并不等于必须重编源码；去掉该误判，保留源码/头文件依赖及对象更新计数。已实际验证增量 3/3、2/2、4/4。主会话回传完整日志，独立窗口保持简洁进度。
- 日志：`build/agent-chat-m1-build.log`、`build/agent-chat-m1-incremental.log`、`build/agent-chat-m1-tests.log`。开发可执行文件 `build/Release/lmms.exe`；默认提示词已复制到其旁 data 目录。未替换生产安装或原生插件 DLL。
- 人工验收：MANUAL/PENDING，不作为阶段受阻条件，也不等于 PASS。

本记录随阶段检查点更新；M1 自动验证完成、检查 diff、提交并确认推送后，才进入 M2。

## 2026-11-16 接续状态

- 最新状态：Penpot MCP已恢复，实际读取原文件及08/09/10画板成功，连接阻塞已解除。以下不可连接/暂停记录为历史。
- 08独立会话管理已通过导入往返、批量归档持久化、搜索/选择以及原生搜索框(48,215,716,44)、标题x133/y491断言。修正主题QScrollArea边框造成的1像素整体偏移；不修改预期坐标规避失败。
- 10分离页按实读数据实现98像素顶部区域、54像素内部头栏、220像素会话栏；1100×1000窗口中，新建(14,168,192,36)、输入框(240,779,840,155)、回接按钮(925,49)均通过原生断言。QDockWidget浮动边框也用该组件局部主题规则去除。
- 09收起宽48、恢复输入、原宽度、560像素最窄侧栏底部按钮不重叠、分离/回接保留同一输入草稿通过。窗口移动到真实屏幕可见区域后再截图，避免屏幕外黑色区域当成UI证据。
- 最终日志：build/agent-chat-m1-final-build.log、agent-chat-m1-final-tests.log。真实窗口截图已归档chat-m1-native.png、chat-floating-native.png、chat-collapsed-native.png、chat-sessions-native.png；设置/批准页沿用此前实测截图。中文正常，列表无水平滚动条；颜色/按钮外观沿用LMMS主题。
- 30个自绘SVG位于默认主题原位置，其中会话页upload/export/trash路径从实时Penpot导出；主窗接入不新增MainWindow字段。开发程序build/Release/lmms.exe，未重建/替换原生插件DLL、未修改生产安装。
- 人工多屏/跨DPI及完整DAW操作MANUAL/PENDING，不作为检查点阻塞。当前测试是实际聊天组件窗口，不能冒充M2模型对话、M3工程操作或M5完整集成。无聊天功能交付ZIP。
- 同一Penpot工具缺失已连续三个目标轮次复核；插件检索无可用入口，目标已标记blocked。需恢复此前MCP连接，再恢复目标并实际读取画板。所有源码保留，未绕过设计门槛。
- 最新恢复检查：目标已active，但Penpot MCP工具已从会话移除（可用工具数0），无法实读目标画板。按计划§4记录BLOCKED（Penpot不可连接），停止实施/编译/测试，保留源码。此为恢复后首次依赖失败，目标状态保持active。
- 持续目标接口返回 paused，已停止后续实施，不自动恢复；M1仍未完成。
- 按实时Penpot08新增独立ChatSessionsDialog，接入搜索/筛选/批量管理/导入导出/会话切换。导入生成新ID并只复制会话字段；新增原生会话管理测试尚未运行。
- 本轮Release增量构建失败：Qt6 mapToGlobal重载歧义（C2668）。已读取并归档 `build/agent-chat-sessions-build-failed.log`；显式QPoint修正已保存，未重新编译，不记通过。
- 当前无活动构建/测试，源码未提交，无新ZIP；恢复后先实时探测Penpot，再验证该最小修正及08真实窗口截图，继续10/09及最小尺寸验证。

## 设置与批准页接续验证

- 源码：`ChatSettingsDialog.h/.cpp`；删除ChatDock中旧表单实现，保留原设置入口。
- Penpot实读：05/06画板，原点/尺寸与节点矩形归档 `chat-settings-layout-baseline.json`。
- Release实际增量14/14，退出码0；CTest 2/2（2.63秒）。原生断言提示词框(306,352,610,43)、请求批准卡(280,208,850,222)，实际点击模式卡、保存后从磁盘重读。
- 首次截图发现palette(dark)在现有主题中为浅灰，头/底栏白字对比不足；改用palette(base)，仍继承LMMS主题。CSS修正后仅复验ChatDockTest，1/1通过（2.39秒）。
- 实际窗口截图：`chat-settings-native.png`、`chat-approval-native.png`。中文正常，双列卡片、导航及底部按钮无溢出。截图是Qt实际显示窗口的客户端区域，没有使用offscreen或Widget.render。
- 日志：`build/agent-chat-settings-build.log`、`build/agent-chat-settings-tests.log`。M1仍在进行：独立会话管理与精确浮动排版未完成，不能提交阶段检查点或进入M2。
