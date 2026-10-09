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
| M1～M7 | TODO | 未执行 | 未执行 | 未提交 |

M0 提取曾因输入列表带双引号被 lupdate 当成文件名失败，移除列表内引号后通过；没有变更任何 TS 或业务代码。
