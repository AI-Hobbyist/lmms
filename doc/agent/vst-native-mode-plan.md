# VST 原生加载模式实施计划

状态为实施中。首版面向 Windows 上现有的 VST2、VST3 沙盒宿主，沿用当前发布工具链和辅助程序位数支持。

## 交付范围

在全局设置新增「VST 加载模式」，选项为「兼容模式」和「原生模式」。现有配置延续兼容模式，切换作用于之后的添加操作。原生模式下，用户从插件目录选择具体 VST 类，直接在目标编辑器创建该 VST 实例。用户无需先放置 VeSTige 或 VST Effect，再从外壳选文件。界面显示实际插件名，编辑器窗口由对应沙盒进程持有。

| 入口 | 原生模式的功能边界 |
|---|---|
| 多轨和节拍编辑器 | VSTi 在现有「乐器插件」列表和添加乐器入口中按实际名称列出，可搜索、点击添加或拖入。传递 MIDI 事件、通道和块内时间偏移。有主音频输入的插件使用主立体声输入映射 |
| 调音台 | 按效果器分类插入，使用主立体声输入和输出。已有明确的 MIDI 路由按插件能力传递事件；效果链顺序和旁通由现有混音器管理 |
| 节点编辑器 | 展示插件声明的全部音频和 MIDI/事件输入输出，额外总线、侧链和多声道能实际连线和传递数据。不能激活的端口显示原因 |
| 自动化编辑器 | 按稳定参数 ID 暴露全部可调参数。支持连续、步进、枚举和布尔参数，保存或重开工程后仍绑定同一参数。只读参数可展示状态 |
| 我的预设 | 枚举插件公开的预设列表。可保存 LMMS `.xpf`，拖入时自动加入对应插件并恢复设置；也可将当前状态导出为 VST2 `.fxb` 或 VST3 `.vstpreset`。计划将需求中的 `.fbx` 理解为 VST2 bank 格式 `.fxb`，保留现有 `.fxp` 兼容 |
| 插件列表 | 同一插件的多种通道布局合为一个列表项。添加时可从该项选择具体布局版本 |

## 现状与最小实现

- `plugins/VstBase/VstPlugin.cpp` 已选用 VST2 `RemoteVstPlugin32/64` 和 VST3 `RemoteVstHost32/64`。继续复用 `PluginCatalog`、`HostSession`、`LegacyHostBridge` 和 `Vst3HostProxy`，保留现有进程隔离、崩溃退出状态和探针。VST2 原生模式使用现有独立窗口方式，VST3 保持当前 HWND 路径和 OLE 初始化。
- `Vestige.cpp` 和 `VstEffect.cpp` 已连接普通乐器与效果处理，`Vestige.cpp` 和 `VstEffectControls.cpp` 已提供 `parameterModels()`。以现有模型作为行为基线，把仅为原生和兼容入口共用的状态与参数逻辑提到 vstbase，保留原工程描述符和 XML 读取。原生实例通过 LMMS 内部的 `Instrument` 或 `Effect` 接口接入轨道和混音器。
- 构建优先沿用现有插件工厂的动态描述符入口，各加一个轻量注册单元来提供原生乐器及效果器工厂。用户直接选具体 VST；注册单元只负责把类身份交给 vstbase，不创建 VeSTige/VST Effect 实例。保留 DAW 核心和 vstbase 的现有依赖方向，避免让 LMMS 主程序反链加载插件库。
- `VstPlugin::process` 和 `Vst3Ports.h` 当前对外提供立体声入口。VST3 的底层元数据和缓冲区已保存音频及事件总线。节点路径要把实际的多通道块和带总线索引事件接到宿主代理，不能只扩展节点外观。现有节点源图只有双声道输出和单 MIDI 边界，需给原生 VST 增加按端口 ID 寻址的分支。
- VST2 公开音频输入和输出引脚，以及插件声明支持的 MIDI 事件方向。当前 `audioMasterProcessEvents` 直接返回 0。支持 MIDI 输出需要在现有块处理路径收集和转发插件发出的事件。VST2 MIDI 端口按 VST2 API 可表达的粒度提供，不能人为推导 VST3 式独立事件总线。VST3 使用已有总线、通道和事件元数据。
- VST2 已有程序名查询和 `.fxp/.fxb` 写出。VST3 已有状态和 `.vstpreset` 编码，宿主仍需补上程序列表查询和受支持的选择操作。使用控制线程查询 `IUnitInfo` 的列表和名称。插件未公开内置库时，显示其提供的文件预设和当前状态导出能力。支持程序列表读取的插件也未必开放程序切换，列表项应按实际能力启用操作。[Steinberg 程序列表](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IUnitInfo.html) · [Steinberg 预设格式](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Preset%2BFormat.html)

## 布局选择与 LMMS 预设拖入

目录保留每个实际可加载的类，在展示层按插件家族聚合。聚合须有可核验的厂商和产品归属，以及插件声明的布局或已确认的版本映射。Shell 子类可参与聚合；同名、同厂商或同属一个 Shell 单独出现时均不足以判定为布局变体。乐器和效果器在各自入口展示。只有一个可用布局的项直接加载，多布局项在点击添加或拖入时提供选择，例如 Mono、Stereo、Mono → Stereo 和多输出。普通入口通过主立体声投影使用所选布局，节点入口显示该布局声明的全部端口。

布局选择同时覆盖两种实现。布局对应不同二进制或插件类时，实例化确切的模块与类；布局由同一个类协商时，在辅助进程中应用并校验所选总线配置。项目和 `.xpf` 都持久化具体类身份、布局标识及必要的总线配置。加载时以保存的布局为准，缺失时明确报错，由用户决定是否换布局。

乐器复用现有 `InstrumentTrackWindow::saveSettingsBtnClicked()` 和 `.xpf` XML 路径，保存轨道乐器设置、插件身份、布局和完整插件状态。效果器用同一 LMMS 预设容器增加可识别的效果器内容类型，保存插件身份、布局和效果器设置。两者均保存在「我的预设」。拖到空白多轨或节拍区域时一次完成乐器轨添加与状态恢复，拖到混音器效果链接受效果器预设并插入实例，节点编辑器在匹配的位置添加相应插件节点。拖入已有乐器轨按现有替换语义执行。插件、类或布局不可用，或状态恢复失败时，保留目标原状，显示具体错误。

## 现有文件拟修改

| 范围 | 文件和改动 |
|---|---|
| 全局设置 | `src/core/ConfigManager.cpp`、`include/ConfigManager.h`、`src/gui/modals/SetupDialog.cpp`。保存加载模式并接通设置控件。新增模式只影响新添加的实例，项目按自身格式进行恢复 |
| 添加与恢复 | `src/gui/PluginBrowser.cpp` 在现有乐器列表展示 VSTi，`src/gui/modals/EffectSelectDialog.cpp` 暴露效果器类；两处复用布局聚合和选择。`src/tracks/InstrumentTrack.cpp` 和 `src/core/EffectChain.cpp` 在现有实例化与状态恢复入口识别原生 VST。多轨与节拍共用轨道入口 |
| 宿主接口 | `plugins/VstBase/VstPlugin.h/.cpp` 增加端口级音频与事件入口、可调参数描述、预设列表和按路径导出入口。现有 stereo 重载保持原行为。`plugins/Vestige/Vestige.cpp`、`plugins/VstEffect/VstEffectControls.cpp` 仅在确有共用收益时改为调用共用参数或预设入口 |
| VST2 沙盒 | `plugins/VstBase/RemoteVstPlugin.cpp`、`plugins/VstBase/RemoteVstSession.h`、`include/vsthost/Vst2Messages.h`、`include/vsthost/LegacyHostBridge.h`。补引脚信息、可写和可自动化能力、已有程序命令的可靠结果，以及块内 MIDI 输出 |
| VST3 沙盒 | `tools/vsthost/Vst3Instance.h/.cpp`、`tools/vsthost/RemoteVstHost.cpp`、`include/vsthost/Vst3HostProxy.h`、`include/vsthost/Vst3Commands.h`。补有界的程序列表及选择协议，复用既有元数据和预设字节；实际多总线路由优先复用底层代理接口 |
| 节点图 | `src/gui/MainWindow.cpp` 的添加与布线、`src/gui/NodeWorkflowCanvas.cpp` 的端口展示、`src/core/Song.cpp` 的图状态与连接验证、`include/NodeWorkflowInputs.h`、`include/NodeWorkflowSourceGraph.h` 的节点路由。把实际处理接到现有轨道和混音调度。只有当前立体声边界确实无法承载多端口数据时，才局部调整 `src/core/AudioBusHandle.cpp` |
| 预设浏览 | `src/gui/MainWindow.cpp` 中「我的预设」补入 VST 文件类型和原生 VST 节点拖入。`src/gui/instrument/InstrumentTrackWindow.cpp` 复用现有 `.xpf` 保存和 `presetfile` 恢复，混音器沿现有效果链入口接收效果器类型。`ConfigManager::userPresetsDir()` 提供保存目录 |
| 构建注册 | `plugins/VstBase/CMakeLists.txt`、`plugins/VstBase/vstbase/CMakeLists.txt` 和测试 CMake 仅注册新增实现、描述符模块及窄测试目标 |

## 拟新增的代码

| 文件 | 职责 |
|---|---|
| `plugins/VstBase/NativeVstInstrument.h/.cpp` | 直接表示一个乐器实例，接入现有 `InstrumentTrack` MIDI、立体声播放与项目状态接口 |
| `plugins/VstBase/NativeVstEffect.h/.cpp` | 直接表示一个效果器实例，接入现有 `EffectChain` 和 `EffectControls` |
| `plugins/VstBase/NativeVstModels.h/.cpp` | 实例级自动化参数模型和直接打开编辑器、列出或导出预设所需的共用操作。任何已有实现可直接复用时从该文件清单删减 |
| `plugins/VstBase/native_vst_instrument.cpp`、`plugins/VstBase/native_vst_effect.cpp` | 各提供一个动态注册入口，沿用现有工厂按角色构造原生实例 |
| `include/vsthost/VstNodePorts.h` | 定义节点专用的稳定端口标识、匹配和主立体声投影，复用已有 VST3 端口描述 |
| `src/vsthost/PluginLayoutVariants.cpp` 及配套内部声明 | 将目录项投影成列表分组和具体布局选项，供乐器、效果器和节点入口共用 |
| `tests/vsthost/NativeVstModeTest.cpp` | 快速验证选择、端口映射、自动化键和预设写出。必要的原生操作补到现有 VST fixture，不扩充通用测试集合 |

原生实例在项目中保存格式、模块定位、VST2 Shell 子类 ID 或 VST3 ClassID、架构及实例状态。节点端口键按媒体类型、方向、总线或引脚、通道构造，参数按 VST3 ParamID 或 VST2 参数索引保存。端口的显示名和排序可以更新，连接键按实际未变的端口保留。总线变化时使用现有元数据发布与音频边界同步；缺失端口的旧连线显式标记失效。实时线程只处理已经准备好的缓冲区和事件，预设查询与文件写出在控制路径完成。

自动化区分可调值与可自动化能力。VST3 读取只读和可自动化标志，VST2 查询 `effCanBeAutomated`；可手调但插件禁止自动化的项在自动化面板显示限制。实际链路验收覆盖参数修改反馈、宿主自动化写入和避免回写循环。预设导出写真实格式并区分 bank 与单预设，使用同一插件重新导入检查状态。写出失败不留下半文件。

## 快速测试

| 检查 | 最小可判定用例 |
|---|---|
| 模式与项目兼容 | 输入两种设置和两种插件身份，验证分派到正确入口；旧工程 fixture 恢复原实例类型；用内存 XML 核对原生实例、自动化键和端口连线 |
| 乐器列表与布局 | 检查 VSTi 可搜索且能从乐器列表拖入。同插件 Mono/Stereo 条目合并，选择后辅助进程收到对应类或总线配置。同名但无共同产品归属的插件仍各自列出 |
| 音频与 MIDI | 为主总线、侧链和额外输出送不同的脉冲，逐端口对比采样；普通入口只接主立体声。事件 fixture 验证总线、方向、通道和块内偏移，覆盖 VST2 MIDI 输出 |
| 参数与预设 | 用连续、步进和只读参数核对列表和实际写入。VST2 校验 `.fxb` 文件头、插件 ID、bank 数据并重新导入。VST3 复用 `Vst3PresetSdk` 验证 ClassID 和状态块，新窄用例覆盖节目列表、无列表与实际导出目录 |
| LMMS 预设拖入 | 保存含非默认布局及参数的 `.xpf`，从「我的预设」拖到空白目标，验证自动添加正确插件并恢复类身份、布局和参数；失配、损坏或目标不兼容时核对目标未变 |
| 实际入口 | 最小 fixture 分别经过多轨、节拍、混音器、节点入口。通过业务输出断言 MIDI、双声道或节点多端口到达目标，避免以实例存在或节点出现作为通过依据 |

优先使用内存数据和确定性 fixture，目标是纯逻辑检查秒级完成。实施时用 `ctest -N` 确认只选定实际业务测试或直接选 QtTest 槽。生命周期测试及含生命周期检查的整套用例记录为 `SKIPPED（用户禁止）`。真正的 GUI 用例使用 Windows Qt 平台和现有开发版。编译、测试和验收均在前台 PowerShell 打日志，先检查同工程任务状态；按阶段记录验证结果，执行提交、推送和远端核对。

## 增量重编估算

这是对当前 Windows Release 配置的规划估算，单位为 C++ 翻译单元。相同源文件在 x86 和 x64 各编一次时按两次计，链接、资源和自动生成单元另外计算。

| 实施范围 | 预计重编 |
|---|---:|
| 设置、目录入口、布局聚合、模型和预设 UI | 12–20 个 C++ 编译项 |
| 节点端口路由与业务断言 | 6–10 个 C++ 编译项 |
| VST2/VST3 双位数辅助进程及预设命令 | 8–14 个 C++ 编译项 |
| 快速测试与 fixture | 3–6 个 C++ 编译项 |

按阶段粗估 **29–50 次 C++ 编译**，其中不同阶段可能重编同一源码，另有约 4–9 个 MOC/资源编译及必要的重新链接。`ConfigManager.h`、节点共享头或 IPC 头影响消费者较多，实际首次增量可能超过估算。开工先用现有目标依赖与编译日志核对波及范围，优先把实现保留在局部源文件。复用 `build/Release/lmms.exe` 和 `build/Release/plugins` 并核对辅助程序版本。

## 最终十轮自动验收

1. 从现有插件目录生成带格式、角色、位数、类 ID 和编辑器能力的样本池。对 Shell 先枚举音频子类并保存列表，随后从子类抽取。记录随机种子、实际抽样清单和已知复现样本。预先验证菜单、参数或控件可操作性，明确记录排除原因。
2. 在一次原生 Windows LMMS 会话中完成十轮。每轮选一个不同的测试实例，轮换四类入口并均衡覆盖 VST2/VST3、乐器/效果器。安装库里有效的 32 位和 64 位样本都纳入。十轮同时保留确定性的已知故障样本 Serum 2，其他样本按约束随机抽取。
3. 每轮在原生模式的实际入口添加样本，打开编辑器、等待稳定、验证一个可确认的位置或可访问控件并实际调整参数。用探针确认编辑器回调和模型值，再通过宿主写入一个已确认参数，检查编辑器保持可见、对应辅助进程持续运行。没有可操作原生控件的样本单独记录未覆盖项，宿主写入证据独立记账。
4. 每轮记录样本、入口、格式和位数、窗口 ID、辅助进程 PID、参数 ID、调整前后值、有效声音或 MIDI 断言，以及失败时的退出码和最后的探针阶段。真实窗口截图按需要留存。用专用 fixture 另行核对多端口和全部参数的功能断言，随机样本用于验证真实插件覆盖。
5. 同一开发会话完成操作后关闭一次。轮次不以关闭、重启或释放资源为验证目标。失败结果保留原轮次，重抽记录为新补充证据。真实桌面输入不可用时按项目规则记录 `MANUAL/PENDING`，不以模拟渲染代替。

十轮内至少有一轮从「乐器插件」列表直接拖入 VSTi，一轮在聚合项中选择非默认布局，一轮从「我的预设」拖入 `.xpf` 自动添加对应插件。可由同一轮覆盖多个入口断言，结果要保留实际保存的类、布局和参数值。

## 实施顺序

1. 先完成全局切换、原生实例、普通入口与兼容恢复，用已知 VST2/VST3 乐器和效果器核对真实功能。
2. 在原生入口基础上实现节点全端口处理与自动化，先验真实多总线输出和 MIDI 反馈。
3. 完成预设列表和可重新导入的文件导出，再进行十轮实机验收。各阶段按仓库要求完成对应检查点。

## 实施记录

- 阶段一的 Windows Release 编译与部署已通过。两个原生模块在 `build/Release/plugins`，与 `build/Release/lmms.exe` 一起验证。当前配置涉及共用头文件，构建日志中 `lmmsobjs` 阶段记录了 94 个 C++ 编译项（含 MOC 和资源生成源码）。
- 普通业务路径用开发版的 `rendertracks` 验证。VST3 乐器接收 MIDI 并生成立体声，VST2 DSK ThoR 输出双声道，VST2 和 VST3 效果器分别产生 0.50 和 0.25 的双声道增益。测试工程以兼容模式设置加载，已保存的原生插件身份仍能恢复。
- 渲染过程报告了既有的文件名正则表达式警告，四个音频文件仍通过验证。该警告留待独立处理。
- 阶段二的 Windows Release 构建和定向功能验证已通过。VST3 额外输出、普通采样轨侧链和跨总线 MIDI 使用节点工程实测，VST2 沙盒 MIDI 回传已用独立测试插件验证。VST2/VST3 工程自动化的目标增益为 0.25 和 0.75，两种格式实测约为 0.249952 和 0.750048。
- 真实 Windows 定向编辑器用例退出码为 0。用例验证连续、步进、只读及仅编辑器可调的参数分类，增益 0.625 回传，以及立体声切为单声道后失效端口的连线仍保存在工程、节点图收到刷新通知。插件间和混音阶段的延迟路由各发送到下一音频周期，保留 MIDI 块内偏移。
- 自定义子图、跨边界连接及音频扇出已有实现已独立构建验证，并以 `ac749dbf6` 推送到 `dev`。阶段二的 VST 改动在此前置提交之上独立提交。
- 人工验收为 `SKIPPED（用户决定）`。生命周期测试为 `SKIPPED（用户禁止）`。最终十轮自动 GUI 验收仍在阶段三。
