# 四语补译执行记录

## 冻结范围

仅简体中文 `zh_CN`、日文 `ja`、英文 `en`、韩文 `ko`。使用 LMMS 总体目录 `data/locale`；第三方 GUI、模型名称、歌词、音素数据、用户项目、协议 ID、SDK ABI 不在补译范围。只允许翻译入口、译文、必要资源部署，以及已授权的长文本省略与完整悬浮提示。不进行无关重构。

最新用户要求优先：不查询其他会话；只验证 100% 缩放；阶段代码完成后才做真实 Windows Qt 窗口验证，禁止 offscreen。M0 按计划不构建应用、不做 GUI 验证。M1～M7 严格顺序；M5 各批独立检查、提交、推送。

## 术语

| 英文 source | 简体中文 | 日文 | 韩文 |
| --- | --- | --- | --- |
| Singing Voice Synthesis / SVS | 歌声合成 / SVS | 歌声合成 / SVS | 가창 합성 / SVS |
| Singing Voice Conversion / SVC | 歌声转换 / SVC | 歌声変換 / SVC | 가창 변환 / SVC |
| Voicebank | 声库 | 音源 | 음원 라이브러리 |
| Voice | 歌手（选择歌声时）；声音（一般声学属性时） | 歌手；音声 | 가수；음성 |
| Phoneme | 音素 | 音素 | 음소 |
| Pitch | 音高 | ピッチ | 피치 |
| Clip | 片段 | クリップ | 클립 |
| Track | 轨道 | トラック | 트랙 |
| Render | 渲染（SVS）；转换（SVC 操作语义） | レンダリング；変換 | 렌더링；변환 |
| Synthesis | 合成 | 合成 | 합성 |
| Conversion | 转换 | 変換 | 변환 |
| Speaker | 说话人 | 話者 | 화자 |
| Gain | 增益 | ゲイン | 게인 |
| Breathiness | 气声 | ブレス | 기식 |
| Tension | 张力 | テンション | 긴장도 |
| Gender | 性别 | ジェンダー | 성별 |
| Lyrics | 歌词 | 歌詞 | 가사 |
| Parameter | 参数 | パラメーター | 매개변수 |
| Seed | 种子 | シード | 시드 |
| Re-record pitch | 音高重录 | ピッチ再録音 | 피치 재녹음 |
| Numbered musical notation | 简谱 | 数字譜 | 숫자 악보 |

术语按语义使用，不对用户数据或所有英文子串做机械替换。RVC、DiffSinger、DirectML、LMMS、插件品牌、标准缩写、标准单位、公式、音名可保持原文；整句和通用操作不能仅凭包含品牌就保留英文。HTML、占位符、换行和助记符必须保留其功能。

## M0 当前审计

源码：`95abfc2366412c5c0a2a3949ed65f3b1f129451b`。原始快照和统计继续保留在根目录《词条缺失表.md》，不覆盖。

| 语言 | 缺键 | 空译 | unfinished | 占位符异常 | 英语原文未收录 |
| --- | ---: | ---: | ---: | ---: | ---: |
| zh_CN | 404 | 1105 | 0 | 0 | 不适用 |
| ja | 404 | 1392 | 0 | 3 | 不适用 |
| en | 23 | 0 | 0 | 0 | 381 |
| ko | 404 | 77 | 0 | 0 | 不适用 |

输入 1,130 个 Git 跟踪文件，输出 3,446 个键。缺键、空译和占位符统计与初始快照无变化。本次按每个非空复数分支严格相等判定同原文，得到 405 个键、674 个语言项（中 128、日 363、韩 183）；初始表记录 401 个键（中 126、日 360、韩 179）。差异明确保留，不将新统计冒充原始统计；M6 同时核对初始表和新增项。

`current-audit.json` 按 `context + source + comment + numerus` 保存当前全部键、源码位置、四语状态、唯一补译阶段及插件批次。SVS/音名/简谱归 M2，SVC 归 M3，VST 与其他主程序归 M4，其他原生插件归 M5；入口修改统一在 M1。SetupDialog 的简谱条目按 source 单独归 M2，其他设置仍归 M4。M4 提前处理三个日文占位符异常，M5 不重复修改。

M5 按插件名称排序，将同一插件全部键保持在同批，按最多约 200 键合批；个别独立插件或末批可较小。当前共 12 批，具体逐键归属在审计 JSON 中；批次数及全键数不冒充初始 1,085 个缺口键数。

`review-ledger.json` 保留 1,306 个初始硬编码候选，每项列出源码行、原文、阶段、分类和调查路径。目前候选不能凭字符串规则认定用户可见或仅日志，均先列为“待确认”，M1 沿 CodeGraph 调用路径逐项确认；不默认忽略。另列 25 个已知用户可见固定声明字段，M1 接入、M2 补译。四类结论为用户可见固定文案、动态数据/专有名称、仅日志诊断、待确认；后三类关闭时须有证据与理由，待确认项不能计为完成。

同原文逐语言记录包含完整键、阶段、处置和理由。白名单中的品牌、缩写、单位、音名可保留，其他项要求在所属阶段核对并补译或记录具体保留理由；M6 不允许仍有 OPEN 项。英文 source 回退独立统计，非英文 source 不算英语有效回退。

## 可复现命令

在仓库根目录执行。所有 Qt 工具、审计和测试均使用当前会话前台 PowerShell，完整输出通过 Tee 保存，立即检查退出码；禁止后台作业或额外终端。

```powershell
. ./buildtools/Enter-LmmsEnvironment.ps1
& python buildtools/translation-audit.py inputs 2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
& lupdate -I include/ "@doc/translation/sources.txt" -locations absolute -no-obsolete -ts doc/translation/current.ts 2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
& python buildtools/translation-audit.py audit 2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
& python buildtools/translation-audit.py selfcheck 2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
```

首次工作清单由 `python buildtools/translation-audit.py worklist` 生成。后续不可重复生成覆盖人工复核结论；修改逐项账本时保留已完成项。`sources.txt` 和 `current.ts` 为本机再生成的中间文件，不提交绝对路径。M0 提取完整告警保存在 `M0-extraction.log`；12 个 lacks Q_OBJECT 类与初始表一致，M1 核对实际 context，不为消除告警批量添加 Q_OBJECT。

## 检查点

| 阶段 | 结果 | 自动验证 | GUI | 提交推送 |
| --- | --- | --- | --- | --- |
| M0 | 当前审计、术语、逐键阶段及插件批次、逐项调查账本已建立 | 提取和审计退出码 0；审计边界检查 PASS | 按计划不适用 | 本阶段提交后推送，下一阶段开始前确认远端一致 |
| M1 | 入口统一、英文 source 迁移、四语目录同步完成 | lmms / UiBaselineCapture / SVSIntegrationTest 构建及两组测试 PASS | Windows Qt、100%，四语入口及导出对话框截图 PASS | 本阶段提交推送后确认远端一致 |
| M2 | SVS 307 键补齐，品牌/模板保留已记录 | 四语资源、受影响目标构建、307 键质量检查 PASS | 四语导入、设置、空声库编辑器，Windows 100% PASS | 本阶段提交推送后确认远端一致 |
| M3 | SVC 144 键补齐 | 四语资源、编译、质量检查 PASS | 四语原生重启的空片段/设置场景；实时参数和过载 MANUAL/PENDING | 本阶段提交推送后确认远端一致 |
| M4 | DONE | 主程序/VST 1381 键 | 四语质量、资源与编译 PASS；100% 原生实窗 PASS | 独立提交推送，详见本阶段记录 |
| M5 | DONE | 十二批原生插件补译 | 每批质量/编译和四语原生实窗 PASS；外部依赖明确待验 | 每批独立提交推送 |
| M6 | DONE | 3877 键，候选/声明/同原文复核 CLOSED | 全量 Release、静态与受影响回归 PASS；四语实窗各 3 PASS | 独立提交推送 |
| M7 | DONE | 最终四语场景矩阵和必要省略/全文提示 | 全量 Release、最终测试编译、静态 PASS；四语实窗各组 5/3/4 PASS | 独立提交推送并核对远端；非阻塞人工项仍待验 |

M0 提取曾因输入列表带双引号被 lupdate 当成文件名失败，移除列表内引号后通过；没有变更任何 TS 或业务代码。

## M1 结果

M0 已提交并推送 `422f305ced7f97e4f8448ccad99558107b23bfd8`，开始 M1 前确认远端 master 一致。M1 修正 12 个告警类及导出对话框的运行时上下文；导出格式使用提取时的 `ProjectRenderer` 上下文。未批量增加 Q_OBJECT。新增自带 SVS/RVC 固定声明映射，仅本地显示副本改变，第三方名称、用户数据、参数 ID、请求和保存数据保持原值。

23 个原始中文键迁移为英文 source；同时把已确认的 SVS 工程导入导出中文固定提示接入 `SVSProjectUI`。迁移映射见 `source-migrations.json`。禁用 sametext/similartext 启发式后同步四语目录，保留已有有效译文；英文全部使用有效英文 source 回退。当前 3563 键，相对 M0 减去 23 个旧键、新增 140 个键，四语缺键均为 0。中/日/韩空译分别 1544/1908/593，原有日文占位符异常 3 项留 M4；后续归属逐键保存在 current-audit.json，本阶段不宣称这些待译项完成。

已确认的硬编码用户提示标为 ENTRY_READY，待所属阶段补译；协议元数据、专有名称和原始日志按理由关闭，其余 682 个候选仍 OPEN，继续沿原调查路径收敛，M6 前必须有逐项结论。固定声明字段已接入可提取映射，不修改插件 ABI。

验证：四语 lrelease 退出码 0；最终前台构建 `lmms UiBaselineCapture SVSIntegrationTest` 通过。开发可执行文件 `build/Release/lmms.exe`；启用的 amplifier/kicker/tripleoscillator DLL 位于 `build/Release/plugins`，SVS 引擎沿既有 `build/Release/svs` 部署。首次编译发现 QJsonArray 代理不能绑定非常量引用，改为索引更新后通过，失败日志保留。

`translationEntryPoints` 和 `unselectedSingerPreview` 各 3 PASS、0 FAIL。前者逐语言验证真实参数窗口的译文命中、Mixed 悬浮提示、固定声明及未知数据回退，并验证导出窗口/格式上下文；后者验证迁移后的声库空状态与菜单。截图均来自真实 Windows QScreen 窗口，100% 缩放；环境为 Qt 6.10.3、Microsoft YaHei UI、默认开发主题。四语截图中 CJK/韩文字形正常。后续尚未补译的导出标签按 M4 处理。测试日志中的 JACK 未运行和测试可执行路径缺少 SVSCompute 属既有运行环境警告，未扩大本阶段修改范围；不把此测试当作计算功能验证。

## M2 结果

M1 检查点 `6d8bb56799c792539885f4a1ce4da6ff065908f0` 已推送并核对远端。M2 完成 294 个原分配键，并为声库图片本地错误、音域解析、工程恢复及声库浏览描述增加 13 个必要入口，总计 307 个 M2 键。全库当前 3576 键，中/日/韩剩余空译 1330/1618/303，均属于后续阶段；日文原有 3 个异常仍留 M4。

中日韩空缺补译分别 227/303/303 项；已有有效译文保留，英文使用源文回退。同原文只保留 AI/SVS/PAN/VOL、加减符号和占位符排版模板，逐语言已记录。M2 既有声明字段均有结论；用户数据和声明分组排序不改变。已接入本地 GUI 提示关闭，尚未确认的核心/插件诊断候选继续在 M6 逐项核对，不计为已翻译。

质量命令：`python buildtools/translation-audit.py check --stage M2`，307 键 × 四语 PASS；检查覆盖、占位符、复数、同原文处置、HTML、换行及助记符。四语 QM 生成退出码 0，`lmms UiBaselineCapture` 前台编译通过，开发程序仍为 `build/Release/lmms.exe`。没有修改其他语言目录或算法。

`svsTranslations` 原生实窗测试 3 PASS、0 FAIL，逐语言打开实际导入对话框、SVS 设置页和未选歌手的 SVS 钢琴窗，关闭后进入下一语言。设置页验证内存策略译文及完整长提示，截图核对 DiffSinger 固定参数、声库空状态和长段落；编辑器核对音高不支持状态、种子/重录及正常 CJK/韩文字形。全部 100% 缩放。测试程序路径下 SVSCompute 未部署的原始诊断如实显示，计算设备实际探测由 M7 开发程序场景验证，不将测试环境回退当作真实推理通过。

## M3 验证范围

M2 检查点 `10272cf192df0b0716f6e73c2475b82f85e3f5fa` 已推送并确认远端。M3 143 个现有键及 1 个内置参考引擎增益键完成三语补译，保留少量已有有效译文；英文 source 回退不变。本阶段四语缺键、空译、unfinished、占位符异常为 0，质量检查 PASS。全库剩余空译为中 1189、日 1476、韩 161，日文原有 3 个异常留 M4。

本阶段不改服务接口、转换算法、模型/权重/索引名称、令牌或请求数据。RVC 固定参数映射由 M1 接入，M3 完成其译文；`Re-render` 在 SVC 语义中统一为重新转换。SVC comparison 候选是临时试听音频总线内部名称，非可选混音器通道文案，记录理由后保留。

代表性的实时 RVC 参数服务和 A/B 过载状态尚未在本阶段取得实际音频窗口证据，标记 `MANUAL/PENDING`，M7 使用现有服务/音频状态恢复验证；不把静态译文检查或无片段窗口冒充该场景 PASS。

实窗复核发现内置参考引擎 JSON 中的 Gain 和 identity 模型名尚未命中显示翻译，增加仅限 `reference/gain` 和 `reference/identity` 的显示映射，原能力 JSON 和 ID 保持不变。缓存的连接状态依赖启动语言；测试改为每种语言重新启动一个原生进程，在 GUI/目录初始化前安装相应 QM，遵守应用重启换语言的行为，不修改生产缓存或引入热切换功能。M3 当前 144 键、全库 3577 键，质量检查再次 PASS。

最终前台构建通过，四语分别重启的 svcTranslations 各 3 PASS、0 FAIL。最新实窗截图确认参考引擎名、增益及初始化连接状态正常命中所选语言，中日韩字形正常，100% 缩放。实时 RVC 参数与 A/B 过载仍按上述 MANUAL/PENDING 记录。

## M4 结果

M3 检查点 `249f129d5b6db7cb158429985f75507eda5c1805` 已推送并核对远端。M4 完成 1381 个主程序及 VST 键，补齐设置、微调音器、编辑器操作和宿主状态说明；对同原文操作名补译，仅保留有逐语言理由的品牌、标准缩写、和弦记号及模板。计划指定的 TripleOscillator 三处日语 %1 错误在本阶段修复。全库仍有中/日/韩空译 846/980/54，属于后续插件阶段；占位符异常已清零。

M4 质量检查 PASS，四语 QM 生成通过，前台 `lmms UiBaselineCapture` 编译通过。审计助记符识别区分 HTML 实体和文本中的独立 &，避免把自然语言连接符误认为快捷键。修复既有关于页中文换行/HTML 以及韩语步进录音方向键标记，未更改其他语言目录。生产可执行文件仍为 `build/Release/lmms.exe`，测试依赖的 amplifier/kicker/tripleoscillator DLL 均写入原有 `build/Release/plugins`。

四语分别启动真实 Windows Qt 窗口，hostTranslations 各 3 PASS、0 FAIL；截图覆盖主窗口、常规设置、VST 设置及导出，100% 缩放，中日韩字形正常。VST 设置底部长说明需滚动查看，M7 验证滚动与完整文案的一致性，不以局部截图宣称完整可读性通过。测试日志中的 JACK/Carla 既有环境诊断不扩展当前修改范围。核心和插件诊断候选的调用方复核仍按计划留 M6。

## M5 插件批次

M4 检查点 `f036467a1584780e9f8b91b4d5ebc81300cfe9b2` 已推送并确认远端。M5-01 涵盖 Amplifier、AudioFileProcessor、BassBooster、BitInvader、Bitcrush 及 Carla 的关联 context，当前 103 键四语质量 PASS。新增 Carla 输入/输出参数两个标签入口，移除两个图标按钮共用的无效空 source（仍显示图标和原悬浮说明），全库 3578 键；剩余中/日/韩空译 840/961/53。Sinc 保留为标准插值算法名，通用操作与波形名补译。

四语 QM 生成、UiBaselineCapture 和 Carla 目标编译成功，CarlaBase/Rack/Patchbay 及支持库使用 `build/Release/plugins`，开发程序为 `build/Release/lmms.exe`。测试支持指定本批插件，避免无关窗口与截图；预设保存/恢复检查沿用既有测试。四语 pluginPanels 各 3 PASS，真实 Windows 窗口截图覆盖五个可用原生插件，100% 缩放。Carla 依赖运行环境，窗口未验证，逐语言覆盖记录为 MANUAL/PENDING。日语 Bitcrush 的固定小面板标签拥挤，M7 核对已授权的省略与悬浮全文行为，本批不宣称该项视觉完整性通过。

M5-01 实窗补充：推送 `16f6990a30263fcce62533a271798d04e1af0522` 后复核韩语截图，确认 BitInvader 的 Interpolation/Normalize 原来仅提示被翻译，显示标签仍使用字面量。以既有 tr 入口替换两个标签，不增加键、不改控件位置、预设或算法。新增真实 LedCheckBox 文本断言，四语重新启动实窗复测。首次测试编译遇到 LedCheckBox::text() 非 const 接口，调整测试遍历指针后通过，不修改生产接口；失败日志保留。
补充验证结果：四语 BitInvader 复测各 3 PASS，实际标签和截图均命中译文，100% 缩放；M5-01 103 键质量再次 PASS。

M5-02：前批补充修正 `1150afb55ba9d60b7ab7c37667fa8abfaa2534c6` 推送确认后开始。Compressor/CrossoverEQ/Delay/Dispersion/DualFilter 共 192 键四语质量 PASS；中/日/韩分别修改 109/114/12 个待译或同原文项，保留 Moog 专名、DC 缩写与 Hz 单位，逐语言理由已记录。全库剩余空译 736/861/52，全部为后续插件批次。

本批仅译文，无 C++ 变更，按计划生成四语 QM 并复用已编译的原生窗口测试程序。四语 pluginPanels 各 3 PASS、0 FAIL，100% 缩放，五个真实效果器窗及预设保存/恢复检查通过。代表截图确认中日韩字形正常。CodeGraph 核对 CompressorControlDialog：面板英文标识来自 controlsBox 与按钮位图，运行时参数提示使用 tr；保留既有位图，不改主题/控件设计，实际悬浮提示的可读性留 M7。开发加载目录仍为 build/Release/plugins。

M5-03：上一检查点 64ae1368301dc80a01e13ce1d254503f03572272 已推送确认。Eq、FreeBoy、DynamicsProcessor、Flanger、GigPlayer、FrequencyShifter 共 216 键四语质量 PASS。移除 Eq 无效空 source，补上 FrequencyShifter 17 个实际控件提示入口，完整补译其 HTML 帮助并保持标签结构、数值和技术内容。plugin-batches.json 固定已定义批次，避免新增入口使后续插件重新分组；本批保持插件完整，不扩大功能范围。全库 3594 键，中/日/韩剩余空译 650/801/38，占位符异常 0。

四语 QM 生成、eq/frequencyshifter/UiBaselineCapture 前台编译通过，DLL 写入 build/Release/plugins，开发程序仍为 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，真实 Windows 窗口、100% 缩放；五个可用插件面板及 FrequencyShifter 帮助页取得截图，中文/日文帮助及韩文面板字形正常。GigPlayer 当前部署不可用，各语言覆盖记录 MANUAL/PENDING。帮助测试首次假定独立面板有父窗口而失败，修正测试空指针检查后重新编译、四语重测通过，保留失败日志；不修改生产窗口结构。位图标签及其悬浮全文验收继续按 M7 范围处理。

M5-04：M5-03 检查点 8e24a7a0431a9f2e4df373d49a87f89150628eb4 已推送确认后开始。GranularPitchShifter、Kicker 及关联浏览器键共 64 键四语质量 PASS。新增粒子变调器帮助正文/标题和 seconds/octaves 单位提示 4 个必要入口，HTML 结构、数值与含义保留；其他有效译文保持原值。全库 3598 键，中/日/韩剩余空译 611/752/38。

四语 QM 生成、granularpitchshifter/UiBaselineCapture 编译通过，开发程序 build/Release/lmms.exe，插件仍在 build/Release/plugins。四语原生 Windows 实窗各 3 PASS、0 FAIL，100% 缩放，覆盖两插件面板、帮助页及预设恢复，中文/日文帮助字形正常。首次测试关闭 GUI 时，原插件静态帮助对象的父对象删除导致堆错误；测试在捕获后解除静态对象的父对象归属，四语完整清理与退出重新通过。仅调整测试清理，不改插件窗口生命周期；该既有生产生命周期风险记为后续事项。缺失 granularpitchshifter/logo 资源也记为后续事项，不扩张本批翻译范围。

M5-05：前批检查点及账本格式修正 0d8ba6ec66d043f944f9747e2a8e62fae0f741b6 已推送确认。LOMM/LadspaBrowser/LadspaEffect 共 197 键四语质量 PASS；新增 LOMM 固定预读延迟的 %1 模板，替代提取器无法处理的动态 tr 拼接，不改数值与算法。补译多段压缩参数及 LADSPA 固定状态/端口说明，第三方插件名称和作者元数据保留。全库 3599 键，中/日/韩剩余空译 473/596/28。

四语 QM、lomm 前台编译成功，DLL 写入 build/Release/plugins/lomm.dll；开发程序 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，覆盖 LOMM、LADSPA 浏览器及现有 LADSPA 效果器宿主面板，预设保存/恢复通过，固定说明正常显示。LOMM 英文位图标识保留，长提示实际悬浮验收留 M7，不将位图重绘纳入本批。

M5-06：M5-05 检查点 e478286f74b2e31dffb2be2c2dd1c2d0b5b00031 已推送确认。Lb302/Lv2Effect/Lv2Instrument/MidiImport/MidiExport 共 59 键四语质量 PASS。指数波按钮原来错误复用白噪声名称，增加 Exponential wave 正确入口，音频与波形顺序不变；补齐 MIDI 导入导出说明和 LB302 波形/滤波提示。两项既有日语 MIDI 告警的额外换行修复。全库 3600 键，中/日/韩剩余空译 440/565/18。

四语 QM、lb302 编译通过，build/Release/plugins/lb302.dll 部署原位，开发程序仍为 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，LB302 面板与预设保存/恢复通过，主机中日韩字形正常；面板既有英文位图保留。当前没有 LV2 外部样本，Lv2Effect/Lv2Instrument 均明确记录 MANUAL/PENDING。MIDI 导入错误弹窗的最终真实场景留 M7，不将静态质量检查视为该场景验收。

M5-07：M5-06 检查点 95d98cba92a94e50b3ed56f5911896f84ea6fcc3 已推送确认。Monstro/MultitapEcho 共 161 键四语质量 PASS，补齐日语振荡器/包络/LFO 调制矩阵参数、波形和三语剩余回声提示；已有有效译文保留。MultitapEcho 交换输入复选框标签接入既有 tr 键，不增加键、不改预设或控件位置。全库 3600 键，中/日/韩剩余空译 429/481/18。

四语 QM 生成与 multitapecho 编译通过，DLL 写入 build/Release/plugins/multitapecho.dll；开发程序仍为 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，两插件面板及预设保存/恢复通过，中文复选框已显示译文。日语交换输入的标签与相邻旋钮标签拥挤，M7 核对授权范围内的省略与悬浮全文；不宣称此项完整可读性通过。Monstro 位图标识保留，矩阵实际切换及悬浮提示留最终场景矩阵。

M5-08：M5-07 检查点 8bedb1022fa19986e6352c8f9868082d622c59a3 已推送确认。Nes/OpulenZ/Organic/Oscilloscope/Patman/PeakControllerEffect/ReverbSC 共 195 键四语质量 PASS。Organic 的 18 个谐波名称及 6 个波形名称增加固定上下文提取和显示翻译，不改数组顺序、参数、预设或算法；FM 保留标准缩写并逐语言登记。实窗复核修正既有日语 DCAY 误译为 ATCK 的问题，改为ディケイ并重新验证。全库 3624 键，中/日/韩剩余空译 353/377/14。

四语 QM 生成、organic 前台编译通过，DLL 写入 build/Release/plugins/organic.dll；开发程序仍为 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，七个真实插件面板及预设恢复通过，100% 缩放，中日韩字形正常。日语峰值控制器修正后的独立复测 3 PASS；M5-08-ja-decay 截图为该标签最终证据。既有位图标识保留，完整悬浮参数提示留 M7。

M5-09：M5-08 检查点 371dcab0bafb871325f0f1dac25f5f6312272617 已推送确认。Sf2Player/Sfxr/Sid/SlewDistortion/SlicerT 共 177 键四语质量 PASS。补齐 SoundFont 合唱/混响、SID 声部、失真类型及切片操作；完整补译 Slew Distortion 日语 HTML 帮助，保持标签和技术内容。新增帮助标题、SlicerT 的 Off 与无采样状态三个入口，不改变音频参数和预设。Tanh 按数学函数处理，芯片型号/BPM/MIDI 保留并登记。全库 3627 键，中/日/韩剩余空译 240/257/14。

四语 QM、slewdistortion/slicert/UiBaselineCapture 编译通过，DLL 部署于 build/Release/plugins，开发程序 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，四个已部署插件面板、真实帮助窗与预设恢复通过；SID 未部署，MANUAL/PENDING。SlicerT 固定文字修正后另行四语复测，100% 原生 Windows 实窗。中日韩字形正常；SlicerT 的 SYNC/CLEAR 标识来自按钮位图，保留既有 artwork，完整操作提示已翻译，M7 验证悬浮全文和窄标签省略，不重绘位图或改变布局。

M5-10：M5-09 检查点 d24c0d7422be0b503b3e29651f767c9d6fe2cb1f 已推送确认。SpectrumAnalyzer/StereoEnhancer/StereoMatrix/Stk/TapTempo 共 174 键四语质量 PASS。补齐频谱分析的 FFT、幅度/频率范围、平均/峰值、瀑布图及高级说明，STK 乐器预设/演奏参数和节拍测速操作。ADSR 与 Hz/ms 数值模板作为标准技术表示保留并逐语言登记；英文源文回退。全库 3627 键，中/日/韩剩余空译 114/129/12。

本批仅译文，四语 QM 生成通过，按计划复用已编译的原生测试程序。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，五个插件均取得面板截图，预设恢复通过（工具面板不适用）。STK 使用既有 build/vcpkg_installed/x64-windows/share/libstk/rawwaves，不创建新部署目录。代表截图确认中文测速提示和日文频谱控制正常显示，字形正常；高级参数展开、完整悬浮提示及乐器其他预设切换留 M7 最终矩阵。开发程序与插件目录仍为 build/Release/lmms.exe、build/Release/plugins。

M5-11：M5-10 检查点 bcfaf3a837c258b3170dd0101f8f45efbd8c9d6c 已推送确认。TripleOscillator/Vectorscope/Vestige/Vibed/Watsyn/WaveShaper 共 197 键四语质量 PASS。补齐弦模型、振荡器调制与波表混合、矢量显示以及 VeSTige 原生宿主的扫描/标识说明；品牌、文件后缀及插件名称/标识组合模板按原值保留。实窗发现 WaveShaper 的 Clip input 显示标签未接入翻译，使用既有 tr 键修正，不增加词条、不改控件位置或预设。全库 3627 键，中/日剩余空译 35/27，韩语空译 0。

四语 QM、waveshaper 编译通过，DLL 写入 build/Release/plugins/waveshaper.dll；开发程序仍为 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，五个已部署插件面板及预设恢复通过。Vibed 未部署，MANUAL/PENDING；VeSTige 为 LMMS 未加载外部插件时的宿主面板，外部编辑器和声音不计为已验证。Clip input 标签修正后另行四语复测，M5-11-clip 截图为最终证据；既有位图按钮保留，完整悬浮说明与窄标签处理留 M7。

M5-11 视觉后续：WaveShaper 日语 Clip input 标签已命中译文，但固定窄面板右边界截断全文，留 M7 授权的省略号与完整悬浮提示处理；本批不宣称全文可读性通过。

M5-12：M5-11 检查点 7a0fcedc0a66fddbe4aef4cd8363cd6503ecb970 已推送确认。Xpressive/ZynAddSubFx 共 66 键四语质量 PASS。Xpressive 新增帮助正文/标题、插值复选框及固定旋钮名称 6 个必要入口，完整补译变量/函数/调制说明并保留公式、数值范围和 HTML 结构；表达式变量 A1/A2/A3 保留并逐语言登记。补齐 ZynAddSubFX 宿主参数与 MIDI 转发，第三方内部编辑器不改写。全库 3633 键，中日韩缺键、空译、unfinished 和占位符异常均为 0；英文为源文回退。

四语 QM 生成、xpressive/UiBaselineCapture 前台编译通过，DLL 写入 build/Release/plugins/xpressive.dll，开发程序 build/Release/lmms.exe。四语 pluginPanels 各 3 PASS、0 FAIL，100% 原生 Windows 实窗，两插件面板、Xpressive 真实帮助页及预设恢复通过。代表截图确认中文帮助、日语插值与韩文宿主字形正常；帮助长文可滚动，最终底部和长提示验收留 M7。Zyn 原始空 XML 调试日志不作为功能失败或新修改范围。M5 十二批已逐批提交推送；M6 继续全库质量和硬编码候选调用方收敛，M7 完成最终场景矩阵。


## M6 覆盖收敛

M5 检查点 1c91d9fa5d9328a96f0fd4c43ac44bda979c0f00 已推送核对。当前 3877 键，比 M5 增加 244；66 个旧中文项目诊断迁移为 64 个去重英文键。补齐本地 SVS/SVC 校验、宿主失败、字典、曲线、时间映射、项目桥接、扫描目录及目录缓存诊断入口；不改验证条件、算法、协议标识和外部插件原始错误。原始发音诊断仅在 UI 显示副本上翻译。

中/日/韩有效译文 3723/3714/3721，同原文保留 154/163/156，均有逐语言理由；英文 3877 源文回退。四语缺键、空译、unfinished、占位符及复数异常为 0；HTML、换行、助记符检查 PASS。账本 1306 个硬编码候选、25 个声明及 779 条逐语言同原文复核全部 CLOSED。内部日志、被宿主转换为错误码的异常、动态元数据和标准标识保留有调用方依据。英文审计补查日文/韩文源文、有效覆盖的占位符与两种复数分支，自检 PASS。

前台全量 Release 编译通过，开发程序 build/Release/lmms.exe；启用插件及 VST/Carla 支持 DLL 使用既有 build/Release/plugins。四语 QM 原位更新到 build/Release/data/locale，源文件与部署文件 SHA-256 一致（M6-resources.json）。SID/GigPlayer 当前未启用且该目录没有旧 DLL，无需停用；Carla 目标实际启用弱链接支持，运行依赖仍为最终验收待办。Vibed 实际目标名称是 vibedstrings，M5 使用 vibed 查询未命中；M7 按正确名称补验，纠正此前“未部署”的判断。

四语 translationClosure 各 3 PASS、0 FAIL，原生 Windows Qt、100% 缩放。截图覆盖扫描 JSON 错误、歌词诊断及原始节点保留提示，中日韩字形正常。环境 JSON 的 language 字段为系统 QLocale，实际译文语言由 LMMS_UI_TRANSLATION 和逐语言截图/断言确认。OK/Cancel、窄标签与长文案全文验收留 M7，不以本阶段截图宣称这些项目全部通过。

回归记录：项目进程、VST 路径迁移、ScanRootsWidget、CatalogIo、PluginCatalog、SVC 缓存/播放及 SVCIntegrationTest 通过。Mapper 旧中文错误断言改为迁移后的英文源文；原“空声库必拒绝”夹具与既有允许未选声库行为不一致，改为缺失一半绑定标识的无效夹具，复测通过，不改变生产行为。SVS 资源/声明/批量歌词测试原来选择目录首个声库，当前排序选到 DiffSinger；明确选择测试要求的 SVSExample，保留完整断言。首次 CTest 全量 SVS 未启用嵌入 GUI、含旧中文导入控制器断言并超过 60 秒，失败日志保留；不报告该全量测试通过。受影响路径使用嵌入原生 GUI 分组验证，最终结果见 M6-svs-regression.log。测试改动过程中名称冲突与替换错误的编译日志保留，最终编译重新通过。

M6 最终受影响 SVS 分组 11 PASS、0 FAIL（9 条路径加初始化/清理）。已有 LADSPA 支持模块重新部署到既有 build/Release/plugins/ladspa，22 个 DLL 的源/部署 SHA-256 一致，见 M6-ladspa-deployment.json；没有创建新部署目录。


## M7 最终实窗验收

M6 检查点 `9667fb5b2b5fcc36d4286fd39869d00a3c9ff82c` 推送确认后开始。只修正本计划授权的长标签省略与全文悬浮：Knob 保留原说明、数值与单位并补充被省略的名称；LedCheckBox 在既有边界省略，MultitapEcho/WaveShaper 的完整名称加入原提示；Bitcrush 标签限制在原列宽；SlicerT 页脚省略并提供原说明；歌词表格各单元格保留全文提示；SVS 状态在原工具栏与片段页脚省略，原状态全文保持。未改变字体、主题、控件位置、音频算法、预设或协议。

前台全量 Release 编译成功，最终测试程序编译成功。开发程序为 `build/Release/lmms.exe`，60 个启用插件 DLL 在 `build/Release/plugins` 原位核对；SVS 引擎沿既有 `build/Release/svs` 加载。四语源 QM 与 `build/Release/data/locale` 部署文件逐字节和 SHA-256 一致，详见 `M7-deployment.json`。SID/GigPlayer 当前禁用且没有旧 DLL；Vibed 的实际目标 `vibedstrings` 已四语实窗验证，撤销 M5-11 的“未部署”判断。

四语分别在全新进程加载目标语言和对应 Qt 标准按钮目录，使用正常 Windows Qt 平台、既定主题、100% 缩放。每种语言主场景组 5 PASS、插件组 3 PASS、导出/滚动补充组 4 PASS，均 0 FAIL（包含各组初始化与清理，不能相加冒充独立场景数量）。测试实际显示窗口，等待稳定渲染后交互，通过 QScreen 捕获真实窗口，再正常关闭；没有使用 offscreen 或 Computer Use。开发程序另外清除测试资源路径覆盖后启动，Win32 DPI=96，捕获真实窗口并正常退出。`M7-environment-*.json` 分别记录实际目标语言、系统语言和 DPR；中日韩字形正常。

场景结果和证据路径见 `M7-scene-matrix.json`：主窗口/菜单/设置、扫描错误与 MIDI 原生错误弹窗、SVSExample 声库参数/歌词/音素/曲线、音名与简谱、SVS 导入/导出选项及音频导出、SVC 空状态和固定设置、DiffSinger 固定选项、内存策略长说明通过。SVS 长状态在 1100/900 宽度下保持省略显示与全文一致；歌词表格诊断实际悬浮全文通过；声库参数和曲线的现有滚动区域可读取长内容。

18 个代表原生插件在四语新进程中显示并验证，适用的预设保存/恢复通过。补验 Monstro 矩阵、频谱高级设置、STK 9/10 预设、Xpressive 帮助末尾与 SlicerT 页脚两项悬浮说明。代表截图确认日语 Bitcrush/MultitapEcho/WaveShaper 的窄标签省略明确、全文可悬浮读取，中日韩文字正常；英文位图、品牌和用户/第三方数据按范围保留。

初次测试发现激活 Qt tooltip 会使其销毁、独立效果器测试窗口不符合生产 EffectView 的 MDI 容器，以及 tooltip 瞬态/延迟隐藏导致截图不稳定。仅修正测试路径：复用生产容器，等待可见且对应目标的真实提示，捕获时不激活提示窗口，关闭后等待隐藏完成；失败日志保留。实窗还发现 SVS 状态最小宽度为零时不可见，将最小宽度设为一个省略号并重新编译、四语重测通过。标准确认/取消按钮原先测试程序未加载 Qt 目录，现与生产程序加载方式一致，未修改生产字体或按钮。最终提取首次漏用 `-I include/` 造成上下文误判，恢复既定命令后质量检查 PASS，失败日志保留。

最终静态统计仍为 3877 键，中/日/韩译文 3723/3714/3721，同原文保留 154/163/156，英文 3877 源文回退；四语缺键、空译、unfinished、占位符、复数、HTML、换行及助记符质量 PASS。1306 候选、25 声明、779 条逐语言同原文复核全部 CLOSED。日志见 `M7-extraction.log`、`M7-audit.log`、`M7-quality.log`、`M7-selfcheck.log`；没有修改其他语言目录。

人工验收尚未完成，明确为非阻塞 `MANUAL/PENDING`：DiffSinger 实际模型合成/音高重录/DirectML 运算、实时 RVC 参数与 A/B 过载、外部项目格式完整转换与损失弹窗、Carla 运行依赖、外部 LV2 样本，以及禁用的 SID/GigPlayer。矩阵逐项记录原因和恢复方法。测试入口副本的 SVSCompute 路径告警不代表生产 DLL 缺失，也不作为实际 AI 运算通过的证据；生产 DLL 原位存在。根据计划“自动检查通过且非阻塞人工项待执行时，可完成阶段提交推送检查点”，M0～M7 实施检查点完成；未将人工项记为 PASS。


## 用户截图补充：侧栏标题与轨道旋钮

在既有提交 607b75ab5 上修正三个未接入翻译的侧栏标题：SVC、SVS、My Favorites。中文显示歌声转换、歌声合成、我的收藏；日文与韩文同步补齐，英文保留标准缩写。SVS/SVC 轨道的 VOL/PAN 原来按缩写保留，现复用普通 InstrumentTrackView 同键译文，并修正 12 条逐语言复核结论；参数、单位和轨道数据不变。

开发程序 build/Release/lmms.exe 和 UiBaselineCapture 编译 PASS，相关插件依赖原位链接至 build/Release/plugins。四语各 3 PASS、0 FAIL，真实 Windows Qt 窗口、100% 缩放，验证实际侧栏标题/按钮、SVS/SVC 轨道旋钮，并保留四语截图；中日韩字形正常。四份 QM 与部署文件一致，见 sidebar-followup-resources.json。全库当前 3885 键质量 PASS；本修正新增 3 个标题键，另 5 个新增提取键来自开始前已有的声库说明提交，不属于本次实现。未修改其他语言。
