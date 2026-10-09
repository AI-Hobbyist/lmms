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
| M4～M7 | TODO | 未执行 | 未执行 | 未提交 |

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
