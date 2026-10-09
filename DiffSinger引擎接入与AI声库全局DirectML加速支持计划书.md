# DiffSinger 引擎接入与 AI 声库全局 DirectML 加速支持计划书

编写日期：2026-10-08。状态：A0～A4 已依次完成必需验收；本次不实施 B。依据当前 LMMS 工作区及 `refs/tl_ref`、`refs/tlds_ref`、`refs/Singers` 的实际内容；不能把参考代码或设置中的占位项当作已完成能力。

严格分为两部分：**A：先完成可独立运行的原生 DiffSinger CPU 引擎；B：再完成适用于接入统一推理接口的 AI 引擎的全局 DirectML 后端。** A 全部验收、提交和推送后才实施 B。本计划授权范围为原生 SVS 插件、必要 SDK 增量、独立推理模块及其最小宿主接入，不是普通 LMMS 子系统重构计划。

## 1. 当前项目与参考依据

分析基线 LMMS：`0370ec66a684fec4541f4992b3e8b45ee590df68`；TuneLab 参考：`813d09ae6acbec5b25223c1e0c0c1a7ea7176a3d`；DiffSingerForTuneLab 参考：`57fd03daa2336a326e627372ca2d0c95e0da65f3`。实施时重新核对当前工作区，不回退其他任务的改动。

| 已有事实 | 源码依据 | 对计划的约束 |
| --- | --- | --- |
| SDK 已为 C ABI 1.2，具有 catalog、capabilities、pronunciation、资源、session、ranges、engine settings | `sdk/svs/include/svs.h:20`、`:99`、`:124` | 复用；只追加必要可选接口，保留 1.0/1.1/1.2 插件 |
| 插件 Registry 扫描宿主 svs 目录的一级 manifest，缓存一次，空声库目录会被跳过 | `src/core/svs/SVSPlugin.cpp:195` | 插件扫描与声库扫描分开；必须允许已安装引擎零声库，并支持显式刷新 |
| 设置有真实 DXGI LUID，但 DirectML/LibTorch/Vulkan 都是 Coming soon | `src/gui/editors/svs/SVSSettingsPage.cpp:36`、`:42`、`:61` | 当前没有真实 GPU 推理；B 必须替换 DirectML 占位行为 |
| 合成快照硬编码 CPU | `src/core/svs/SVSClip.cpp:125` | B 必须传递解析后的有效后端，不能仅改下拉框 |
| 目前仅引擎设置值变化会让对应 clips 失效 | `src/gui/editors/svs/SVSSettingsPage.cpp:113` | B 必须把全局后端变化接入失效、取消和重新合成 |
| 插件实例访问串行，调度器避免同一插件占多个等待线程；结果有 generation/revision/request 门禁 | `src/core/svs/SVSPlugin.cpp:112`；`SVSSynthesisScheduler.cpp:26`；`SVSClip.cpp:104` | 首版保留现有调度，不为 GPU 改造音频框架 |
| 资源图片当前被限制在插件包路径 | `src/core/svs/SVSPlugin.cpp:106`；`src/gui/editors/svs/SVSImageLoader.cpp:27` | 外部声库图片通过已授权资源 ID 接入，不能简单放开任意路径 |
| 用户所称 singers 实际为 `refs/Singers`，六个外壳目录各嵌套真正声库根 | 详见 `doc/svs/DiffSinger-reference-findings.md` | 不能假设根目录名或固定深度，六包全部纳入验收 |

TuneLab 只用于不可变快照、音符/音素身份、生成与手工数据分离、参数能力声明、推理链的语义参考。其 .NET/Avalonia SDK、程序集、编辑器、参考工作区不成为 LMMS/插件/SDK/Release 的运行依赖。参考行为与 LMMS tick/秒、立体声 PCM 等既有协议不同的地方在插件内显式转换，不替换宿主协议。

研究证据与具体声库清单位于 [DiffSinger-reference-findings.md](doc/svs/DiffSinger-reference-findings.md)。参考实现不是规范：例如 YAML/TXT 合并丢字段、DML 设备固定为 0、缺少 DML 必要 session 配置等，必须按本计划纠正。

## 2. 范围、目标和排除项

最终用户能够配置声库根目录，递归发现真实 DiffSinger ONNX 声库，选择声库和说话人、显示头像/立绘，在现有 SVS 钢琴窗编辑歌词、音素、音高和模型支持的曲线，CPU 合成、播放、导出、保存重开；随后通过 SVS 全局后端选择 DirectML 和系统真实设备，让符合计算接口的 AI 引擎实际使用该后端。

“全局”指：一份宿主设置、同一个设备身份/探测规则、共同 SDK 推理契约与运行时、所有参与该契约的 AI 引擎采用同一冻结策略。不能通过选择器强制改变任意第三方插件内部的私有计算代码。未接入接口的 AI 插件明确报告未支持；传统拼接和非 AI 示例不改后端，也不受 GPU 设置影响。用第二个最小 AI 测试插件证明共享能力，而不是只有 DiffSinger 特例。

不实施 CUDA、LibTorch、Vulkan、模型训练/转换/下载、在线声库商店、任意 C# 音素器加载、VST/Carla 改造、普通 PianoRoll/InstrumentTrack/Mixer/Audio Engine/Automation 重构、独立皮肤系统或实时逐采样神经合成。已有 LibTorch/Vulkan 占位项保持未实现，不借本计划补齐。发现相邻问题只记录 follow-up。

参考声库仅作本机 fixture，不修改源包、不将 ONNX、PNG、字典或个人配置随 SDK/Release/ZIP 分发；声库可由用户从任意明确配置的位置安装。首次安装无需 TuneLab/OpenUtau 软件或其安装路径。

## 3. 总体接入方式

新增独立原生 `plugins/SVSDiffSinger`，导出 `svs_get_api`，manifest 声明稳定插件 ID、名称 `DiffSinger`、`engineType: ai`。必要模块：VoiceDiscovery、MetadataReader、VoiceConfig、Pronunciation、Duration/Pitch/Variance/Acoustic/Vocoder、InferenceAdapter、ResourceCatalog、Session。插件内部可使用 C++，公共边界只用现有 C ABI。

A 使用原生 ONNX Runtime CPU EP；不调用 Python/.NET/OpenUtau 可执行程序。以公开数据格式和有许可的算法移植实现原生音素化。A 的 InferenceAdapter 接口只描述模型/张量/取消/输出，留给 B 更换实现；不在 A 实现 DirectML、不做大范围框架抽象。

B 将该具体推理适配器接入共享原生 `SVSCompute` 客户端运行库和 `SVSComputeWorker` 原生推理辅助程序，并提供 SDK 的独立 `svs_compute.h`、C++ RAII 包装与示例。DiffSinger 与其他采用接口的 AI 插件调用共享运行库；宿主负责设置与调度，辅助进程负责 CPU/DML session 创建、模型推理和资源释放。CPU 与各 DML 设备使用独立 worker，避免参考已记录的同进程 DML 失败后切 CPU 的 AccessViolation 风险。该隔离只用于本计划 AI 推理，不扩展为通用插件宿主。ONNX Runtime 对象、COM 指针、Qt/STL 对象不能跨 SDK C 边界。共享库 C ABI 单独版本化，不改变旧 SVS ABI 必需前缀。

开发构建沿用 `build`，主程序沿用 `build/Release/lmms.exe`，普通插件及支持库沿用 `build/Release/plugins`。新的 SVS 引擎输出为 `build/Release/svs/SVSDiffSinger`；这是新增引擎包，不是另建替代安装树。B 共享运行库/ORT/DML 依赖放同一既有 Release 根的约定位置并显式纳入安装与清单；不再向其他构建目录部署第二套 DLL。

## 第一部分 A：原生 DiffSinger 引擎实现

### A.1 声库发现与身份

**DiffSinger 引擎设置严格只有两项：渲染步数、声库目录（多个）。** 声库目录使用可添加/移除路径的列表控件，“重新扫描”是操作按钮，不是第三项配置；两项值通过 engineSettings 声明、验证并持久化到 LMMS 用户配置，不写入声库。SDK补充目录列表的展示/值编码约定，保持与旧参数声明兼容，不能用单个路径字符串假装多目录支持。默认可扫描用户声库目录；`refs/Singers` 只通过本机测试配置加入。引擎没有声库时仍显示 `DiffSinger (AI)` 设置页和诊断，不能注册假声库来绕过空 catalog。

递归遍历所有配置根，按 canonical path 去重根和包，排序稳定；防止目录 junction/symlink 循环、重复重叠根，无法读取单个目录只诊断并继续。发现符合模型配置的声库根，不以目录显示名、头像或 `character.txt` 单独判定；扫描到根后仍识别真正嵌套声库，但 `dsdur`、`dspitch`、`dsvariance`、`dsvocoder`、共享 Vocoders 内的配置不能误注册为声库。没有 acoustic 的 duration-only 配置不是可唱声库。

ID 与显示名、扫描顺序、绝对路径和头像无关：优先显式包 ID；无 ID 时分配并持久化宿主安装 ID，登记规范化包位置及内容指纹，用于重扫/移动时匹配。完全相同副本只保留一个；同 ID 不同内容明确冲突、按配置根优先级选择并列出被遮蔽者；无法无歧义匹配移动包时要求重新绑定，不擅自绑定同名声库。说话人 ID 来自包内稳定键，与显示名分开。

catalog 发布不可变快照。重新扫描完成后原子替换，对受影响引擎更新选择器/能力声明；正在渲染的旧句保留其资源引用，已移除的声库进入缺失状态且保留工程数据。新增可选 `query_catalog(engine, context_json, &json)` 承载扫描根、rescan 请求和目录版本；旧 `catalog` 仍返回当前目录。按 size/feature 协商追加接口和 SDK 包装/conformance。Registry 仅做必要的“已安装引擎描述与可选声库列表分离、显式刷新”接入，不改其他插件发现系统。

### A.2 元数据、JSON/YAML/TXT 配置

每个包读取并逐字段规范化，保留原始来源、未知字段和诊断；不把所有文件作为一种可任意覆盖的字典合并。

| 数据 | 解析与用途 |
| --- | --- |
| 根 `dsconfig.yaml` / `.yml`、明确等价的 JSON 配置 | acoustic、phonemes、languages、vocoder、speakers、采样/声学参数与 use_* 能力；根据实际键验证，不按扩展名猜类型 |
| `character.yaml` / `.yml`、明确结构的 character.json | name、作者、描述、版本、语言、头像/立绘、默认音素器、subbanks 等元数据 |
| `character.txt` | 按真实包编码和 key=value 语法读取，补 name/image/author 等缺省字段，保留值中的等号；UTF-8/BOM 优先，传统编码必须显式识别或配置并记录 |
| `phonemes.json`、`languages.json`、字典文本/YAML | 音素/语言 ID 映射及实际发音词典，不混入角色元数据，不擅自重排模型 ID |
| `dsdur`、`dspitch`、`dsvariance` 内 dsconfig | 各阶段模型、词典、speakers 与专属开关，不能误用根配置代替 |
| `dsvocoder/vocoder.yaml`、共享 Vocoders 配置 | 声码器模型、mel 规格、sample rate、hop、pitch_controllable、force_on_cpu |
| `comfort.json` | 用于声明可用音域、舒适音域、弱点音域；接入 SVS 编辑器琴键，以不同明暗度标注这三类范围，钢琴窗侧栏同时用文字显示对应音域，二者使用同一份声明数据。没有该文件保持默认琴键显示，不添加音域提示、不推测音域；其他未识别扩展报告而不误作引擎主配置 |

用户补充（2026-10-08）：音域呈现属于所有 SVS 引擎通用的宿主能力，以可选声库 metadata.pitchRanges 声明 available/comfort/weak 标准音名及范围。DiffSinger 将 comfort.json 转成该声明，其他引擎可通过同一 catalog 元数据提供；SVS 全局增加“显示声库音域”开关，默认开启，统一控制琴键明暗及侧栏文字。没有声明或关闭时保持默认显示，开关不影响实际推理/缓存。无法解析的非标准片段保留原文字并诊断，不推测数字 7 的含义；用户后续重新生成 JSON。A4 验证通用声明、开关持久化及即时显隐、缺失声明默认行为和真实窗口截图。

字段合并优先级：明确宿主用户覆盖 > 相应角色 JSON 配置 > YAML/YML > TXT > 合理缺省；同角色同时存在 JSON/YAML 时按此稳定选择并诊断冲突。阶段推理配置按其专属目录覆盖该阶段字段；不能用角色显示元数据覆盖模型张量约束。**一个 YAML 文件存在不代表 TXT 无效**：例如 YAML 仅声明立绘、subbanks、默认音素器，TXT 仍提供 name/image/author。别名 image/avatar、portrait、opacity 按明确映射读取。

配置模型：VoicePackage、CharacterMetadata、LanguageInventory、Speaker/Subbank、StageConfig、VocoderConfig、ResourceMap、ModelFingerprint，分别可验证。模型/字典路径相对其声明配置文件解析；限定在已配置授权包和显式共享模型根，不扫描磁盘寻找缺失模型、不执行配置脚本。YAML 使用安全数据解析，限制递归/别名/文件大小；解析失败保留可读错误而不随机默认出“可用”状态。

声码器优先解析包内声明/`dsvocoder`，其次从同一组用户声库目录中的 `Vocoders` 共享资源位置解析；同名冲突按目录列表优先级报告。不新增独立声码器目录设置，不依赖 TuneLab/OpenUtau 安装目录。校验 acoustic/vocoder 的 sample rate、hop、mel bins、FFT/window、fmin/fmax、mel base/scale 兼容性，必需字段错误不合成。

头像/立绘用原生资源 API 提供包内 ID、MIME、大小、SHA256。最小修正 SVS 图像调用链，使“外部声库合法资源”走 Plugin::resource，而不是在插件目录下拼路径；已有包内图片和用户主动选择的测试图片行为保持。透明度、显隐、缩放和鼠标穿透继续使用现有 SVS 视图；轨道名跟随说话人名字，轨道不新增说话人选择控件。

### A.3 发音、音素时间、说话人

实施补充（用户要求，2026-10-08）：字典解析参考 `tlds_ref` 的 `DiffSingerPredictor`；可选语言直接依据声库能力声明生成，不能固定为中文列表，也不能因 `use_lang_id=false` 删除多语言。保留模型各阶段独立的语言/音素 ID，按参考字典优先级处理 entries、symbols、replacements；语言选项和缺失自动解析能力的诊断分别提供。

覆盖六个真实包涉及的语言/音素器家族，按包默认音素器和字典实现原生适配；注册表将已知音素器名称映射到原生实现，不加载 C# DLL。不认识的默认音素器明确说明，不能偷偷用 la 代替成功。手工音素/手工读音可作为显式替代入口，但不能用它们取代六包自动歌词验收。

保留宿主优先级：手工 segments > 手工 reading > 工程词典 > 声库词典 > 默认分析；处理延续音符、休止、跨音符连读、OOV、前置辅音、语言 ID 和词典不同音素集合。未知词保留歌词和原数据，显示诊断/明确回退。自动结果只回显，不写成用户 segments；重置手工覆盖恢复自动。空 segments 与不存在字段区分，禁止伸缩再次插入 null。

duration 预测将 ph_seq、note 约束等转为模型要求输入，产生可编辑起点/时长；手工钉定段必须影响实际推理，不能只影响画面。前置/刚性辅音、相邻音符去重叠、最小时长与最大提前量使用既有 SVS 能力和共享伸缩操作，生成反馈依 note ID 归属。TuneLab 的无答案与确定零音素状态在现有 feedback 结构中明确区分；不以“删所有音素”充当正常无发声。

包级多个 speakers/subbanks 统一提供插件设置中的稳定说话人枚举与音色条件；嵌入文件、范围匹配、权重归一化和 hidden_size 验证。说话人切换影响缓存/能力/PCM，用户选择和工程字段持久化；不将同一模型的所有嵌入重复加载为多套模型。

### A.4 完整推理链和时间协议

实现并验证：**歌词分析 → duration/音素布局 → pitch predictor → variance predictor → acoustic → vocoder → 重采样/立体声 PCM + feedback**。可选阶段按包配置和 ONNX 实际输入输出判断，不能凭文件夹存在开启；无预测器时使用显式合法基线，无必需 acoustic/vocoder 时拒绝合成。

每阶段按模型实际 tensor 名称、dtype、rank、shape、符号维验证，不给所有导出版本套一组硬编码输入。覆盖参考中实际存在的 linguistic+dur/pitch/variance、fs2/aux/denoiser、连续 steps/depth 与离散 speedup/maxDepth 等导出路径，以及 .onnx external data；每个角色使用自己的词表/语言表和嵌入，不复用 acoustic token ID。linguistic 的 word_div/word_dur 与 ph_dur 模式、dur 的 ph_dur_pred 输出按模型识别。参考配置与模型签名不一致报出 stage/model/inputName。speaker/language 条件、rest mask、slur、帧长必须与模型一致。

内部以模型帧轴 `sampleRate / hopSize` 建立 FramePlan，统一将冻结 tempoMap 的秒映射到帧，尾端累计误差按明确规则修正，不独立四舍五入导致音素长度总和漂移。长句按休止/模型上下文分块，保留跨块连续曲线、前后 padding 和重叠拼接；只实现真实模型所需分块，不另建范围调度系统。

pitch 使用 fractional MIDI 与 Hz 显式转换，模型自动基线与用户覆盖/偏差正确组合；沿用 svs_time.hpp/svs_curve.hpp，不另写宿主曲线插值。variance 的绝对实参覆盖区使用用户终值；自由区使用预测基线加声明的偏差，再按声学单位 clamp，voicing 的线域/dB codec 显式转换。宿主已组合全局基础偏移的有效值不得再次叠加；每个实际声明参数明确是终值还是偏差，不凭同名曲线猜测。自动 pitch/variance 与用户曲线分开，参考模型支持的 energy、breathiness、voicing、tension、key_shift/gender、velocity/speed、expressiveness、speaker mixing 等按能力动态声明。声学不接受的量不展示为可调输入；预测输出可提供各自有值、独立显隐的只读参考曲线。

引擎设置中的真实渲染步数为1–100、默认20；具体模型支持连续steps或离散speedup时说明映射及有效值，冻结并参与身份，不沿用纯AI示例页值。depth/sampling使用声库配置的合法默认，不增加额外引擎设置。随机推理使用稳定seed并进入输入/缓存，既有工程含seed时保留，没有时采用固定版本化默认；seed 不新增引擎设置项。

用户补充（2026-10-08）：DiffSinger 插件全局设置增加“显示音素语言前缀”布尔开关，默认开启。只控制音素标签是否显示已识别的语言前缀（例如 zh/i），不改实际音素、手工覆盖、推理输入和音频缓存身份。A4 验收开关切换显示效果、设置保存重开，以及切换时已有音频保持有效。

用户补充（2026-10-08）：音符默认在上方显示拼音/对应读音，内部显示原歌词；读音来自已解析的发音结果，不能用原歌词重复冒充拼音，不改写歌词数据。A4 使用实际中文声库的歌词与拼音实窗截图验收，缺少读音时保留歌词并显示既有发音诊断。

模型原生 mono 按既有协议转换为 float32 stereo，重采样到 snapshot.sample_rate；结果 start_seconds 是全局秒，pitch/phoneme feedback 是内容局部秒/曲线局部 tick，正确处理 clip position/contentOffset、负提前量、tempo 变更。检验 finite、帧数和现有 16M 帧结果上限；超长内容在插件内部受限分块或明确说明超过支持上限，不能增加无限结果内存。

submit 完整拷贝输入，推理只读快照；每阶段和块边界检查取消，可调用 ORT 终止机制但不承诺设备内核立即中断。取消/删除 clip/换声库/编辑后的旧结果不得发布。release_result/destroy_session 必须等已启动计算退出并在所属模块释放缓冲。

### A.5 持久化、缓存、错误与独立运行

实施补充（用户要求，2026-10-08）：音频与 `.tensor` 张量缓存统一使用 LMMS 工作目录下 `cache/SVS/<引擎>`，DiffSinger 固定为 `cache/SVS/DiffSinger`；路径由宿主冻结传入，不能另设张量缓存根。目录调整不改变内容身份，清理保持有界，只处理各自拥有的缓存条目，不删除整个用户缓存目录。

命名与试听补充（用户要求，2026-10-08）：最终音频文件名为完整音频文件字节的 64 位 SHA-256 字符串加扩展名；输入身份/反馈索引单独保存。tensor 文件名同样使用完整 SHA-256，覆盖模型/config/runtime/seed 与输入 tensor 名称、类型、形状和全部字节，避免重名；读取校验损坏并按 miss 恢复。真实音频测试保留试听文件供用户随时播放，不提交私人声库/音频到仓库。

交付补充（用户要求，2026-10-08）：DiffSinger 成品为完整 `SVSDiffSinger` 文件夹，保留引擎、额外依赖 DLL、manifest、发音数据及许可证相对结构；不能以单个插件 DLL 作为完整成品。安装及独立运行验收核对整个文件夹。

最终部署补充（用户要求，2026-10-08）：A0～A4 全部完成后的最终构建统一写入已有 `build/Release`，主程序 `build/Release/lmms.exe`、普通插件 `build/Release/plugins`、DiffSinger 包 `build/Release/svs/SVSDiffSinger`；最终加载测试使用这套真实开发版 Release，核对已启用插件 DLL 原位更新，不能以独立 SDK/测试目录的二进制代替最终加载验收。

工程沿用 SVS XML，保存稳定 voice/speaker ID、模型相关参数、歌词、手工音素/音高/曲线、seed，未知字段原样保留。机器相关扫描绝对路径留用户配置；工程保存可移植 ID/内容指纹与缺失诊断，不写死本机 D: 路径。

缓存键包含 voice ID/version、完整模型/外部权重/词典/speaker/config 指纹、算法版本、采样率、tempo 与全部输入。不能只靠插件 DLL hash 或目录 mtime 判断外部模型更新。通过冻结 voiceVersion/新增 identity 元数据把内容指纹传给既有 SVSCache；重新扫描变化只取消/失效受影响 clips。头像/立绘显示设置不触发重新合成。

错误保留 stage、voiceId、model、noteId、parameterId、rangeId 与原始 ORT 诊断，既有错误栏显示可读主因，详细信息进入日志；不得将 OOV、缺模型、非法张量全变成泛化 Failed (2)。缺失声库保持工程可编辑和符合身份校验的缓存播放，不合成修改后的内容。

### A.6 里程碑与阶段验收

| 顺序 | 交付 | 必需自动验收与完成条件 |
| --- | --- | --- |
| A0 | 固定六包扫描/配置/模型签名矩阵，选定 native ORT、JSON/YAML、音素化依赖和许可；新增插件骨架 | 独立 SDK 构建/协商通过；旧示例可用；真正缺少依赖清晰报错；冻结依赖版本和清单 |
| A1 | 递归扫描、字段合并、目录刷新、ID、资源图片和引擎空目录状态 | 六包实际嵌套根全找到；predictor/vocoder零误报；仅渲染步数和多个声库目录两项引擎设置、目录添加/移除/持久化通过；重叠根/重扫去重、冲突、Unicode、YAML+TXT缺省补齐、JSON映射、权限/循环/坏配置fixture通过；刷新不丢用户数据 |
| A2 | 原生歌词/语言/词典、duration、手工音素与说话人 | 六包对应音素器歌词输入、OOV/延续/休止、手工重置、首尾伸缩、最小时长、说话人持久化测试；自动结果不污染 segments；真实 duration ONNX 推理通过 |
| A3 | CPU pitch/variance/acoustic/vocoder 全链、曲线与音频反馈 | 六包各用有效歌词 CPU 生成有限非静音 PCM，模型全部阶段/签名校验；有值音高/结果曲线，手工音素/音高/可调量改变输出；tempo/position/offset、分块、取消、缓存、导出通过 |
| A4 | 原部署安装、SDK 文档/示例、Release 独立性 | 完整既有 SVS 回归与新增回归；开发主题原生实窗完成选择、设置、合成、播放/导出、保存重开；不带 refs/.NET/Python/本机图片/声库亦能启动，用户配置外部声库后可唱；CPU 内容变化导致正确缓存失效；有 comfort.json 时按真实声明以不同明暗度琴键显示可用/舒适/弱点音域，切换声库同步更新；无此文件时默认琴键显示不变，并留存真实窗口截图 |

A3 六包矩阵包括每个实际 ONNX 模型族和可选阶段组合，不能只验一个包后声称其余可用。少量人工听感项目可 MANUAL/PENDING；真实 CPU 推理和六包适配是必需自动验收，不能以“人工待测”越过进入 B。

## 第二部分 B：AI 声库全局 DirectML 加速后端

### B.1 全局策略与兼容性

沿用 SVS 设置 `computeBackend/computeDevice`，CPU 默认并显示 CPU，不展开 CPU 型号。DirectML 成为实际后端；设备列表来自系统探测并验证 D3D12/DirectML/ORT session 能力，软件适配器过滤。没有 GPU/缺依赖时 DirectML 不可用并有原因；不能把普通 DXGI 列表当作“支持推理”的证明。

保存设备 LUID 与识别元数据，不保存 UI 索引。每次初始化用 LUID 重新解析真实 adapter；可使用同一 adapter 创建 DML device/queue，或严格解析为对应枚举 index，禁止参考中的固定 DML(0)。设备消失/驱动变更后保留用户期望选择，实际回退 CPU并提示；用户取消设置不更改合成。

CPU 与 DirectML 是本计划真正实现的两个后端。所有符合接口的 AI 引擎收到全局冻结策略；传统/非 AI 引擎仍收 CPU。引擎单独声明 supportedBackends、shared-runtime 协议版本、stage 约束和原因；未知能力默认 CPU。引擎参数页仍按名字分栏并显示 `(AI)` / `(传统拼接)`，不把全局后端复制为每个声库单独设置。

策略结果至少记录 requestedBackend、requestedDevice、effectiveBackend、effectiveDevice、policyRevision、runtimeVersion、模型阶段 overrides 和 fallbackReason；支持者的 submit 收到同一策略快照，不在 render 中读取会变化的 ConfigManager。

### B.2 共享计算 SDK 与推理实现

新增独立 `svs_compute.h`：size/version/feature 协商、固定宽度数据、UTF-8 描述、opaque model/session/run/result handle。最小函数族：query_backend_devices/probe、create_model/session、query_model_signature、run、cancel、release_result/destroy_session/model、release_string。tensor 描述提供 name、dtype、rank、dims、byte_count、data；输入借用仅在同步 run 期间有效，输出由产生的运行库释放。明确 float32/int64/bool 等实际模型类型，不透传 ORT C++对象。

model descriptor 来自引擎已验证的配置根/相对模型和外部权重；注册规范化授权根，不沿 SDK resource API 接受任意路径，不从目录遍历逃逸。运行库检查维度乘积、数据大小、文件存在和 tensor 签名。CPU 与 DML 对调用者使用同一接口；非 Windows CPU 正常编译，DirectML 查询返回不可用，不在通用代码硬链接 D3D。

公共 SDK 协议文档补充引擎 backend 声明、policy JSON 和实际执行反馈；继续 ABI 协商，不能仅加常量就宣称 SDK 支持。共享运行库加载及其 ORT/DML 版本唯一、版本不匹配早报错；旧引擎仍可用自身 CPU，不能因可选库缺失导致整个 LMMS 启动失败。

ORT/DML 的官方限制必须在 worker 实现：DML session 禁用 memory pattern，使用 ORT_SEQUENTIAL，同一 session 的 Run 串行；DML device/queue 属于同一个 D3D12Device。不同 session 并发由受限预算管理，首版不需要增加既有 SVS 宿主插件并发。依据：[DirectML EP 官方说明](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)、[DML provider API](https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/providers/dml/dml_provider_factory.h)。

客户端与 worker 采用有界原生 IPC：协议版本、request ID、worker epoch、模型身份、tensor dtype/dims/byte count 必须校验；大张量使用受限共享缓冲，客户端与worker均有明确释放责任。启动进程是无GUI原生helper，不启动.NET/Python；断连/退出/超时明确返回失败，作废该epoch模型句柄，回收共享内存，丢弃晚到结果。仅固定CPU worker和实际选中DML设备worker，不生成无限进程；主程序退出有界取消和清理。独立SDK消费者可通过明确运行时目录定位helper，不依赖LMMS内部对象。

依赖采用经 A/B0 冻结且实测的 native ORT DML 构建，带 CPU EP，同时分发它要求的 DirectML/runtime 依赖和许可；不能只复制 onnxruntime.dll。参考版本 1.23.0/DirectML 1.15.4 只作为调查起点，不把不同版本 DLL 混配。具体 opset/算子支持按选定构建和实际模型 probe 冻结，不能用“是 ONNX”推导可 GPU。

### B.3 阶段路由、回退、取消与资源

DiffSinger 的 duration/pitch/variance/acoustic/vocoder 和神经 G2P（如实际需要）均通过统一 adapter。vocoder 的 force_on_cpu 等真实约束必须优先：它只将受约束阶段路由至 CPU，其余阶段可 DirectML；UI/日志显示各阶段实际设备，不能假称全链 GPU。

显式 CPU session 与 DML session 在独立worker创建和缓存，锁与 device/queue 生命周期清楚；CPU worker仅启用CPU EP。unsupported graph/init/device removal/OOM 等可识别后端错误允许当前请求整阶段转交CPU worker一次，记录原因；DML worker原生崩溃通过进程退出/断连检测，不能用C++异常捕获假称可恢复。回退不在已污染的DML进程创建CPU session。张量/模型/数据错误不靠重复 CPU 推理掩盖。取消不启动回退。

区分 ORT 内部 CPU 节点分区与应用整阶段回退：保留 provider assignment/profile 的诊断证据，只有确实有 DML 节点执行的模型才算加速已启用。回退结果附 effective stage route 与原因；按有效路由存储身份，不能存入仅声明 DML 成功的缓存键。下一次正常请求可以重新探测，不把一次失败永久伪装成 GPU 或永久禁用。

worker以 model/config fingerprint + runtime/backend/device + shape/options 建 session 缓存；不每帧创建 session。固定有限并发/有界模型缓存，记录 CPU/GPU预算及卸载时机；动态句长只做必要 shape 策略，不做计划外自动调优。共享客户端运行库在最后一个 model/session/run 退出后释放，插件/LMMS关闭等待取消清理并回收自己启动的worker，不在推理中卸载DLL。

### B.4 宿主设置、调度、缓存和验证

后端 Apply 后递增 policy revision，取消正在运行的受影响 AI clips，失效并请求新合成；排队的请求仍使用自己的快照，旧结果经既有门禁丢弃。预览、歌曲播放准备、离线导出、缓存恢复均走同一策略，不出现 UI DML、导出 CPU 而未说明的状态。一般音频 callback 不调用 ORT/设备探测/创建模型。

在 cache lookup **之前**冻结可解析的有效后端/设备与版本；运行期间回退改变路由则结果携带实际身份，重新确定存储键。定义 requested/effective 与 CPU-only阶段对身份的影响，连同模型/词典/算法/seed 加入结果键；缺失插件缓存兼容规则仍保留，展示属性不影响。不得删除整个用户缓存来掩盖错误。

验证 GPU 执行必须同时有：真实模型 PCM、有效 LUID/后端信息、DML EP 节点/运行记录，且 CPU和DML使用同歌词/音符/曲线/seed。比较 frame_count/时间原点、finite、音素布局、pitch/参数反馈和数值差异；在 B0 固定按模型/阶段适用的容差（不可事后放宽为刚好通过），随机输出不要求逐字节相同。

性能记录冷启动/热句耗时、关键阶段耗时、峰值资源和 provider 路由；在本机可用 GPU 上至少一个真实 acoustic 推理链必须证明 DML 节点执行并完成 CPU/DML对比。加速幅度记录实际结果，不承诺所有模型/设备都更快；不以 UI 文案或设备任务管理器曲线代替节点证据。

### B.5 里程碑与阶段验收

| 顺序 | 交付 | 必需自动验收与完成条件 |
| --- | --- | --- |
| B0 | 冻结共享计算 C ABI、依赖版本、全局策略、实际设备 probe、六包 GPU 兼容/CPU约束矩阵和容差 | 明确可用/不可用依据、CPU-only阶段、unsupported模型及回退规则；旧 SDK/plugin兼容设计检查 |
| B1 | SVSCompute客户端、独立CPU/DML worker、SDK包装、最小第二 AI 插件 | CPU无GPU可运行；真实LUID选择、DML options、tensor/ownership/取消/缺库异常；IPC断连/worker崩溃不带崩LMMS、句柄epoch和清理验证；两个引擎消费同一接口，旧示例/旧ABI兼容 |
| B2 | 宿主全局设置和所有提交入口接入 | CPU禁用设备框；DML只选probe成功设备；Apply/Cancel/重启、后端变化失效、旧结果门禁、真实设备消失、传统插件不受影响；快照不再硬编码CPU |
| B3 | DiffSinger全阶段迁移、CPU约束/回退、正确缓存 | 六包逐阶段矩阵验证；真实DML节点证据与CPU对比；vocoder强制CPU、OOM/unsupported/init失败回退最多一次；取消不回退，错数据明确失败；同设备session并发/释放有界 |
| B4 | 完整回归、部署、独立Release/SDK | 原路径DLL/清单哈希；SDK外部最小消费方不依赖LMMS/Qt/refs；Release配置外部声库可CPU/DML运行；默认主题实窗设置/编辑/合成/保存重開/导出；原有SVS回归无回退 |

机器不具备第二 GPU时该项 MANUAL/PENDING，多GPU测试不阻塞其余开发。没有任何可用DML设备时可继续实现/CPU/failure-path验证，但 B3/B4 的“真实 GPU 执行”保持 BLOCKED/PENDING，**不得报告全局加速完成**；需要真实可用设备的证据后才能最终完成。人工听感/操作手感可以独立 MANUAL/PENDING，不代替必需自动推理验证。

## 4. 实施规则、验证命令和交付

所有构建/测试/打包均在前台 PowerShell，先 dot-source `buildtools/Enter-LmmsEnvironment.ps1`，从 PATH 调用 cmake/ctest，使用共享 `$env:QTDIR`、`$env:SVSSDK_ROOT`、toolchain/generator/platform。每条命令使用 `2>&1 | Tee-Object -FilePath build.log`，立即保存并检查 LASTEXITCODE，失败读取日志；新 ONNX SDK变量由环境脚本统一设置，供已启动的agent刷新，不到处写本机路径。

示例仅表示新增目标后的标准执行方式；A0建立目标时固定最终名字，不另建目录：

```powershell
. ./buildtools/Enter-LmmsEnvironment.ps1
& cmake --build build --config Release --target lmms SVSDiffSinger SVSIntegrationTest --parallel 8 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log -Tail 80; exit $buildExitCode }
```

ctest/native ONNX smoke/SDK conformance/打包命令分别执行相同管线；B增加实际共享计算目标。必要configure仍用原build及环境变量；日志每次覆盖前归档到相应阶段证据文件。先检查进程占用，在同一目录更新DLL，禁止另起目录绕过；禁用目标的旧DLL原位停用且记录。

GUI只使用原生Windows Qt：构建→实际开发版窗口→稳定渲染→必要操作→实窗截图检查字体/QSS→记录→关闭。禁止offscreen和仅widget截图宣称UI正确。本计划编制不需要Computer Use；后续只有明确操作语义比较才使用，并结束后关闭窗口。测试仅开发版，不开启原安装版。UI复用Qt Widgets+既有Theme CSS/QSS，不添加新皮肤系统。

每一里程碑满足预定义自动验收后立即：更新本计划状态和证据→检查git status/diff→仅提交该阶段文件→push当前分支→确认远端成功→开始下一阶段。禁止把多个完成里程碑堆到最终提交。必要不阻塞人工验收写MANUAL/PENDING，不因人工项目无限扩大自动矩阵。

交付包括：原生DiffSinger插件包、共享计算运行库及依赖（B）、独立SDK接口/例子/文档/conformance、voice配置/支持范围说明、CPU和DML smoke记录、真实声库矩阵、模型/依赖许可清单、安装manifest及ZIP/hash验证。打包显式核对新引擎DLL和所有依赖，不假设顶层install_manifest自动包含嵌套目标。验证真正安装内容而非另一套test DLL。

参考代码两个LICENSE.txt为MIT，移植保留声明；额外音素化/ORT/YAML/DirectML等按实际采用组件逐项核对许可。声库README的作者来源说明不等于再分发授权；任何六包实际资源均不进入交付。Release脱离refs、本机测试图、TuneLab/.NET/Python安装仍能启动；外部声库由用户配置后完成真实推理。

## 5. 完成定义与进度

只有 A0→A4、B0→B4 顺序验收并分别提交推送，六包CPU真实推理与递归配置适配通过，至少一台可用设备真实DML推理证据通过、全局策略被第二AI插件消费、传统引擎保持行为、持久化/缓存/取消/独立SDK/Release均验证后，实施任务才完成。必要GPU/模型自动验收不能降格为MANUAL替代。人工听感/布局待验收允许单独标记。完成后停止，不实施计划外重构。

当前 A0 原生骨架、依赖冻结、独立 SDK/旧示例协商和六包 48 模型 CPU session/实际签名检查通过。详细记录见 [A0 验收](doc/svs/DiffSinger-A0-validation.md) 和 [六包矩阵](doc/svs/DiffSinger-six-package-matrix.json)。A1 原生递归声库目录、元数据、身份、资源和设置实窗验收已通过，见 [A1 验收](doc/svs/DiffSinger-A1-validation.md)。A2 六包原生发音和真实 duration 推理已通过，见 [A2 验收](doc/svs/DiffSinger-A2-validation.md)。A3 六包真实 CPU 全链、反馈、统一 SHA 缓存、分块/tempo/导出及原生只读曲线实窗验收已通过，见 [A3 验收](doc/svs/DiffSinger-A3-validation.md)。目录与 duration 验收不能替代全链验收。每阶段提交推送成功才进入下一阶段。

| 里程碑 | 状态 | 自动证据 | 人工事项 |
| --- | --- | --- | --- |
| A0 | PASS | 独立 SDK/原位开发 DLL ABI 1.0/1.1/1.2；旧 full/minimal 示例；缺依赖拒绝；六包 48 ONNX CPU session/签名 | 无本阶段人工项 |
| A1 | PASS | 六包扫描/配置/身份/权限循环fixture；独立SDK/ABI1.0–1.3/旧示例；两项设置与目录持久化/刷新/资源实窗 | 无本阶段人工项 |
| A2 | PASS | 六包原生歌词/真实12个 duration ONNX；词典/延续/休止/覆盖重置/最小时长/取消；说话人嵌入fixture和工程持久化；独立SDK/原位DLL/开发主题实窗 | 无本阶段人工项 |
| A3 | DONE | 六包 CPU 全链、稳定 seed、SHA 音频/tensor 缓存、受限分块、tempo、导出/重开、只读参考曲线实窗通过；本阶段提交推送后进入 A4 | 已保留六份试听；主观听感 MANUAL/PENDING |
| A4 | DONE | 既有 Release/58 个插件原位部署、独立 SDK/旧示例、完整 SVS 68/0、原生 GUI 5/0、实际 Release 与空声库启动 3/0；通用音域/背景波形、前缀/拼音/颜色/加粗波形通过，见 A4 验收 | 主观听感 MANUAL/PENDING |
| B0 | PASS | 共享 C ABI/策略/依赖/容差冻结；两块真实 LUID 的 DML Add 节点；六包 48/48 DML session；纯 C 头与缺设备负向通过，见 B0 验收 | 实际模型 DML 推理在 B3 验收 |
| B1 | PASS | 原生共享客户端/独立 worker/SDK RAII；六包 CPU 全链、第二 AI 插件、两 GPU LUID/DML 节点、取消/缺库/崩溃 epoch、旧 ABI 通过，见 B1 验收 | 非 Windows 运行未在本机验证 |
| B2 | PASS | 全局策略/有效缓存身份；第二 AI 实际 DML clip/导出、Apply/Cancel/重启、缺设备/旧结果门禁和传统回归；原生主题设置实窗通过，见 B2 验收 | — |
| B3 | PASS | 六包 48 模型 DML/CPU 数值与反馈对照、六包 CPU 回归、两 GPU 实际链、CPU 约束/一次回退/取消/并发预算、有效阶段缓存通过，见 B3 验收 | 听感 MANUAL/PENDING |
| B4 | PASS | 独立 SDK/共享运行库安装、完整回归与 Release 实窗验收通过；包含用户追加的通用模型内存管理，见 B4 验收 | 听感/体验 MANUAL/PENDING |

计划编制交付检查已在 `05cd61c5f` 完成。本轮授权仅顺序实施 A0～A4，各阶段分别提交推送；B 保持未开始。

A4 用户补充验收：Song Editor 中 SVS 使用默认蓝色片段，沿用主题和已有自定义轨道/片段颜色机制。
A4 用户补充验收：音符下方波形加粗；SVS 全局可开启 Song Editor 多轨片段半透明背景波形（非钢琴窗背景），默认关闭，适用于所有引擎，显示切换不改音频或缓存。

A4 完成证据见 [A4 验收](doc/svs/DiffSinger-A4-validation.md)。本轮 A0～A4 的 CPU 引擎授权范围完成；B 仍未开始。用户在 A4 收尾时追加的“按空拍分段增量渲染及 Song Editor 当前段/总段进度”在本检查点之后单独实施和提交。

## 用户追加：空拍分段增量渲染（A4 检查点之后）

状态：DONE。授权来源为用户明确要求“实现分段渲染+改参不影响整个svs片段，分段渲染可以在song editor里面显示渲染进度”，并指定“当前段/总段，按照空拍处分段”。A0～A4 已独立提交推送；本增量另行验证、提交推送，不实施 B。

范围仅为 SVS 分段计划/合成调度/缓存/有效音频保留，以及 Song Editor 片段进度显示。按音符之间的空白/显式休止分段；连续音符保持同一段。通过可选引擎能力声明使用分段，旧引擎保持原行为。DiffSinger 接入。各段使用既有 SHA256 WAV/.svsmeta/tensor 缓存路径和冻结输入；局部音符/曲线变化只失效实际受影响的段，声库/模型/全局参数/tempo 等共同依赖变化失效全部受影响段。未受影响段的有效音频在其他段重渲染期间保持可用；无效/失败段不播放旧音频。暂停/取消、旧请求结果丢弃和错误诊断沿用既有机制。

验收：三段真实 CPU 生成；修改其中一段音符及局部曲线，证明另外两段缓存复用/PCM 不变且重渲染中仍可用；全局参数正确失效全部；空拍新增/删除导致正确合并/拆分；进度为当前段/总段并在 Song Editor 实窗截图中可见；tempo/position/offset、取消/失败、导出及缓存恢复回归；重编既有 Release 并加载。独立 SDK 文档说明可选能力，无 ABI 破坏。完成后另行提交推送，不扩张其他子系统。

用户追加验收：可调参数曲线及标签按参数颜色区分，配对的只读参考曲线保持对应颜色；DiffSinger offset 使用同色系浅色。其他引擎未声明颜色时使用宿主参数配色。完成后同步 SDK 的可选分段能力、参数颜色、缓存/进度/完整导出约定，并通过独立 SDK 构建及旧 ABI 示例验证。

本增量自动验收完成：完整 SVS 70 passed / 0 failed / 2 GUI-only skipped；真实窗口专项 5 passed / 0 failed / 0 skipped；独立 SDK 旧 full/minimal 与 DiffSinger ABI 1.0–1.3 / 六包分段和颜色声明通过。见 doc/svs/SVS-segment-rendering-validation.md。完成本检查点提交推送后，再交付用户追加的英文 README 对比表与安装版全量替换包。

用户追加交付已完成：英文主 README 对比表及 AI 辅助开发/独立分支同步上游说明已在 236c5f3fe 推送，原 README 保留。全量替换包 `build/packages/lmms-enhanced-full-236c5f3fe-win64.zip` 包含完整 DiffSinger 与官方默认四轨模板；ZIP 哈希、360 PE 位数、原开发目录实际覆盖/配置保留、包内运行库实窗启动及实际部署六包 ABI 通过，见 `doc/svs/SVS-replacement-package.md`。个人空白模板已先备份再恢复官方模板，生产初始化代码未修改。B 阶段仍未开始。

## B 阶段实施（2026-10-09 新授权）

用户已明确授权依次实施 B0～B4，每阶段分别提交推送。以上 A 阶段限定语属于历史检查点，不限制本次 B 实施。B0 自动验收通过，详见 [B0 验收](doc/svs/DiffSinger-B0-validation.md)；确认本阶段推送后才开始 B1。B1～B4 尚未完成，不宣称全局 GPU 加速完成。

B1 自动验收已通过，详见 [B1 验收](doc/svs/DiffSinger-B1-validation.md)。本阶段提交并确认推送后才进入 B2；全局设置与真实 acoustic GPU 比较仍待 B2/B3。

B2 必需自动验收已通过，详见 [B2 验收](doc/svs/DiffSinger-B2-validation.md)。本阶段提交并确认推送后进入 B3；真实声库 GPU 全链比较仍待验收。

B3 必需自动验收已通过，详见 [B3 验收](doc/svs/DiffSinger-B3-validation.md)。本阶段提交并确认推送后进入 B4；独立 SDK/Release 交付与完整回归仍待完成。

B4 用户追加（2026-10-09）：SVS AI 全局选项增加模型驻留策略：渲染完成后立即释放／空闲后自动释放（默认）／保持模型常驻。空闲时间可自定义，默认 60 秒。所有计算后端通用：CPU 释放模型内存，GPU 释放模型显存。修改驻留策略不更改音频身份；渲染租约期间不释放，结束/取消后按策略回收，下一请求自动重新加载；保存/重启及 CPU/DML 生命周期均验证。

B4 必需自动验收已通过，详见 [B4 验收](doc/svs/DiffSinger-B4-validation.md)：完整 SVS 88 passed / 0 failed；10 个有前提用例另行执行通过。最终 GPU/Release 实窗专项 6 passed / 0 failed，旧 GUI fixture 修复复验 4 passed / 0 failed。CPU 与两 GPU 三种驻留策略、公开 SDK 消费方、SDK 源码 ZIP 独立重建、包内真实推理及原位 DLL/替换包哈希均通过。主观听感/操作体验仍为非阻塞 MANUAL/PENDING。

| 检查点 | 状态/提交 |
| --- | --- |
| B0 | 已提交推送 `a4f948e7a` |
| B1 | 已提交推送 `d65e271ac` |
| B2 | 已提交推送 `3bb156fee` |
| B3 | 已提交推送 `d37677308` |
| B4 | 已提交推送 `027ea2e8e` |

用户后续交付要求：B4 检查点完成后，删除 DAW 部署中的 SVS 示例 DLL，仅保留可用引擎及计算依赖；参考代码保留。生成基于已有全量替换包的增量包，安装器同步清理对应示例 DLL/扫描 manifest，另行记录并提交推送交付检查点。

后续交付验收：示例二进制及扫描 manifest 已移出 DAW，参考代码保留。基于 `lmms-enhanced-full-236c5f3fe-win64.zip` 的最终增量包、旧基包兼容性、原位安装/安装后哈希、正式引擎误清理拒绝、生产 Release 实窗通过，详见 [生产增量交付记录](doc/svs/SVS-production-incremental-delivery.md)。本记录所属交付检查点另行提交推送。

## 用户追加：即时张量回收与全局同时渲染线程（2026-10-09）

B0～B4 及示例清理交付已分别推送。用户追加要求：推理结束及时释放临时张量；SVS 全局同时渲染线程默认 1，其余轨道依次排队，可调整上限。模型沿用既有驻留策略，CPU/GPU 通用。实现及针对性验收见 [渲染队列与张量验收](doc/svs/SVS-render-queue-tensor-validation.md)。本增量另行提交推送并更新替换包，不扩张其他模块。
