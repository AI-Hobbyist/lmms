# LMMS VST3、WaveShell 与全入口进程隔离实施计划

日期：2026-10-04。代码基线：`dbf7500af67b11200f513bdc5c0b5e3a7ab2f0f9`。

本次交付是可执行的实施计划，不是已经实现的 VST3 功能。本次只提交本文件，不提交根目录 `vst3sdk/`，不修改宿主代码，不执行商业插件。下文新增类型、目录、CMake 开关和测试名称均为拟实施接口；不能当作当前可调用功能。

## 1. 目标与完成边界

| 编号 | 必须实现的行为 | 对应阶段 |
| --- | --- | --- |
| R1 | 保留现有 VST2 工程、乐器、效果器、参数、预设和编辑器行为 | S0、S2、S5 |
| R2 | 用本地 VST3 SDK 实现 VST3 乐器和效果器的扫描、加载、处理、编辑与恢复 | S3、S5 |
| R3 | 同一模块的全部音频类可选择；正确支持 WaveShell 的多个子插件、多个版本和多个实例 | S2、S3、S7 |
| R4 | 所有 VST 原生代码只在子进程运行，包括扫描、预览、工程恢复、命令调用、离线导出及 Carla 间接加载 | S1～S6 |
| R5 | Windows x64 LMMS 能加载 x86 和 x64 插件；依据实际 PE 架构选择 helper | S1、S2、S3、S8 |
| R6 | VST 扫描设置支持多个目录、增删、排序、启停和递归；旧配置、旧工程不失效 | S4、S5 |
| R7 | 插件崩溃或挂起不能使 DAW 因等待插件而退出或永久卡住；失败实例可诊断、重新加载 | S1、S6、S7 |
| R8 | Release 全量编译、自动测试和 ZIP 打包；人工验收按用户要求默认跳过并注明 | S7、S8 |

“完整支持 WaveShell”以本计划的类枚举、身份、处理、状态、参数、界面和故障测试为判据，不以“能打开一个 Waves 插件”代替。商业许可证状态单独记录；发现文件不等于插件可用。进程隔离降低第三方原生代码故障对 DAW 的影响，不是操作系统权限安全边界，也不能保证 LMMS 自身不存在缺陷。

沿用本机 HTTP MCP 作为已有控制入口。此计划不增加云端服务、LLM、联网认证、远程执行或新 MCP 服务；本地桥接 IPC 与 HTTP MCP 是不同通道。

## 2. 已核实的代码入口与缺口

以下位置来自当前 CodeGraph 精确文件／符号检索。实施时从这些入口追踪调用，不以扩展名搜索代替加载路径检查。

| 入口／模块 | 当前行为与实施位置 |
| --- | --- |
| [VstPlugin.cpp](plugins/VstBase/VstPlugin.cpp#L127) | 已有 VST2 DAW 代理；按 PE machine 选择 `RemoteVstPlugin64` 或 `32/RemoteVstPlugin32`。`tryLoad` 目前发送模块路径，缺少 shell 子插件身份。复用兼容接口，原生调用继续留在子进程。 |
| [RemoteVstPlugin.cpp](plugins/VstBase/RemoteVstPlugin.cpp#L969) | 子进程内 `LoadLibraryW`、查找入口、`effOpen`。现有 `audioMasterCurrentId` 返回 0，未发现 `effShellGetNextPlugin` 枚举实现；需补齐 VST2 shell，而非重写成 DAW 内加载。 |
| [RemotePlugin.cpp](src/core/RemotePlugin.cpp#L88) | 已有 ProcessWatcher、Windows Job 关联和终止处理。`waitForStarted(-1)` 无期限；音频 `process` 同步等 `IdProcessingDone`，仍可能被存活但挂起的插件阻塞。 |
| [RemotePluginBase.cpp](src/common/RemotePluginBase.cpp#L136) | `waitForMessage` 和 FIFO 接收缺少完整期限；事件泵等待不能作为可靠的挂起恢复。需统一错误、期限、断连和代次管理。 |
| [Vestige.cpp](plugins/Vestige/Vestige.cpp#L334) | `loadFile` 创建 `VstInstrumentPlugin`；包括文件选择、拖放、reload、工程／预设 `loadSettings`。保存插件路径与状态，参数模型按索引组织。 |
| [VstEffect.cpp](plugins/VstEffect/VstEffect.cpp#L108)、[VstEffectControls.cpp](plugins/VstEffect/VstEffectControls.cpp#L125) | 效果器构造／`openPlugin` 走 VstPlugin；控制层保存路径、状态及 `paramN`。新格式必须持久化稳定身份，VST2 旧索引兼容仍保留。 |
| [VstSubPluginFeatures.cpp](plugins/VstEffect/VstSubPluginFeatures.cpp#L56) | 当前单目录递归枚举 DLL／SO，按文件生成效果器键；不是 VST3 factory 类扫描，也不是 WaveShell 子插件扫描。改为统一目录服务的已验证目录项。 |
| [FileBrowser.cpp](src/gui/FileBrowser.cpp#L1270) | 识别 DLL／SO 并通过插件注册表加载；VST3 bundle 必须作为插件模块而非普通目录进入。预览、上下文加载、拖放均使用同一代理。 |
| [InstrumentTrackWindow.cpp](src/gui/instrument/InstrumentTrackWindow.cpp#L573)、[InstrumentTrack.cpp](src/tracks/InstrumentTrack.cpp#L1040) | 拖放预设／插件文件、注册表选乐器、工程和克隆加载汇入轨道／乐器加载链。包含 `.vst3` 目录类型检查，不只增加文件扩展名。 |
| [EffectSelectDialog.cpp](src/gui/modals/EffectSelectDialog.cpp) | 效果器选择从子插件键构建实例；需展示每个 factory 音频类及版本／位数，不能只展示 WaveShell 文件名。 |
| [CoreCommands.cpp](src/agent/CoreCommands.cpp#L3884) | `executeInstrumentLoad` 用 `loadInstrument` 和 `loadFile`，当前 `path` 要求 `QFileInfo::isFile()`，会拒绝目录式 VST3。改为目录项身份或经过验证的原子 bundle 路径。 |
| [CoreCommands.cpp](src/agent/CoreCommands.cpp#L1914)、[ProjectSnapshot.cpp](src/agent/ProjectSnapshot.cpp) | `executeEffectAdd` 通过 `Effect::instantiate`；快照、撤销、回滚和恢复复用工程加载。必须覆盖这些潜在实例化过程，等待不占用音频变更锁。 |
| [Carla.cpp](plugins/CarlaBase/Carla.cpp#L152)、[CarlaRack.cpp](plugins/CarlaRack/CarlaRack.cpp)、[CarlaPatchbay.cpp](plugins/CarlaPatchbay/CarlaPatchbay.cpp) | CarlaInstrument 当前直接调用 native descriptor 的 instantiate／activate；保存状态也直接调用 `get_state`。Carla 内部能加载 VST2／VST3，因此是独立的隔离缺口。必须改成代理和外部 Carla 宿主。 |
| [ConfigManager.cpp](src/core/ConfigManager.cpp#L529)、[SetupDialog.cpp](src/gui/modals/SetupDialog.cpp#L881) | `paths.vstdir`、`setVSTDir`、设置页都是单目录。增加目录集合，保留旧根用于工程解析。 |
| [PathUtil.cpp](src/core/PathUtil.cpp#L62) | `UserVST` 与 `uservst:` 依赖 `userVstDir()`；旧相对路径也按该根解析。不能把它随扫描列表排序改成新的第一个目录。 |

以上覆盖直接 VST2 桥、注册表／GUI／命令／持久化入口与 Carla 间接宿主。S0 还须以运行时模块装载记录核实预览、导出、恢复等动态路径，交付入口清单和实例创建调用图；发现新增路径应补入清单并纳入测试。不得因静态调用图未显示动态分派而认定不存在入口。

## 3. 本地 SDK 与样本依据

实现依据为根目录 `vst3sdk/` 的 **3.8.0** SDK（其 README 使用 3.8.x 表述），不额外引入 JUCE 宿主实现。使用 `base`、`pluginterfaces`、`sdk_common`、`sdk_hosting`；后者由 `cmake/modules/SMTG_VST3_SDK.cmake` 创建。按实际目标依赖接入 Windows 模块加载实现。生产构建关闭 SDK 插件示例、hosting 示例及不需要的 VSTGUI；测试构建单独启用需要的 fixture／validator。

本地可复核基准：

- `vst3sdk/CMakeLists.txt` SHA256：`A111E0DE9EE9D2BB4E0C7AE8092A4E445EA63BB32A0BB6CA227EBBDDFC339E8C`。
- `vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h` SHA256：`ED14B81DAE653FDBED6BAF3FF5E071F20CDC9FC34CC4D3FEEA06746C3AB067F8`。
- SDK 根 LICENSE 为 MIT；未来接入时记录 SDK 版本、来源和校验值，并保留许可。当前提交不包含 SDK。构建应明确报 SDK 缺失，不能自动下载代替用户指定版本。

优先参考 `public.sdk/source/vst/hosting/module.h`、`module_win32.cpp`、`plugprovider.h`、`hostclasses.h`、`parameterchanges.*`、`eventlist.*`、`processdata.*`，以及 `pluginterfaces/vst/` 与 `pluginterfaces/gui/` 的实际接口。SDK 示例是生命周期与数据结构参考，不应直接把示例的全局状态、多实例假设或线程安排移入宿主。

| 本机样本 | 已发现的形态 | 计划用途 |
| --- | --- | --- |
| `C:\Program Files\Common Files\VST3\WaveShell1-VST3 16.6_x64.vst3` | 单文件，10,279,248 字节 | 全类枚举、与 16.7 共存、旧工程绑定 |
| `C:\Program Files\Common Files\VST3\WaveShell1-VST3 16.7_x64.vst3` | 单文件，10,281,304 字节 | Waves 主验证样本、多子插件、多实例 |
| `C:\Program Files\Common Files\VST3\WaveShell1-VST3-ARA 16.7_x64.vst3` | 单文件，10,281,296 字节 | 按用户要求不纳入测试 |
| `C:\Program Files\Common Files\VST3\Youlean Loudness Meter 2.vst3` | 单文件 | 效果器、界面、参数／测量状态 |
| `C:\Program Files\Common Files\VST3\Serum2.vst3` | bundle，有 `Contents/x86_64-win` | 乐器、MIDI、状态、目录式模块 |
| `C:\Program Files (x86)\Common Files\VST3\Plugins.VST.NDI.Input.vst3` | bundle，同时有 `Contents/x86-win`、`Contents/x86_64-win` | 双架构路径解析；实际 PE、依赖、处理能力仍须验证 |

目前只核实文件存在及形态，未运行扫描、实例化、授权校验或听音。NDI 的外部输入不作为确定性音频判据；x86／x64 音频与故障主测试使用可控 SDK fixture。

Waves 官方说明 WaveShell 是实际插件的入口，不能移动 Waves 的 `Plug-Ins Vxx` 安装目录；自定义扫描目录只复制 shell。**Waves V15 起不再支持 VST2**，本地 V16 样本应走 VST3，不能用 VST2 shell 作为替代。[Waves 官方目录说明](https://www.waves.com/support/how-to-use-waves-plugins-when-using-custom-vst-folder)

VST3 支持一个模块导出多个类，Windows 既有单文件形态，也有按架构组织的 bundle。扫描必须枚举音频类并解析实际架构，不以文件名推断类型或只选第一个类。[Steinberg 模块加载](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/VST%2BModule%2BArchitecture/Loading.html)、[模块格式](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html)

## 4. 宿主边界与进程模型

```text
LMMS 进程
  设置／浏览器／乐器／效果器／工程恢复／HTTP MCP／导出
                       │
                Catalog + HostSession 代理
                       │  固定格式 IPC + 预分配共享音频缓冲
             ┌─────────┴────────────┐
       一次性扫描子进程         每实例独立处理子进程
       模块／类探测           x86 helper 或 x64 helper
                                  │
                           VST2 / VST3 adapter
                           原生组件、控制器、GUI

CarlaRack / Patchbay 代理 → 独立 Carla 宿主进程
                           → 每个 VST 实例的独立 bridge
```

拟新增 `src/vsthost/`、`include/vsthost/` 和 `tools/vsthost/`，将目录项、会话代理、进程监督与格式适配器聚合为少量有明确职责的模块。现有 `VstPlugin` 保留兼容外观，逐步委托公共会话；Vestige、VstEffect 只持有代理和 LMMS 参数模型。所有 SDK module／factory／component／controller／view 对象都在子进程中，扫描也不在 DAW 内 `LoadLibrary`。

一个活动插件实例对应一个处理进程；两个相同 CID 的实例不能共享可崩溃的原生组件进程。扫描 worker 可按架构复用队列，但一次只探测一个模块，失败后销毁 worker。不要在所有实例之间共享同一个 WaveShell 进程。节省进程开销的宿主池不列入初版。

Carla 至少将整个 native 宿主移出 DAW，并强制每个内部 VST 走独立 bridge。须在真实 Carla 后端上验证此模式、VST3 和双架构能力；若当前后端不支持所需模式，则补齐或用公共 HostSession 接管 VST 节点。仅把整个 Rack 放入一个进程可以保护 DAW，却不足以满足每个 VST 独立隔离，不能据此宣告完成 R4。保持 Rack/Patchbay MIDI、音频路由及持久化，错误定位到内部节点。

### 4.1 会话、通信和故障策略

- 状态机：`Stopped → Starting → Ready → Processing → Faulted → Stopped`，退出另有 `Stopping`；状态变化通过控制线程通知 UI，不在音频线程重建插件。
- 每个会话包含不可重用 session ID、递增 generation、请求序号和结构化错误。重启后丢弃上一代音频／控制回复，防止旧消息写入新实例。
- IPC 协议头固定宽度，版本握手；不直接传 C++ 对象、Qt 类型、指针、`size_t`、原生句柄布局或跨位数 `sizeof` 结构。字符串用长度限定 UTF-8，CID 为原始 16 字节，VST2 ID 为无符号 32 位值；共享区偏移和尺寸逐项校验。
- 音频／事件环与控制／状态通道分离，预分配缓冲。实时线程不等控制锁、GUI、进程启动或状态序列化，不执行无期限阻塞、不分配堆内存、不调用插件原生函数。
- 初版采用一块流水线处理和非阻塞结果读取；该块引入的桥延迟加上插件报告延迟必须进入 LMMS 延迟补偿。不得只给代理记一个数字而不修改实际混音路由。迟到结果不可在后续错误块使用。
- 在效果链、混音 send、干湿并行和 Carla 路由中计算累计延迟，给较短路径补偿；动态延迟变化在安全块边界重建。离线导出使用同一时序，补首尾延迟、保留尾音；用脉冲验证并行相位与样本位置。
- 初始可配置期限：启动 30 秒、模块扫描 30 秒、单类探测 15 秒、状态操作 15 秒、正常退出 2 秒；期限均由单调时钟和监督线程执行。实时截止由块序号／音频时间决定，不使用 15 秒控制超时。超过预算先隔离实例，再终止子进程。
- 插件处理失败时乐器输出静音；效果器默认静音或使用预先配置且延迟匹配的安全 bypass。记录实例、身份、阶段、错误、退出码、架构和日志位置，不弹出反复阻塞音频的对话框。
- 离线导出默认明确失败，保留诊断但不把缺插件的文件当作成功作品；用户显式选择跳过失败效果后才允许继续。GUI 关闭、插件 remove、DAW 正常／异常退出应释放 Job、共享区、管道、窗口和子孙进程。
- Windows 保留现有 Job 思路，正确处理句柄继承、父进程退出、启动失败和终止竞态。插件必要的本地资源／许可证访问仍可用；不因“沙盒”名称加入低权限或断网措施破坏 Waves 授权。

### 4.2 公共代理接口（拟新增）

`PluginCatalog` 提供 `scan(roots)`、按身份查询及候选版本选择；`HostSession` 提供异步创建／关闭、块处理、参数事件、MIDI、状态读写、编辑器和故障通知。VST2/VST3 adapter 在 helper 内实现上述真实需求，不把格式专有接口传播到 GUI。

`PluginIdentity = format + audioClassID + architecture`；VST3 的 audioClassID 为 component CID；VST2 为模块／插件 ID 加可选 shell 子 ID。vendor/name 仅用于显示。VST2 unique ID 可能碰撞，必须结合模块来源及兼容绑定，不能单独作为全局唯一键。

`PluginLocator` 单独保存模块／bundle 路径、实际二进制路径、版本及指纹。身份决定选择的子插件，locator 决定具体安装版本；多个安装位置是候选，不因扫描顺序自动换版本。控制器 CID 与组件 CID 分开保存，不把控制器注册为效果器。

## 5. 格式适配要求

### 5.1 VST2 保留与 shell 补齐

1. 原有 `RemoteVstPlugin` 的 dispatcher、音频、MIDI、GUI、chunk、program 和时间信息能力先形成回归基线，再迁移公共通信。旧项目无需重写插件类型；原相对路径与参数索引继续读写。
2. 扫描模块后查 `effGetPlugCategory`；shell 类用 `effShellGetNextPlugin` 获取全部子 ID／名称。扫描本身与子类初始化只在可终止 helper 内运行。
3. 实例化时将选定子 ID 传入 helper，**在调用插件入口前**使 `audioMasterCurrentId` 返回该 ID；不能先打开默认子插件再改身份。用原始 32 位 ID 序列化，避免四字符字符串、符号扩展和字节序错误。
4. shell 的扫描、子实例、状态恢复与 GUI 均测试；无法获得本地旧版 Waves VST2 时使用自建多子插件 shell fixture，记录商业样本未覆盖，不混同 V16 VST3 已覆盖。
5. 补齐插件 UI 参数改变向 LMMS 自动化回传、正常退出和取消挂起行为；不让公共桥迁移丢失已有 VST2 预设、嵌入编辑器与 x86 支持。

### 5.2 VST3 生命周期与宿主接口

1. 用 SDK `Module::create` 加载实际模块，枚举 factory 的全部 `kVstAudioEffectClass`。通过 `ClassInfo`／CID 建立目录，保留类别、厂商、版本和 instrument/effect 类型信息。moduleinfo 元数据可加速显示，不能替代可用性验证。
2. 创建 `IComponent`，设置 `IHostApplication` 并 initialize；取得 `IAudioProcessor`；依据组件指定 controller CID 或同对象控制器建立 `IEditController`，正确 initialize，连接 `IConnectionPoint`，配置 component handler。参考 PlugProvider 的建立与拆除次序，不共享跨实例原生对象。
3. 协商 bus arrangements、激活所需音频／事件 bus、setupProcessing、setActive、setProcessing；关闭按逆序释放 connection、controller、component 和 module。每一步返回值与超时都可诊断，部分初始化失败也完整清理。
4. 支持 float32；插件仅支持 float64 时协商并在 helper 预分配转换缓冲。确认采样率、最大块长、可变块长、实时／离线模式、静音旗标和时间信息；不可假设固定 2 输入／2 输出或固定 block size。
5. 主总线 mono／stereo 映射显式定义；多输出乐器、sidechain 和额外总线在 LMMS 路由层暴露可选择的端口。未连接 bus 明确停用或提供符合 SDK 的缓冲，不能伪称连接。S5 包含路由扩展，不把依赖 sidechain 的 Waves 插件视为“能打开就通过”。
6. 音符、力度、通道、note-off、pitch bend、CC／MIDI mapping、program／unit 与插件产生的输出事件正确转换；事件携带块内 sample offset，排序与范围有界。实现宿主确实承诺的接口；可选 note expression／MIDI2 等查询不支持时按协议拒绝，不返回虚假的成功。
7. 参数用稳定 `ParamID` 而非展示顺序；维护 normalized 值、字符串显示和可自动化／只读／bypass 属性。`IParameterChanges` 保留样本偏移；UI `beginEdit/performEdit/endEdit` 回传为 LMMS 编辑／自动化操作，避免反馈循环和跨线程直接调用。
8. `restartComponent` 涉及参数列表、I/O、latency、program 等变化时异步重建对应缓存／路由，不在回调中销毁插件。支持插件消息通信和实际需要的 host interface；接口支持列表与真正实现保持一致。
9. 保存 component state 和独立 controller state；恢复 component 后正确调用 controller 的 `setComponentState`，再恢复 controller state。状态操作遵守接口线程要求、与处理互斥，使用异步屏障；不在实时线程存盘。状态 blob 有大小上限／校验，失败仍保存原工程中的旧 blob。
10. `.vstpreset` 读写使用 SDK 格式／CID 校验；不能把任意字节误当 VST2 chunk。程序、参数模型及自动化绑定在项目、预设、复制、撤销／重做、缺插件占位场景保持一致。
11. `IPlugView`、`IPlugFrame` 和原生 HWND 在 helper 的 GUI 消息线程托管。优先支持独立原生窗口；嵌入需要验证跨进程及跨位数窗口、DPI、resize、键盘焦点、关闭顺序。DAW 只代理窗口控制；不因缺 x86 Qt6 而退回进程内 GUI。

参数 ID／自动化、component/controller 状态及宿主编辑回调遵循 SDK 语义。[参数与自动化](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Parameters%2BAutomation/Index.html)、[持久化](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Persistence.html)、[IComponentHandler](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponentHandler.html)

### 5.3 WaveShell 专项

- 分别枚举普通 VST3 的 16.6、16.7 模块全部音频类；以独立 SDK inspector 的 CID 集合为对照，过滤非音频类，不根据 Waves 名称规则猜子插件。显示类名、模块版本、架构、mono/stereo 类和实际 bus 能力。测试清单显式排除 ARA shell。
- 每次加载传精确 component CID；mono/stereo 若不同 CID 则为不同目录项，同 CID 多种布局则为可协商布局。两个不同子插件、两个相同子插件都分别建立组件、控制器、状态和处理进程。
- 同 CID 在多个 shell 版本出现时提供候选版本并保持项目的原 locator。原版本缺失时提示候选，恢复必须确认兼容身份，不默认加载扫描到的第一个模块。
- 缓存除 shell 路径／时间／大小／指纹外，记录实际 Waves 安装版本信息；提供强制冷扫描。Waves 更新依赖或真实插件但 shell 文件未变时，也能显式使缓存失效。坏类的探测超时应记录该类并继续其余类，不把整个已成功的目录清空。
- 保持安装路径、依赖 DLL 和许可证服务；在 helper 内报告 factory、授权、依赖和实例化失败。DAW 中不用商业插件原生接口探测授权，不尝试自动激活许可证。
- 同时开多插件、自动化、UI 改值、save/reload、sample-rate/block-size 更换、mono/stereo、sidechain、latency、尾音、offline 和子进程终止必须进入测试矩阵。
- 按用户要求不测试 ARA shell，也不实施 ARA document model／编辑集成；报告将其注明为范围外，不声明 ARA 支持。这一排除仅适用于 ARA 版本，不减少普通 WaveShell 16.6／16.7 的类覆盖。
- 不声称取得 Waves 官方认证；本地样本的测试结果应标明产品、版本、CID 和覆盖范围。[Waves 支持宿主列表](https://www.waves.com/support/tech-specs/supported-hosts)

## 6. 多目录、目录缓存与工程迁移

拟在现有 ConfigManager 标量配置模型中增加 `paths.vstscanroots`，值为版本化 JSON 字符串：

```json
{"version":1,"roots":[{"path":"C:/Program Files/Common Files/VST3","formats":["vst3"],"recursive":true,"enabled":true}]}
```

首次迁移加入旧 `paths.vstdir` 与存在的 Windows VST3 标准目录；相同路径去重。设置页用列表支持添加、删除、排序、启停、格式范围、递归、立即／强制扫描及扫描进度。无效／不可访问目录按项报告；一个目录失败不取消其他目录。无目录时显示空目录，不偷偷扫描全盘。

**保留 `paths.vstdir` 为旧工程的固定兼容根**，`vstDir()`／`userVstDir()` 仅供迁移和 `uservst:` 旧路径解析，新的扫描不用它们。目录列表重排、禁用或移除不改此根；用户显式迁移旧根时列出影响，并采用旧根仍可解析或重写 locator 的有记录迁移。新增项目保存稳定身份和独立 locator，不能继续把插件身份等同文件名。

扫描规范：

1. 规范化 Windows 大小写、分隔符、Unicode、UNC／长路径，去除重叠根的重复项；junction／symlink 防循环。网络盘是可选本地扫描根，不引入网络服务；不可访问时可取消。
2. `.vst3` 文件／目录均视作原子模块；bundle 不递归把内部二进制当另一模块。只选合法架构入口，排除 `.BAK` 等备份。不可根据 `Program Files (x86)` 判断模块一定为 x86。
3. 读取 PE machine 与依赖／模块布局的静态信息后选择对应 worker。仅支持的 ABI 才执行；错误 PE、缺 helper 或缺依赖给出明确状态，不让主进程试载。
4. 目录缓存按 schema／宿主版本／模块指纹失效，写入原子替换。缓存与失败隔离名单分离；失败后用户可重试，禁止仅凭上次成功就在 DAW 内跳过隔离加载。
5. 刷新目录不改变活动实例；后台目录服务的生命周期与音频会话分开。处理扫描取消、文件被替换、目录被移除、并行扫描和 DAW 退出。
6. 工程缺插件时保留身份、原 locator、参数映射、自动化、bus 配置和原状态。显示可恢复占位，保存不能抹掉未知状态；重新定位后只按匹配身份恢复。

## 7. 可执行阶段与依赖

| 阶段 | 负责范围／拟修改文件 | 交付与完成判据 |
| --- | --- | --- |
| S0：基线与契约 | 上述入口、现有 VST2 测试、`tests/vsthost/` 契约 fixture | 固定旧工程样本；记录全部实例化入口／模块装载；确定身份、错误、IPC、bus 和状态 schema。先获得现有 x86／x64 VST2 回归结果。 |
| S1：公共进程桥 | `RemotePlugin*`、`RemotePluginBase*`、`RemotePluginClient*`、`SharedMemory*`、`SystemSemaphore*`；新增 HostSession／Supervisor、helper 构建入口 | 双架构协议、控制超时、generation、防挂起、Job 清理、预分配音频队列。原 VST2 先接入并通过故障 fixture。依赖 S0。 |
| S2：VST2 与 shell | `plugins/VstBase/`、Vestige、VstEffect 兼容适配 | 保留现有行为，补 shell 全枚举／指定子 ID、旧状态、GUI 自动化回传。x86/x64 shell fixture 与普通 VST2 回归通过。依赖 S1。 |
| S3：VST3 adapter | `tools/vsthost/` 的 VST3 scanner／adapter、CMake、本地 SDK 引用 | factory 全类、生命周期、基础 bus、处理、事件、参数、状态、native editor；双架构 SDK fixture 通过。依赖 S1。 |
| S4：多目录与目录项 | ConfigManager、SetupDialog、PathUtil、VstSubPluginFeatures、新 PluginCatalog | 单目录迁移、多目录扫描、bundle、重复类／版本候选、缓存／取消；旧 `uservst:` 在列表重排后仍正确解析。依赖 S2/S3 的身份契约。 |
| S5：LMMS 全入口与路由 | Vestige／VstEffect、FileBrowser、EffectSelectDialog、InstrumentTrackWindow、InstrumentTrack、CoreCommands、ProjectSnapshot、EffectChain／Mixer 路由 | 乐器／效果器统一选择；bundle 命令校验；工程／预设／撤销；multi-out、sidechain、MIDI、动态延迟补偿与 offline 时间线。导出、预览和命令路径通过同一代理。依赖 S2～S4。 |
| S6：Carla 全隔离 | CarlaBase／CarlaRack／CarlaPatchbay、Carla native bridge、专用宿主及构建 | 外部 Rack/Patchbay，内部每个 VST 独立 bridge；保持路由／状态；真实 Carla 后端的 VST2/VST3、x86/x64 和故障测试。依赖 S1/S5。 |
| S7：WaveShell 与故障矩阵 | 自动测试 runner、SDK fixture／inspector、测试报告 | 本地 Waves 类集合与 inspector 一致；可授权类实例、状态、参数、路由和故障测完；失败／跳过有原因与范围。自动故障矩阵全部通过。依赖 S2～S6。 |
| S8：Release 与 ZIP | 根 CMake、打包规则、桥接依赖清单 | 全量 Release、两种 helper、现有与新增测试、干净 PATH 启动／渲染、ZIP 内容及依赖检查；生成 SHA256、版本和已跳过项报告。依赖 S7。 |

按阶段提交，每个阶段先通过其可自动执行的完成判据，再进入依赖阶段；不因人工听音或 UI 观感等待。关键路径为 S0→S1→S3→S5→S6/S7→S8。估时需在 S0 fixture 与 Carla 实际 bridge 能力核实后给出，不用未经验证的工期承诺掩盖路由和跨位数工作。

## 8. 自动测试与人工跳过规则

拟新增 CTest 测试组 `VstHostProtocol`、`VstHostFaults`、`Vst2Compatibility`、`Vst2Shell`、`Vst3Lifecycle`、`Vst3Catalog`、`Vst3Processing`、`Vst3StateAutomation`、`VstPathsMigration`、`VstEntryPoints`、`CarlaIsolation`、`WaveShellInventory`。名称尚未实现。

| 测试组 | 必须验证的情况与判据 |
| --- | --- |
| ABI／IPC | x64 DAW 分别运行 x86／x64 helper；PE 选择正确；错误协议版本、超长 blob、坏偏移／长度、乱序、断连、上一代回复均安全拒绝。 |
| 扫描 | factory／类探测崩溃或挂起、取消、坏 PE、缺依赖、多根重复、bundle、junction、Unicode、备份；DAW 存活，剩余模块继续扫描，目录写入完整。 |
| VST2 | 普通插件及 shell 多子 ID；旧工程、`uservst:`、program/chunk、GUI、MIDI、参数、双架构行为不倒退。 |
| VST3 音频 | 确定性 fixture：mono/stereo、sidechain、多输出、float32/64、变块、采样率变更、参数 sample offset、MIDI 音符／CC／bend、latency 更新、尾音和 offline。比较已知波形／脉冲时序，不依赖听音。 |
| 状态与自动化 | 组件／控制器不同对象、不同 CID、参数顺序变化、UI 改值回传、preset、save/reload、clone、undo/redo、缺插件恢复、版本定位；按 ParamID 绑定且状态不串实例。 |
| GUI | 可控 fixture 自动创建／关闭／resize 编辑器，反复 50 次，测试 x86/x64 native window。DPI、焦点和真实商业 UI 体验的人工部分单独跳过。 |
| 实例隔离 | 至少 16 个混合实例；在扫描、初始化、音频、GUI、状态保存、卸载各阶段注入 crash/hang。DAW PID 保持、控制命令在其正常期限内返回、未故障轨持续输出；故障实例重建后代次正确。 |
| 资源 | 创建／销毁／重建循环至少 50 次；无遗留 helper／子孙进程、共享区、窗口、Job／管道句柄；对比运行前后对象／进程记录。 |
| 入口覆盖 | 文件选择、浏览器预览／拖放、效果器选择、工程打开／克隆、preset、reload、MCP 乐器／效果器命令、snapshot／undo、offline 和 Carla。运行时装载记录中第三方 VST 模块只出现于对应子进程。 |
| Carla | 使用真实 native 后端而非 DummyCarla；Rack／Patchbay 内插件单独崩溃／挂起，DAW 和其他插件继续；内部 routing、状态、不同 ABI 正确。 |
| Waves | 独立 SDK inspector 与宿主扫描音频 CID 集合完全相同；16.6/16.7 并存、mono/stereo、同 CID 双实例、不同 CID、状态恢复、GUI 参数回传、动态延迟与 sidechain。无法授权的类记录环境失败，不算通过。 |
| 打包 | Release 解压后干净 PATH 启动；helper 双架构齐备且架构依赖不混用；离线渲染输出正确；缺 SDK 不影响已打包程序运行。 |

SDK validator 用于验证 fixture／插件接口合规，不能代替本宿主的扫描、桥接和恢复测试。独立 inspector 应在受监督进程运行，并记录模块指纹、CID 集合和架构，不能用同一份宿主缓存与自己比对。

测试报告状态固定为 `PASS / FAIL / SKIPPED_MANUAL / BLOCKED_ENVIRONMENT / NOT_RUN`：

- 用户要求所有人工验收默认跳过并继续；听音、主观 UI、实机体验、许可证手动激活、官方认证请求均记录 `SKIPPED_MANUAL`、原因与未覆盖范围，不等待人工签字。
- 可以自动执行的构建、fixture、超时、恢复、音频时序、进程清理和旧工程检查不因这一规则跳过。真实插件需人工授权时记录环境／人工状态，继续无授权 fixture 与其余模块。
- 本地前一版 Release 使用 DummyCarla，不能拿其通过记录证明真实 Carla 隔离完成。实施阶段须安排可运行的 Carla 自动 fixture 环境；若不可得，应明确未覆盖 R4 的该分支，不能以跳过代替已完成。
- 当前计划中的实现测试全部 `NOT_RUN`；本次仅进行了代码／SDK／样本静态核实。已有旧版编译结果不是新增 VST3 功能通过证据。

## 9. 构建、打包与发布操作约束

Windows 以 x64 DAW 为主，另行构建 Win32 和 x64 helper；所有加载架构依赖放在各自目录，不能将两种同名 DLL 覆盖。x86 helper 使用 SDK 与原生 Win32 GUI 消息循环，避免将不存在的 x86 Qt6 构建当作前置要求。主程序 Qt6 使用 `C:\Qt\6.10.3`，Windows SDK 使用 `D:\Windows Kits\10`。如提供 x86 DAW，需增加其构建矩阵，不能从 helper 支持推断 DAW 自身也可构建。

拟新增 `LMMS_VST3_SDK_ROOT`、`LMMS_BUILD_VST_HOST_ONLY` 和 helper 目标 `RemoteVstHost`；S1/S3 实施后才可运行以下示意命令。主构建 generator、Qt／vcpkg／现有依赖参数沿用当前可成功的 Release 配置；helper 独立子工程仅链接相应架构宿主必需依赖。

每条 configure、compile、test、package 都必须在前台使用 AGENTS.md 的日志管线。建议统一调用函数，非零退出立即读日志并停止该动作，不吞错误：

```powershell
function Invoke-LoggedNative {
    param([string]$Executable, [string[]]$NativeArgs)
    & $Executable @NativeArgs 2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8
    $vstBuildExitCode = $LASTEXITCODE
    if ($vstBuildExitCode -ne 0) {
        Get-Content -LiteralPath "build.log" -Tail 100
        throw "$Executable failed: $vstBuildExitCode"
    }
}

# 以下参数／目标由 S1/S3 新增；不是现有构建的即用命令。
Invoke-LoggedNative cmake @('-S','tools/vsthost','-B','build/vsthost-x86','-G','Visual Studio 17 2022','-A','Win32','-DLMMS_VST3_SDK_ROOT=D:/UserData/Desktop/Project/lmms/vst3sdk')
Invoke-LoggedNative cmake @('--build','build/vsthost-x86','--config','Release','--target','RemoteVstHost')
Invoke-LoggedNative cmake @('-S','tools/vsthost','-B','build/vsthost-x64','-G','Visual Studio 17 2022','-A','x64','-DLMMS_VST3_SDK_ROOT=D:/UserData/Desktop/Project/lmms/vst3sdk')
Invoke-LoggedNative cmake @('--build','build/vsthost-x64','--config','Release','--target','RemoteVstHost')
# 主 Release 配置确认上述 helper 的安装来源后，全量编译并运行旧／新测试。
Invoke-LoggedNative cmake @('--build','build/release','--config','Release')
Invoke-LoggedNative ctest @('--test-dir','build/release','-C','Release','--output-on-failure')
# 打包脚本须先在 S8 形成；自身也用同一前台日志管线执行。
```

每次日志可在下一动作前按架构／阶段归档，但执行时始终生成根 `build.log`。脚本打包还应检查 PowerShell 异常并返回非零码，不能只依赖 ZIP 命令输出。Release 包含主程序、原有 VST2 helpers、公共 x86/x64 helpers、各自运行库、Qt／LMMS 资源及许可；不包含商业插件、Waves 安装文件或整份 SDK。输出 ZIP、SHA256、构建版本、自动结果和跳过清单，供实机测试。

提交范围仅本计划；之后实现阶段按实际源码变化提交，SDK 引用与本机路径由配置提供。当前 `vst3sdk/`、构建产物及其他未跟踪文件不得随本计划提交。

## 10. 实施最终检查表

- [ ] 所有登记入口在运行时都只通过代理加载，DAW 无第三方 VST module／factory／controller／GUI 调用。
- [ ] 普通 VST2、VST2 shell、VST3 全类、双架构和旧工程回归通过。
- [ ] WaveShell 类集合、精确 CID、版本绑定、多实例、state／automation／bus／GUI 结果可追溯；授权／ARA 限制有单独记录。
- [ ] 多目录迁移不改变旧 `uservst:` 根，缓存、取消、失败和重复候选正确。
- [ ] 音频不受无期限 IPC 阻塞，桥与插件延迟真正进入路由补偿，导出时序／尾音正确。
- [ ] 真实 Carla Rack/Patchbay 外部宿主与内部每 VST bridge 完成，并有自动故障证据。
- [ ] Release 全量构建、自动测试、干净环境运行、ZIP 依赖／许可检查通过；所有人工项按用户要求注明跳过。

本表是后续实现验收表，当前保持未勾选。计划书完成与推送不代表上述功能已经实现。
