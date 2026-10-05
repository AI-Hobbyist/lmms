# SVS v0.1 契约与接入核查

2026-10-05。依据根目录《SVS轨道、SVS插件、SVS钢琴窗支持计划书.md》；本文件冻结 M0 决策，不扩大功能范围。

## 数据与时间

XML `svstrack` / `svsclip` 节点携带 `schemaVersion=1`。轨道保存 pluginId/voiceId、版本、语言、nameMode、混音控制与立绘设置；片段保存 UUID、起点/长度/内容偏移、音符、字典、参数、曲线和界面状态。未知属性/子节点保留原始 XML；未知参数值和隐藏曲线保留。派生音频不写入撤销快照。复制生成新 clip/note ID，重新创建 generation，原地撤销保留实体身份但增加 revision。

工程 tick = clipStart + localTick - contentOffset。SVS 的 contentOffset 为正向内容裁剪量；接入 LMMS `Clip::startTimeOffset` 时显式转换符号，不能沿用普通 MIDI 的加法解释。曲线锚点使用 double tick，音符边界、小节边界不切断曲线。统一 tempo 映射计算快照中的秒制时间，缓存包含片段位置及 tempo 上下文。分块求值保留边界插值上下文。未知段/擦除区使用显式断段，无 NaN 序列化。

连续曲线提供 linear / cubic Hermite；自动平滑采用受约束切线，离散参数采用 step。绝对音高以 MIDI 连续半音表示；相对模式必须声明 referencePitch，适配计算 absolute-reference。切片保留边界值、导数；跨界音符生成两个可编辑音符并重新解析。手工音素局部时间可为负，随所属音符移动，合法边界由插件校验；优先级：手工音素 > 手工读音 > 工程词条 > 声库字典 > 默认解析。延音由能力声明，宿主不硬编码 `-`。

## ABI / 所有权 / 生命周期

公开 `sdk/svs/include/svs.h` 使用 C ABI，major=1、minor=0。每个可扩展结构以 uint32 size 开头，函数表包含版本及特性位；major 不匹配拒绝，minor 通过 size 和可选函数指针协商。字符串 UTF-8、固定宽度整数、不透明 engine/session/task 句柄；不传 Qt/STL 对象或异常。C++ 包装只在调用方编译，不形成跨 DLL ABI。

Engine 查询声库/Schema；Resource 查询及读释放；Pronunciation 查询字典/候选及校验；Session 提交快照、查询范围、开始/取消；Result 提供音频/音高/音素/参数回显/诊断；Host 提供日志、完成投递和缓冲服务。每个返回指针标明有效期和释放函数。同步输入由调用方拥有，只在调用期间有效；提交时插件复制输入。目录/Schema 返回数据由插件拥有，宿主复制后调用对应释放函数；结果持有到 release_result。所有释放回到原分配模块。

数据线程串行声明调用；后台任务仅接触不可变快照；完成回调进入宿主队列，UI 发布使用 clipId + generation + revision + requestId 四重门禁。取消只请求停止，任务退出前占用并发名额。销毁取消、等待任务/回调退出、销毁 session/engine 后才卸载库。音频线程只读取当前有效不可变 PCM，不调用通用插件 API、不访问磁盘、不等待推理。非并发 engine 实例串行执行；全局预算初值 2，可配置。

状态 Dirty → Queued → Rendering → Ready / Failed，失效立刻静音；旧请求不能恢复 Ready。音频、音高、音素、参数回显同版本原子发布。缓存包含完整输入及输出格式，排除主题/立绘/缩放；内存 PCM 上限 128 MiB，磁盘上限 512 MiB，损坏可重建。图像解码上限 16 MP / 64 MiB，显示缓存上限 32 MiB。

## 最小接入变更清单

| 入口 | 已核查事实 / 允许变更 |
| --- | --- |
| include/Track.h, src/core/Track.cpp | 原类型 0..6；追加 SVS=7，Count=8；factory 只在 Song 容器创建 SVS；复制通过 XML，必须更新新实体 ID |
| include/Plugin.h | Instrument=0..Other=6，Undefined=255；追加 SVS=7；内部 Descriptor 适配公开 ABI，不让插件继承内部 Plugin |
| src/gui/PluginBrowser.cpp | 原浏览器只枚举 Instrument，使用 PluginDescWidget 过滤/加载；追加准确分类与独立 SVS 路由，保留原浏览器过滤约定 |
| src/gui/editors/TrackContainerView.cpp | instrument 拖放建立 InstrumentTrack；增加独立 svsvoice MIME，Pattern Store 明确拒绝 |
| src/gui/editors/SongEditor.cpp | 新增 SVS 添加入口；传输仍使用 Song |
| src/gui/tracks/TrackContentWidget.cpp, src/gui/clips/ClipView.cpp | 使用 Clip 基础编曲选择/复制/偏移协议，SVS 自己负责内容映射/切片与缩略图 |
| src/tracks/SampleTrack.cpp, include/AudioBusHandle.h, include/PlayHandle.h | 仅参考 AudioBusHandle 组合和播放句柄；新增 SVS 实现，不修改 SampleTrack/InstrumentTrack 语义 |
| src/core/ProjectRenderer.cpp | startProcessing 切换音频设备并 startExport；添加 SVS 快照准备/等待/失败与取消门禁，继续共用混音输出 |
| src/core/RenderManager.cpp | 多轨导出只枚举 Instrument/Sample；最小追加 SVS 判定 |
| src/core/Mixer.cpp, src/gui/MixerView.cpp | mixer channel 删除/加载/清空时仅修正 Instrument/Sample 路由；需要最小追加 SVS 分支，不重构混音处理 |
| src/core/TrackContainer.cpp | Count 只作 countTracks 通配哨兵；automatedValues 特殊分支不应将 SVS 当作 Automation；保留默认分支 |
| src/agent/CoreCommands.cpp | 轨道类型名称 switch 最小补 SVS；Agent 普通 MIDI 编辑入口保持类型检查，不扩展 SVS Agent 功能 |
| src/gui/LmmsStyle.cpp | 文件监视热重载 qApp stylesheet；SVS 响应 StyleChange/PaletteChange，Q_PROPERTY 更新自绘缓存 |
| src/CMakeLists.txt | lmmsobjs OBJECT + 显式各模块源清单；新增独立 SVS 内部目标与显式链接/注册；公共 SDK 单独安装 |
| src/core/CMakeLists.txt, src/gui/CMakeLists.txt, src/tracks/CMakeLists.txt | 新模块显式构建，避免递归收集 tl_ref；主题/SDK/示例包补安装规则 |

新增模型/调度/曲线位于 src/core/svs；轨道 src/tracks/SVSTrack.cpp；编辑器 src/gui/editors/svs（含 operations）；公共 include/SVS*.h；SDK sdk/svs；示例 plugins/SVSExample。禁止普通 PianoRoll、Mixer、Automation、VST/Carla 等顺带重构。枚举引用审计仍在 M1 接入改动时逐项验证，M0 的静态核查不代替运行验收。

## 操作映射

已对照 tl_ref PianoScrollViewOperation.cs 与 LyricInput.axaml.cs；这是开发行为参考，不进入依赖或发行资源。

| 操作 | SVS v0.1 映射 |
| --- | --- |
| 音符创建 / 歌词 | 音符工具空白双击创建并可拖尾；音符双击进入 Qt 原生歌词输入；Enter 确认，Esc 取消；Tab/Shift+Tab 前后导航，IME preedit 不提交 |
| 选择 / 移动 | 空白拖动框选，Ctrl 多选；音符主体拖动整体移动；左右边缘拉伸；Alt 暂时关闭时间吸附；Shift 移动约束时间 |
| 删除 / 剪贴板 | Delete；Ctrl+C/X/V；右键定位粘贴，选区最早时间为锚点；Ctrl+A；上下键移调，Shift 上下八度 |
| 撤销 / 事务 | Ctrl+Z / Ctrl+Shift+Z；begin/update/commit/cancel，松手一次提交；Esc/捕获丢失回滚；边缘自动滚动 |
| 滚动 / 缩放 | 普通滚轮纵移，Shift 横移，Ctrl 横向缩放，Ctrl+Shift 纵向缩放，中键平移；触控板横移 |
| 曲线 | 独立工具选择自由笔/锚点/线性/平滑/擦除；按连续片段时间命中；显式连接/断开、默认重置；不照搬参考的计划外振音功能 |
| 歌词 / 音素 | 批量分配预览后一次提交；延音按能力跳过；候选菜单；音素符号/边界/属性按 Schema 编辑，恢复自动结果 |

计划书 §8.3 优先于参考程序；参考最新源码含计划外功能，均排除。TuneLab 本机交互复核 MANUAL/PENDING；不会阻塞 M1。界面参考图已读取，布局以“布局和功能.png”为准；配色由 LMMS QSS/QPalette 提供，不另建皮肤。

## 自动验证与性能数据集

机器：Windows x64，AMD Ryzen 7 260 / 16 逻辑线程、约 16 GiB RAM；Qt 6.10.3 msvc2022_64；现有 build 使用 Visual Studio 18 2026 / Release。Windows SDK 路径 D:/Windows Kits/10；沿用现有工具链配置。SDK 示例不需要 Qt/Windows SDK/Libjack（常规编译器工具链除外）。

固定验收工程：4 SVS 轨 × 16 片段 × 64 音符；单曲线 4096 锚点；另有三小节/四音高/一个休止的专项片段。记录示例合成耗时、交互响应、缓存和可见区绘制，阈值：操作预览 p95 < 50 ms、GUI 阻塞单次 < 100 ms；并发活任务 ≤ 配置预算、非并发实例 ≤1；缓存不越上限；音频回调不得出现插件/文件/任务等待。此阈值仅衡量宿主及确定性示例，不评价真实模型速度。

M0 静态验证检查枚举原值、工厂、浏览器/拖放、主题与 CMake 入口，检查契约必需族和生命周期关键约束。M1 起执行真实编译及自动运行测试。默认/浅色/旧主题、热更、高 DPI、真实 fixture 头像立绘以及无 TuneLab/.NET 的目标环境验收分别记录 MANUAL/PENDING，自动可测项不能用该标签替代。
