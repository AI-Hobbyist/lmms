# M3 工程生命周期核对

2026-11-16，范围限实施计划书§8/M3。实时Penpot01可连接；本轮没有改变聊天布局。

## 已确认调用路径

- `Song::clearProject`：原先没有工程替换前通知；只有普通dataChanged，在删除轨道等对象之后发出。现在增加projectAboutToReset，在m_loadingProject guard生效后、任何对象删除/音频changes guard之前发出。
- `Song::createNewProject`：无模板分支调用clearProject；模板分支→createNewProjectFromTemplate→loadProject→clearProject。
- `Song::loadProject`：有效文件路径在清空旧对象前进入clearProject，结束时projectLoaded。projectFileNameChanged只覆盖路径变化，同路径重载不足以识别新工程。
- `Song::restoreProjectState`：设置loading guard→clearProject→恢复对象；没有projectLoaded。ProjectJournal的项目快照undo/redo也调用此路径。一般对象级撤销仍可能只替换局部对象，因此后续批准执行必须重新解析目标并检查对象身份，不能只依赖整工程通知。
- `CommandBus::execute`：已在校验和事务前拒绝isLoadingProject，返回project_busy；保留。成功提交事务沿用现有原对象，不为刷新聊天重新载入工程。

## 最小修改与后续接线

只在Song信号列表末尾追加projectAboutToReset及在共同入口emit，不增加Song数据字段，不更改已有信号次序/事务/恢复行为。工程代次由后续聊天批准调度器订阅该信号后维护；本轮尚未实现代次绑定或批准执行，不能将新增通知冒充完整隔离完成。

下一步M3调度器使用现有ToolRegistry/CommandBus/CommandSchema/CommandTargets：schema校验不执行工具；调用ID、完整参数、会话/轮次、工程代次与目标身份绑定；只读默认也请求批准；批准恰好一次，拒绝零执行。等待网络/批准不持有项目事务或audio guard，脚本展示完整内容/变量/seed。工具结果经已有M2各协议续接。不得偷偷调用getContext上传工程。

## 验证状态

新增ChatProjectLifetimeTest覆盖通知发生时旧轨道仍存在、loading guard与project_busy、清空后旧QPointer失效、恢复/实际文件加载/模板新建、普通改名保留对象与快照撤销/重做通知。测试创建真实Windows Qt窗口；不使用offscreen。配置通过，Release100/100源文件编译通过（10分23秒），ChatProjectLifetimeTest CTest1/1通过1.18秒，QtTest5 passed/0 failed/skip。日志build/agent-chat-m3-lifecycle-{build,tests}.log。直接createNewProject无模板分支目前为静态路径证据，完整DAW用户操作在M5记录。

## 后续接线已核对的接口

ToolRegistry::tools只读取现有描述符，不执行getContext；输入schema来自CommandDescriptor.argsSchema。现有validateArguments（src/agent/CommandSchema.h）可用于批准前纯schema校验，不能用CommandBus::execute或dryRun代替该校验。agent.runScript的script允许内置名或JSON对象；批准内容需展开ScriptRunner::loadBuiltIn，并包含vars与实际seed，最终仍调用原总线而非另建脚本解释器。

出站工具声明需分别适配协议，不能直接把MCP tools/list对象作为模型tools：已核对[OpenAI Chat](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)、[Responses](https://developers.openai.com/api/reference/resources/responses/methods/create)、[Claude工具定义](https://platform.claude.com/docs/en/agents-and-tools/tool-use/overview)、[Gemini函数调用](https://ai.google.dev/gemini-api/docs/function-calling)。OpenAI函数名限制不接受当前注册表命令的点号；后续须保留内部命令原名，使用有明确反向表的合法线名，校验模型仅请求本轮实际公开的工具。工具目录与声明适配现已实现，具体状态见下节；批准及工具执行尚未接线。

## 工具目录与声明基础（2026-11-16）

ChatTools.h/.cpp新增ChatToolCatalog：从ToolRegistry::tools读取元数据快照，保留原schema及注解；点号映射_d，原下划线映射_u，前缀lmms_，反查仅使用本轮目录。resolve校验非空调用ID、对象参数和现有validateArguments，输出结构化ChatToolCall（原命令/线名/参数），不信任模型自带command字段，不执行总线或dryRun。目录快照不接受加载后才注册的工具，也不会暗中调用getContext。

buildChatRequest/ChatProviderClient显式接收工具声明（默认空，普通文本路径保持无工具），八协议对应原生结构。自定义JSON必须配置声明/调用/历史三个入口及模板占位符。当前ChatController仍未传入目录；正式聊天没有新工具执行路径，不把本步骤记为完整Agent工具回合。

原目录configure通过；Release8/8成功，输出build/Release/lmms.exe，QM4137 finished/0 unfinished。ChatRequestTest+ChatProviderClientTest CTest2/2通过5.59秒，QtTest分别12/18 passed，0 failed/skip。新增注册目录快照/不执行探针/非法参数/伪造command字段、八协议声明及缺映射/重复名/非法schema拒绝、本机HTTP完整工具目录与仅显式消息正文。日志build/agent-chat-m3-tool-schema-{build,tests}.log。没有新GUI布局或真实厂商验收结论。下一步批准与执行调度，M3仍IN PROGRESS。

## 批准调度基础（2026-11-16）

新增ChatToolDispatcher，单个具体调用逐项处理。beginTurn绑定会话/轮次并加载目录快照，默认submit请求批准，包括只读工具；无需批准也通过GUI事件队列执行，可以在队列执行前取消。待批准数据包含令牌、调用ID、命令/线名、完整实际参数、工程代次、目的和目标作用域。decide核对令牌及原会话/轮次/ID/参数，拒绝和篡改返回结构化失败；批准在进入总线前消费，重复决定和同轮重复调用ID不会重放。

工程代次订阅已添加的projectAboutToReset及Song销毁；取消或重置清空待执行授权，旧Qt队列令牌也不能执行新轮次的调用。等待期间没有项目批次或audio guard；执行复用CommandBus，原有project_busy及事务仍有效。agent.runScript在批准前展开内置资源、vars和实际seed，获准后仍走原脚本总线，不另建解释器。同步业务函数一旦开始，不承诺能中断。

CoreCommands新增纯身份元数据访问器，复用addressableModels及现有轨道/片段路径；只读取QObject身份和名称，不序列化参数/工程，不调用查询工具。执行前重新解析当前选择器，比较QPointer与名称；轨道、片段、模型、混音通道和控制器有对应身份入口。带明确选择器只绑定相关子树，动态脚本及全局命令绑定整体对象作用域，因此无关轨道改名不导致明确目标的改名调用失效。

自动验证：Release7/7成功，QM4148 finished/0 unfinished；ChatToolDispatcherTest CTest1/1通过1.10秒，QtTest9 passed/0 failed/skip。测试启动并关闭真实Windows Qt窗口，使用实际Engine/CommandBus：只读默认等待/批准一次、拒绝及上下文/参数篡改零执行、无需批准队列取消并切新轮次、同路径工程清空失效、目标轨道重排拒绝后重新请求、无关轨道改名不误拒绝、内置脚本展开及实际获准脚本成功保留对象/零重载/批次结束。日志build/agent-chat-m3-dispatcher-{build,tests}.log。没有通过HTTP MCP绕行，也没有新增界面布局。

音符目标补充：已用实际总线复现 MIDI 排序后旧批准错改另一音符（RED，build/agent-chat-m3-note-target-red.log）。已写入所选 MIDI 音符地址与字段快照、SVS 稳定 ID/范围快照，复用原模块的参数读取与 noteDetail/noteJson；只用于批准前后比较，执行仍走原有 CommandBus。新增 MIDI 排序及 SVS 范围/稳定 ID 回归，包含单数 id 的 updateNote 不受无关音符变化影响。任务状态查询恢复且无冲突后，Release5/5通过，CTest1/1（1.07秒）、QtTest11 passed/0 failed/skip。日志 build/agent-chat-m3-note-target-{build,tests}.log。

路由目标补充：实际 model.setValue 的 /mixerChannel 参数值选择另一个混音通道。RED 用例证实待批准期间 A/B 换位后旧值1误路由到B（build/agent-chat-m3-routing-target-red.log）。调度器仅增加目标通道身份作用域，不改变原有路由命令；重排使旧批准返回 target_changed，新批准值2成功。Release1/1通过，CTest1/1（1.13秒）、QtTest12 passed/0 failed/skip；日志 build/agent-chat-m3-routing-target-{build,tests}.log。真实 Windows Qt 测试窗口开启并关闭，无 offscreen。SVS 用例有测试目录缺少 SVSCompute.dll 的运行时告警，验证的是实际音符编辑和批准，不覆盖合成能力；没有新增插件部署。

## 控制器工具回合基础（2026-11-16）

ChatController通过显式setToolsEnabled开启dispatcher目录和逐项调用；默认false，侧栏尚未启用。冻结本轮连接/Key/system prompt，保留显式消息及附件历史；完整响应后顺序处理调用，保存实际CommandBus结果并继续模型请求。拒绝返回permission_denied；无需批准也由dispatcher排队执行。等待与网络期间不持有工程事务。停止或外部重置为剩余调用补取消结果，已完成assistant工具消息保持complete，未来历史仍配对。决定和下一个调用前检查原会话/消息，删除不执行、不复活。

Gemini无providerId分配本地唯一ID，工具结果持久化provider_call_id（包括空值）以保留原生缺省语义。dispatcher提供executing只读状态，命令自身工程重置先等待实际结果再取消剩余调用；这些跨协议及事件重入分支尚待集成回归。运行时批准令牌不持久化、导入历史不会恢复授权。

新增ChatToolTurnTest采用真实Windows窗口、本机HTTP服务、实际Engine/CommandBus，验证逐项批准/拒绝、结果出站、无隐式工程上下文、停止后配对与继续发送、无需批准、工程替换、会话删除。续答失败重试原逻辑遇tool停止回溯，RED确认后只修改回溯允许assistant/tool；重试保留已有结果，实测不覆盖工具执行后的手工改名。最终Release2/2通过（23秒）；ChatProviderClientTest+ChatToolTurnTest CTest2/2（7.08秒），QtTest18+8 passed/0 failed/skip。日志build/agent-chat-m3-continuation-retry-{red,build,tests}.log；首次新增夹具漏假Key的校验失败另存tool-turn-fixture-red。

八协议双轮补充：ChatToolTurnTest每个协议实际接收两轮调用并逐项批准真实改名，结果回传后完成最终答复。Gemini无服务端ID的两轮本地ID不同，出站仍无伪造ID；两轮原生签名/加密思考状态保留。Release1/1通过，CTest1/1（5.26秒），QtTest16 passed/0 failed/skip。日志build/agent-chat-m3-protocol-turn-{build,tests}.log；只证明本机HTTP实际控制器和总线路径，不代替厂商API验收。

批准卡组件：实时读取Penpot01待批准卡420×157与三个按钮坐标，ChatTranscript内新增原生QFrame/ToolButton、复用主题图标。完整批准快照通过信号交给调用方，当前尚未连接侧栏；详情按钮目前仅发信号。中文词条生成QM4153。真实Windows窗口和截图build/agent-chat-approval-native.png已查看，中文正常、按钮与参考坐标一致。Release6/6通过；夹具补产品对象名后ChatDockTest CTest1/1（8.66秒）、QtTest12 passed/0 failed/skip。首次418宽失败来自夹具没有agentMessages主题对象名，只改测试；没有为测试添加生产渲染路径。日志build/agent-chat-m3-approval-card-{build,fixture-red,tests}.log。

侧栏接线：ChatDock开启工具目录并连接原会话批准卡、状态、决定与完整详情；查看变更使用原生QMessageBox只读详情，完整展示实际参数及script/vars/seed，去掉令牌。两个会话删除入口删除前stop；侧栏使用ID快照，避免同步刷新列表造成item失效。每个工具结果保存后立即通知视图并核对存活，保持草稿与消息一致。真实窗口测试覆盖发送、拒绝、展开完整脚本详情、批准后实际编辑和结果续答。Release8/8（74秒）、QM4155；ChatDockTest+ChatToolTurnTest CTest2/2（15.34秒）、QtTest12+17 passed/0 failed/skip。日志build/agent-chat-m3-dock-tools-{build,tests}.log。既有纯文本GUI测试相应使用真实Engine，不添加生产测试分支。

剩余：工具结果状态卡与原型对应、命令自身工程重置/重入和会话删除实际入口专项、既有MCP回归及M3最终检查。当前是本机HTTP/实际总线和侧栏验证，不等于实际厂商或完整DAW验收。M3继续IN PROGRESS，M4/M5未开始。
