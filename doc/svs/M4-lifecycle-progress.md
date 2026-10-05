# M4 实施记录（PASS）

M3 已通过并提交/推送 776ae86fa。M4 已达到计划书既定自动验收门槛；最新结论和证据见文末“M4 最终验收”。下列历史记录中的 IN_PROGRESS、剩余项目和旧测试数量描述当时进度，由文末最终结论取代。人工项仍为 MANUAL/PENDING。

独立 SVSSynthesisScheduler 管理优先级队列和可配置全局预算（默认 2）；非并发实例排队，不在工作线程等待引擎锁。RenderControl 在真实 session 生命周期内安装线程安全取消回调，失效/删除/换输入请求取消。取消后的运行任务仍占名额直到 render 退出；发布继续以 clip ID、generation、revision、request 四重门禁检查。输入快照在后台串行重新查询上下文能力/音高基准，避免其它片段声明污染。示例支持 JSON developmentFaults delayMs / lateReturn / fail，仅用于故障验证，含迟到返回忽略和失败重试。

SVSCache 由后台任务读取/写入；内容键包括插件二进制 SHA-256、声库/字典/参数/曲线/位置/内容偏移/tempo/输出格式和音符输入，排除实体请求版本。音符 ID 在缓存内部标准化，命中后回显绑定当前音符 ID 和 revision。内存 LRU 128 MiB，磁盘上限 512 MiB，文件校验和/格式/尺寸/PCM 有限值检查、原子写入与损坏重建。单个超限结果不进入对应缓存；缓存拥有的内存有上限，播放/片段正在持有的不可变结果另由其生命周期管理。命中结果复制/重绑定均在工作线程，GUI/音频线程不读磁盘。

SVSTimeMapping 明确正向 contentOffset 与 LMMS startTimeOffset 的相反符号。插件声明结果全局秒起点，宿主映射回内容 startTick；播放与波形从真实结果起点读取。示例保留首音负相对 tick 的前导音素。移动/长度/偏移改变立即失效并合并事件循环内重新提交；主题/立绘不参与失效。已验证第 2→6 小节复制、实际 LMMS 混音出现于新位置、裁剪不重复平移、变速重新生成结果和前导音素起点。

编曲切片只在 ClipView 增加一个 SVS 路由分支，独立 SVSClip::splitAt 负责可见内容重定位。左右归零局部时间/内容偏移，连续曲线使用带边界导数的 slice；跨界音符生成两侧可编辑音符并清除需重新预测的音素，参数/歌词/手动读音保留。一条轨道检查点恢复整个原片段，撤销/重做通过。片段/轨道克隆及 StateCopy/clipboard 路径生成新 clip/note ID；原地工程恢复保留实体身份。

自动证据：M4-scheduler-build/test；M4-cache-build/test（首次命名冲突失败日志另存，不作为通过证据）；M4-origin-build/test/QtTest；最终当前批次 M4-split-build/test/QtTest。Release 主程序/示例/集成测试构建通过；25 passed / 0 failed / 0 skipped，CTest 1/1 通过。自动运行采用 offscreen，不打开桌面程序。

持久化补齐（M4-persistence-*）：未知轨道/片段属性和子节点、notes 容器/音符属性与子节点、未识别曲线原始 JSON 均保留；已识别数据与未知数据一起经复制、切片、保存重开恢复。schema 0 按当前字段迁移为 1；更高版本、损坏 JSON、非法音符/位置/长度及已声明但不能解析的曲线拒绝编辑/合成，保存原始节点并显示诊断。缺失插件/声库时保留语言、插件/声库版本与隐藏参数。轨道未知 schema 同时保护其片段，编辑器禁用编辑区域；插件版本进入声库元数据与合成输入。26 passed / 0 failed / 0 skipped。

声明异步补齐（M4-declaration-*）：bindVoice 不再在 GUI 等待引擎互斥锁或读取字典；后台串行查询/解析并加载字典，GUI 以请求 ID 门禁接收当前声明。初次声明未就绪的片段等待；失败显示诊断，过期声明/已取消片段不复活，销毁目标后回调不访问 UI。能力到达后再次检查不能解析的已声明曲线，避免加载工程时能力尚未到达而静默忽略。忙碌示例延迟 1000 ms 的测试验证换声库返回 <100 ms、事件循环持续响应、连续切换最终能力正确、取消状态保留。现有依赖初次声明的自动用例显式等待就绪。27 passed / 0 failed / 0 skipped；CTest 1/1 通过，该证据覆盖上述持久化和现有全部回归。

## 剩余必需工作

tempo 映射与捕获基础补齐（M4-tempo-map-*、M4-tempo-snapshot-*）：公开 SDK svs_time.hpp 提供只含标准 C++ 的不可变分段 tick/秒映射，保留分数 tick 与负前导时间；宿主 TimeMapping、插件音符秒时长、示例 DSP 曲线/音素求值、结果起点、播放读样、波形/音素/音高回显和缓存恢复共用该映射。输入 tempoMap 必须从 tick 0 开始、严格递增且每段时间比例有限正值，损坏映射拒绝合成。映射内存计入缓存预算。专项跨两个 tempo 变化点与 contentOffset 验证实际 PCM、连续音高、手动音素和磁盘缓存恢复，28 passed。

独立 SVSTempoSnapshot 捕获当前工程的 tempo 自动化数值节点、切线、段类型、位置/偏移、静音筛选和 Pattern 循环上下文；后台求值只访问值对象，不保留 GUI/可变工程指针。测试逐 tick 对照 Song::automatedValuesAt，覆盖全局/普通轨覆盖顺序、Hermite、线性、离散、裁剪、尾值及 Pattern 循环；捕获后修改/静音/删除源不改变快照，构建映射支持取消与工程长度上限。Release 主程序/示例/集成测试构建通过；29 passed / 0 failed / 0 skipped。首次编译的 IntModel typed getter 使用错误已修正，失败日志保留为 M4-tempo-snapshot-build-failed-model.log，不作为通过证据。

tempo 快照链路与退出补齐（M4-tempo-lifecycle-*、M4-shutdown-*）：SVSTempoSource 监听工程/Pattern 自动化节点、静音、位置、长度和轨道变化；片段捕获不可变数值快照，调度器后台生成映射与秒时长并纳入缓存输入。播放产生的自动 tempo 通知及 Stop 不改变数值的通知不使当前结果失效。实际 LMMS 混音测试覆盖 120→240/60 BPM、停止保留结果及静音后按当前 fallback 重合成。声明任务改用 SVS 专属线程池；Engine 退出在销毁工程对象前取消并等待合成/声明任务，移除已结束任务的待投递完成回调。专项验证等待正在执行的声明、取消运行/排队任务、禁止关闭后提交及迟到发布。Release 构建通过，完整 QtTest 31 passed / 0 failed / 0 skipped；先前 transport/静音失败日志保留，不作为通过证据。音素编辑时间约束补齐（M4-phoneme-tempo-*）：最短音素秒时长与最大前导秒数通过 TempoSnapshot 局部积分/逆映射转换为边界 tick，包含分数 tick、负时间、跨变速点及片段 contentOffset；拖动使用捕获快照，tempo 变化取消当前拖动。鼠标专项在工程 240 BPM 段、当前显示 120 BPM 时验证 5 ms 最短时长限制为 0.96 tick，属性修改及实际插件结果均通过。GUI 只求解所需短区间，不生成整首 tempoMap。最新 Release 主程序/示例/测试编译及完整 QtTest 32 passed / 0 failed / 0 skipped。
导出快照与正式入口补齐（M4-export-snapshot-*、M4-export-integration-*、M4-export-regression-*）：SVSClip::captureInput 复用既有完整输入构建，独立 ExportSnapshot 捕获声库/参数/字典/曲线/版本/tempo/位置与内容偏移，以目标输出采样率和高优先级准备不可变结果。声明尚未就绪时按已捕获的声库与上下文异步补齐声明和字典，不等待 GUI 锁。ProjectRenderer 在准备前固定现有 Song 导出范围，准备完成后激活独立 SVS 导出音频区，再沿现有总线/混音/编码路径输出；SVS 实时编辑与后续合成不会替换该导出快照。默认失败中止并显示轨道、片段名称/ID、tick 和原因；导出对话框提供默认未勾选的“失败 SVS 区域以静音导出”，明确选择后输出静音。准备取消向导出所属任务传递，迟到结果忽略；未完成文件删除。RenderManager 的 Song 多轨导出筛选追加 SVS，Pattern Store 筛选保持原有范围。导出器独立完成信号覆盖准备失败，Agent 导出保留具体片段错误并使用该完成信号。实际 32 kHz 浮点 WAV 验证准备期间修改/移动原片段仍输出原范围与非零音频、缺失声库默认无成品文件、忽略失败为静音、准备取消清理文件；完整 QtTest 34 passed / 0 failed / 0 skipped。SVSIntegrationTest、A3CommandsTest、McpExportLifecycleTest CTest 3/3 通过；A3 为 18 passed / 0 failed / 3 skipped，跳过项为未配置原生 DLL 路径的参数/MIDI/导入扩展用例，导出取消/退出测试实际通过；MCP 为 3 passed / 0 failed / 0 skipped。首次 QThread private finished signal 编译错误已修正，失败日志另存，不作为通过证据。

- 缓存播放/混合与分轨导出一致性、自动化 tempo 实际导出、循环/跳转和准备期间删除轨道已通过下述自动验证；导出准备期间变更工程自动化/混音路由的冻结保护仍需核对，不能宣称全部导出验收完成。
- 全生命周期销毁等待与引擎卸载检查；缓存损坏、失败、版本门禁及播放跳转/循环的最终验收。GUI 声库切换等待引擎 mutex 的问题已由上述异步声明与专项验证处理。

用户最新约束：计划书实施期间暂不使用 Computer Use；M5 和最终自动回归完成后由用户人工验收并指导钢琴窗差异修复。原安装版不打开，本机测试 fixture 不进入可分发包。普通 PianoRoll/InstrumentTrack/Automation/VST/Carla 不做顺带重构。

实际音频一致性与删除保护补齐（M4-playback-export-*、M4-variable-export-*、M4-export-deletion-*、M4-audio-export-regression-*）：两条 SVS 轨分别使用完整/精简声库和左右声像，缓存播放与正常混合 WAV 导出逐样本误差 <1e-6；两份分轨 WAV 补零求和与混合文件误差 <1e-6，轨道静音状态恢复。测试先让 LMMS 既有声像模型过渡稳定，并使用正常导出的最后一小节排空混音尾部，不以删尾模式截断不同分轨后强求尾部相同。循环至少两次和跳转到第二片段有声验证通过，已发布结果保持同一不可变对象。120→240 BPM 自动化工程缓存播放与 WAV 导出长度相同、误差 <1e-6。ExportSnapshot 在捕获后/准备期间删除轨道时取消所属任务并报告轨道和片段定位，即使选择忽略合成失败，也不能将丢失混音目标误记为成功；实际导出验证文件删除、退出导出状态、完成仅一次与迟到回调无副作用。首次完整回归发现前一导出恢复 Dummy 设备自动启动周期，造成后续手动推进音频的测试重复计时；测试设备改为显式推进周期，生产 AudioEngine/Mixer 无修改，失败日志保留为 M4-audio-export-regression-test-failed-transport.log。Release 集成测试构建及完整 QtTest 37 passed / 0 failed / 0 skipped，最新通过证据 M4-audio-export-regression-build.log、M4-audio-export-regression-test.log、M4-audio-export-regression-QtTest.txt。

相邻问题 follow-up（不实施）：RenderManager 的既有分轨文件名清理正则在 Qt6 报 invalid QRegularExpression；本测试文件名 Left/Right 合法，文件与音频输出正常，此告警与 SVS 接入无直接必要关系，不扩展修改范围。
导出上下文门禁补齐（M4-export-context-*）：捕获 SVS 音量/声像/混音通道/静音及效果链序列化状态；激活混音前核对这些状态与不可变 tempo 自动化快照。歌词/参数/片段编辑继续使用已冻结合成输入，准备期间修改混音上下文或 tempo 则定位失败并提示重启导出，清理文件；“忽略合成失败”不能绕过该门禁。该检查避免旧 PCM 与新工程上下文混用，不声称复制了整个 LMMS 混音/自动化图。实际 ProjectRenderer 验证音量与 tempo 变化两种情况的终止/定位/文件清理和导出状态恢复。Release 主程序/示例/集成测试及 A3/MCP 导出入口重建通过，完整 QtTest 38 passed / 0 failed / 0 skipped，CTest 三项 3/3 通过，证据 M4-export-context-build.log、M4-export-context-regression-test.log、M4-export-context-regression-QtTest.txt、M4-export-context-entry-build.log、M4-export-context-entry-test.log。首次测试编译缺少 DefaultVolume 声明已改为恢复捕获前值，失败日志另存，不作为通过证据。

M4 最终核对仍未完成：导出实际运行期间的工程变更门禁、分轨连续导出的快照边界、插件缺失时有效缓存播放，以及生命周期/卸载检查须按原计划核对，不能以当前 38 项通过提前标记 M4 PASS。
插件/声库缺失缓存补齐（M4-missing-cache-*）：正常发布结果携带完整缓存 key 与可编辑输入摘要，片段 XML 保存 key / 摘要 / 原输入采样率；缺失插件或目录中已无所选声库时，宿主不重新查询引擎能力，而是在同一有界调度器后台读取缓存并重建 tempo 映射，继续通过实体 ID / generation / revision / request 门禁发布。摘要包含绑定 ID/版本、音符/歌词/覆盖/参数/用户曲线/工程字典、位置/偏移/长度、tempo 数值快照和输入格式，排除当前无法查询的能力 Schema 与声库字典；完整缓存 key 仍保留引擎二进制/能力/字典上下文，文件内也保存并核对可编辑摘要，不能只靠工程属性猜测某块 PCM 属于当前输入。旧缓存文件没有该摘要时当作未命中，插件存在时可重建。加载缺失绑定保留插件/声库版本参与核对。状态明确显示 Missing voice/plugin: cached audio；无有效缓存、修改歌词、移动位置等输入变化均静音，用户数据保持可编辑。缓存 PCM 仍留在本机有界缓存目录，不将本机资源或缓存写进 SDK/Release。

真实无插件进程证据：父用例合成并保存含有效引用的 XML，随后以独立测试进程和空插件/数据根加载，同一进程断言目录为空且原插件不可用，再验证保存重开后的缓存播放、复制后回显绑定新音符 ID、32 kHz WAV 导出、歌词编辑/移动失效及不存在缓存静音。父用例实际执行并检查子进程退出码与 QtTest 3 passed / 0 failed；完整父进程 QtTest 40 passed / 0 failed / 0 skipped（包含仅供子进程执行的辅助槽，缺插件验收实际由父用例启动子进程完成）。Release 主程序/示例/集成测试及 A3/MCP 导出入口重建通过；CTest 三项 3/3 通过。证据 M4-missing-cache-build.log、M4-missing-cache-test.log、M4-missing-cache-regression-test.log、M4-missing-cache-regression-QtTest.txt、M4-missing-cache-entry-build.log、M4-missing-cache-entry-test.log。初次独立进程测试暴露缓存导出错误标记为待查询声明而解引用缺失插件，已修正为仅实际插件存在时声明；失败日志 M4-missing-cache-test-failed-null-job.log 保留，不作为通过证据。

当前剩余必需核对：导出运行期间工程变更、分轨连续导出的快照边界、生命周期/引擎卸载及迁移保护克隆身份；仅按计划书既定门槛收尾，不增加计划外测试矩阵。M4 继续 IN_PROGRESS，尚不提交为阶段完成或进入 M5。
受保护片段复制身份补齐（M4-protected-copy-*）：更高/损坏 Schema 的片段仍保存原始 XML 并保持只读；复制分配新片段/音符 ID 时，同步更新原始 XML 中已知的实体 ID，避免保存副本时回写来源 ID。未知属性/曲线/节点内容和 schemaVersion 不改写。现有迁移专项新增 schemaVersion=99 的独立副本检查，核对内存与序列化 ID、新旧音符 ID、未知 vendorState/曲线及只读状态。Release 集成测试重建与迁移专项 QtTest 3 passed / 0 failed / 0 skipped，证据 M4-protected-copy-build.log、M4-protected-copy-test.log、M4-protected-copy-QtTest.txt；完整回归最新证据仍为此前缺插件缓存批次的 40 passed，后续最终回归将覆盖这次修正。

接下来只完成 M4 既定剩余门槛：实际导出运行及分轨连续导出的快照保护边界、生命周期/引擎卸载核对。缓存缺失与迁移克隆问题已处理，不再列为未实施项目。
## M4 最终验收（2026-10-05）

连续分轨导出在 RenderManager 开始时捕获一次 SVS 输入快照，各轨输出复用各自的冻结区域；冻结范围也参与 SVS Track 长度计算，不因等待期间移动片段而延长输出。空音符区域保留范围并按静音处理，不请求合成。轨道队列采用受控弱引用，避免删除轨道后访问已销毁对象。专项先输出基准，再在第二次连续导出开始后修改尚未输出轨的歌词、音高和位置，两份 WAV 的长度与样本均匹配基准（最大差 <1e-6），实时编辑仍保留在工程。

导出准备完成后继续检查 SVS 混音/路由、tempo 和轨道生命周期；GUI 控制变化或删除轨道会明确中止当前导出，不能在冻结 PCM 上混入新的上下文。渲染线程正常自动化写值不当作用户编辑。实际写 WAV 期间分别修改音量、改变 tempo、删除轨道，三项均检查错误信号只一次、导出失败、工程退出导出状态和未完成文件不存在。Windows 文件清理需在编码停止/finalize 后关闭持有的 QFile 再移除；AudioFileDevice 仅增加这一个必要接口，不改编码器或音频处理。专项 5 passed / 0 failed，证据 M4-export-running-build.log、M4-export-running-test.log、M4-export-running-QtTest.txt；连续分轨专项 3 passed，证据 M4-export-batch-*。

生命周期核对：Engine::destroy 在停止音频后、清除工程前调用 SVS scheduler.shutdown；关闭取消队列和运行任务，等待合成与声明线程池真正退出，移除已完成任务的 MetaCall，再释放 job 持有的 Plugin。每个运行 job 持有 shared_ptr<Plugin>，session 的 cancel 回调在其销毁前 detach，结果由插件 release_result 释放，session 随后 destroy_session；Plugin 析构先 destroy_engine，最后 Impl 的 QLibrary 释放。Registry 可能保留实例至进程退出，退出前已经完成上述 join，不在执行任务或回调时卸载库。schedulerShutdownWaitsForWorkers 验证运行/排队取消、声明等待、迟到回调不发布以及关闭后拒绝新提交；完整测试 cleanup 实际执行 Engine::destroy，独立无插件测试进程也正常退出。

最终 Release 主程序、SVSExample、集成测试、A3/MCP 导出入口重建通过。完整 SVS QtTest 44 passed / 0 failed / 0 skipped（含无插件子进程辅助槽；父用例实际启动并检查无插件子进程）。新增测试让共享撤销栈达到既有 100 条上限，初次全量中两项深度断言因此失败；现用 QtTest init 在每项前清空历史隔离用例，不改变生产撤销逻辑。失败日志本机保留，不作为通过证据。最新完整证据 M4-export-final-regression-test.log、M4-export-final-regression-QtTest.txt；构建证据 M4-final-entry-build.log。

本阶段按计划书既定门槛结束，不继续扩大测试矩阵。真实 IME、桌面观感、听感及 TuneLab 对齐人工复核保留 MANUAL/PENDING，依用户安排在 M5 后进行；不使用 Computer Use，不打开原安装版。原有 RenderManager 文件名正则在 Qt6 的警告只记 follow-up，未实施相邻修复。

最终入口：SVS CTest 1/1 连续五次通过（每次完整 QtTest 44 passed），A3/MCP CTest 2/2 通过；A3 18 passed / 0 failed / 3 skipped，跳过项仍为未配置 DLL 的扩展用例，导出取消和退出实际通过；MCP 3 passed / 0 failed。证据 M4-final-callback-build.log、M4-final-callback-test.log、M4-final-callback-QtTest.txt、M4-final-entry-test.log、M4-final-A3-QtTest.txt、M4-final-MCP-QtTest.txt。初次 CTest 曾在运行中删除轨道后的变速导出附近单次超时；同命令三次复验及相关两项组合通过，测试 progressChanged 回调的接收对象随后由测试类改为导出器自身，防止测试对象跨用例接收待投递回调，再连续五次完整通过。没有抓到超时栈，因此不宣称单次超时根因已确认；M5 最终回归继续使用同一入口，若复现再按该触发链定位。该记录不以延长超时或忽略失败替代通过证据。

M4 PASS，按阶段要求提交并推送后进入 M5；不增加本阶段完成条件。
