# DiffSinger 参考实现与真实声库研究笔记

日期：2026-10-08。范围：只读核对 `refs/tlds_ref`、`refs/tl_ref`、`refs/Singers`，为“先引擎、后全局 DirectML 后端”计划提供证据。本笔记不是实现完成报告，没有运行模型或声称实机推理通过。根目录 `singers` 不存在；用户所指声库实际位于 `refs/Singers`。

## 实际声库清单与格式

当前六个模型包均为两层目录：`refs/Singers/<包名>-DiffSinger/<包名>/`。包名/`character.txt:1` 展示名为：

| 模型目录 | 名称 |
| --- | --- |
| `fu2_ning2_na4` | 芙宁娜 |
| `na4_wei2_lai2_te4` | 那维莱特 |
| `na4_xi1_da2` | 纳西妲 |
| `wen1_di2` | 温迪 |
| `xi1_ge2_wen2` | 希格雯 |
| `xia2_die2` | 遐蝶 |

每个包具备主 `dsconfig.yaml`、`character.yaml`、`character.txt`、`dsdur/`、`dspitch/`、`dsvariance/`、自带 `dsvocoder/`。三个预测器子目录也有同名 `dsconfig.yaml`，不能把每一份配置当作独立声库。

典型证据：[芙宁娜主配置](../../refs/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4/dsconfig.yaml)（仓库路径 `refs/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4/dsconfig.yaml:1-35`）：配置指定声学 ONNX、JSON 音素表/语言表；44100Hz、hop512、mel128、FFT/window2048、40～16000Hz、`mel_base:e`、`mel_scale:slaney`、depth0.6；启用 breathiness/key_shift/speed/tension/voicing，energy 和 lang_id 关闭。六包这些能力位一致，模型与词表文件名前缀不同，不能依赖固定文件名或共享单个角色词表。

三角色配置的典型契约：`.../dsdur/dsconfig.yaml:1-10` 为 linguistic+dur、`predict_dur:true`；`.../dspitch/dsconfig.yaml:1-12` 为 linguistic+pitch、`use_expr:true/use_note_rest:true`；`.../dsvariance/dsconfig.yaml:1-14` 为 linguistic+variance，预测 breathiness/voicing/tension，energy 关闭。这些设置在六包一致。JSON 语言表 `.../fu2_ning2_na4_aco.languages.json:1-6` 含 en/ja/ko/zh，**词表有多语言标识不等于整个声库具备该语言 G2P 或有效训练能力**。需要核验词典和音素器后才声明能力。

### 元数据需要逐字段合并

`.../character.yaml:1-10` 声明 OpenUtau 中文默认音素器、`portrait:character.png`、`portrait_opacity:0.67`、`singer_type:diffsinger`、`text_file_encoding:utf-8`、subbanks/name0；`.../character.txt:1-7` 才提供 `name`、`image:avatar.png`、author、voice、web、version、sample。只读 YAML 会失去头像和名称。应按字段明确 YAML 优先、缺失字段回退 TXT，再处理显式 manifest 覆盖；路径相对于所属配置/元数据包根解析，作者与配音者分开保留。

参考实现的 `CharacterMetadata.cs:19-29` 是“有 YAML 就返回”，不是字段合并；`FromYaml:55-60` 未读 portrait_opacity/text_file_encoding，`ReadTextName:34-37` 另走 TXT。不能无条件移植这一行为。

真实 JSON 不只有程序设置：`.../comfort.json:1-15` 包括 comfort/available/weak 音区元数据；`*.phonemes.json` 与 `*.languages.json` 是模型 token 字典。当前没有实际 `character.json`、`tunelab.yaml` 样本，计划可以定义兼容解析 fixture，但不得宣称已在六包验证这些格式，也不应把所有 JSON 都按同一 schema 解析。comfort 字段可保留为来源元数据，本次不由其添加新的音区 UI。

### 递归发现与包判定

`VoicebankScanner.cs:14,40-66` 参考采用最大深度6、命中包后停止向下、主配置+character.yaml/txt 判定，按完整路径去重。用户要求递归，应覆盖当前外壳层级及更深包装目录、重复搜索根、链接循环、不可读子目录、错误配置隔离；不能照搬固定6层导致合法深包静默丢失。候选还应验证配置声明 acoustic，排除仅有 linguistic/dur/pitch/variance 的预测器。仅元数据缺失的模型包也应有确定的文件夹名回退/诊断策略；明确 JSON/YML 兼容与冲突优先级。

## 完整推理与编辑语义

`DiffSingerSynthesisSession.cs:197-250,350-631` 当前实际已实现完整 Render；文件顶部14-17的“暂不产音”描述属于旧注释，不能作为现状判断。

1. 冻结短语音符、属性与曲线，解析 voice/model/version 对应物理包，校验声学与声码器频谱契约（197-222）。
2. 歌词/读音→词典/G2P→dur linguistic+dur→带归属的音素时序（233-238）；缺 dur 的参考降级是每音符一元音（634-643），应明确降级提示与能力限制，不能称与正常 G2P 等价。
3. 包含前后 SP，累积取整得到帧时长，帧步长 hop/sample_rate，renderStart 包含前导（245-250）；不要独立四舍五入造成整体漂移。
4. dspitch linguistic+pitch 生成自然音高；用户画过的绝对音高覆盖预测，自由区使用预测（无模型回退 note pitch），PITD/偏差叠加，再从半音转 f0 Hz（364-388）。
5. dsvariance linguistic+variance→预测 breathiness/voicing/tension 等，曲线按各模型能力声明；最终参数应按真实输入量程钳位（408-466）。`DiffSingerVarianceCurve.cs:26-56` 的实际规则是**实参画过的帧直接成为终值；否则预测基线加偏差，再 clamp**；旧 schema prose 的不同顺序不能压过运行代码。
6. acoustic 条件按 InputMetadata 构造 tokens/languages/durations/f0、参数与可选 `[1,frames,hidden]` spk_embed；连续加速 steps/depth 与旧 speedup 导出形态分开，后者1000/steps取可整除的 speedup（416-498）。
7. mel输出→按 e/10 对数底变换→vocoder mel(+f0)→waveform（508-593）。发布 audio/renderStart/sampleRate、逐音符音素和音高/参数只读回显（596-631）。

预测器有独立词表与 embedding，不能用 acoustic 的 token id 直接喂所有角色（`DiffSingerPredictor.cs:14-16,55-81`）。linguistic 根据实际是否存在 `word_div` 区分 word_div/word_dur 模式与 ph_dur 模式（`DiffSingerPitch.cs:10-18`，`DiffSingerVariance.cs:48-85`）；dur 输出为 `ph_dur_pred`（`DiffSingerPhonemizer.cs:369-415`）。模型可选输入/输出、dtype、rank、dynamic shape、missing-role 应在加载和测试中明确，不能只对一个固定导出版本成立。

音素语义参考：`DiffSingerPhonemizer.cs:27-31,49-100` 中 `+` 表示多音节词推进，`-` 表示连通音符的延音；句首孤儿/断链必须有明确处理。`DiffSingerSynthesisSession.cs:100-119` 是插件自己的延音判定。用户钉死音素不走自由 G2P；空表代表自动，不应产生 null 手动覆盖。

TuneLab 当前 SDK 使用引导/主体两列表、标称时长秒、弹性权重、带符号 BodyOffset（`refs/tl_ref/TuneLab.SDK/Voice/VoiceSynthesisNoteSnapshot.cs:5-46`，`.../Synthesis/SynthesizedPhoneme.cs:3-37`，`.../Synthesis/PhonemeLayout.cs:6-29`）。共享布局以 junction 为锚、在音符头划分拍前/拍后域，相接时借入下一音符前导并压缩（`PhonemeLayout.cs:54-68,94-167`）。LMMS应在现有SVS数据模型做适配、测试音频和音素条一致，不能直接复制TuneLab宿主API或扩大普通钢琴窗范围。

`DiffSingerPredictor.cs:271-280` 词典路径顺序包含自定义 phonemizer 字典名、dsdict-lang、dsdict-zh-lang、dsdict.yaml。`ExternalPhonemizers.cs:42-55,64-96` 参考加载声库 managed DLL 和 OpenUtau 门面；这些.NET插件不能直接 dlopen 成原生 LMMS SVS 插件。引擎计划必须明确自定义音素器支持方式/隔离桥接与缺依赖诊断，而不能以“扫描到了DLL”算支持。

### 声码器解析

真实 `.../dsvocoder/vocoder.yaml:1-13` 指向 bundled Kouon微调模型，pitch_controllable:true、force_on_cpu:false；主 dsconfig 的 vocoder 名却为另一模板名称。`DiffSingerModels.cs:138-158` 先选包内 dsvocoder/vocoder.yaml，再按配置名查询用户全局 vocoder 根，必须保留这一优先级。缓存按实际物理目录而非名称（110-112）。参考未读取 force_on_cpu，应在后续后端计划定义该字段true如何通过CPU路径执行。

## DirectML 参考实现及不能照搬的部分

`DiffSingerModels.cs:159-197` CPU纯CPU，DML调用 AppendExecutionProvider_DML(0)，报错不在同一进程切CPU；`RuntimeHost.cs:98-109` 同样硬编码设备0。**没有实际适配器枚举、稳定LUID、真实设备选择，也没有显式 Sequential/禁用memory pattern设置**。这些应按官方ORT DML契约补足，不应记为参考已实现。

`RuntimeHost.cs:70-94` 使用单一锁串行 Run+复制输出及释放，避免会话原生内存跨请求泄漏。`RuntimeClient.cs:8-10,43-83,123-139` 子进程崩溃丢连接、下次重新加载；DML失败仍提示改CPU重启，当前实际并非自动透明CPU回退。`DiffSingerModels.cs:29-68` 默认子进程但启动失败会退进程内。这是参考故障策略，不代表LMMS应复制全部.NET运行结构。

`DirectMlNative.cs:7-29` 显式全路径预载打包DirectML.dll，处理ORT delay-load搜索到System32旧库的问题。`DiffSingerForTuneLab.csproj:99-108` 使用 ORT DirectML1.23.0、Microsoft.AI.DirectML1.15.4。这是参考锁定版本，不是最新版本断言；新计划需依自己的SDK组合锁定版本并验证干净机器部署。

全局加速应覆盖接入共享后端的各AI引擎模型（包含G2P/linguistic/dur/pitch/variance/acoustic/vocoder中适用阶段），由实际支持能力驱动下拉启用。CPU文字为CPU。传统拼接保持不受影响；未接共享后端的第三方AI不能因设置存在而声称已加速。设备切换要使会话/缓存/代际结果正确失效，不把推理挪进音频实时回调。

## 许可与验收证据边界

`refs/tlds_ref/LICENSE.txt:1-13` MIT（2026 Jingang），`refs/tl_ref/LICENSE.txt:1-13` MIT（2024 Jingang）；复制实质代码需保留版权许可。`refs/tlds_ref/THIRD-PARTY-NOTICES.md:5-35` 声明OpenUtau门面/G2P等第三方来源；语言包还需逐项核对来源许可。ORT/DirectML/YAML依赖按最终选定包核对通知。

`refs/Singers/README.txt:1-8` 是放置说明；`.../dsvocoder/README.txt:1` 只说明基于角色语音微调，没有给出模型/图片再分发授权。这六库只作本地fixture，不随SDK/Release/ZIP分发；不能把软件MIT许可推及模型和角色资源。

计划验收必须分别证明：递归六包发现且只发现六个主包、YAML+TXT字段合并（头像/立绘/0.67透明度）、JSON映射与配置兼容、六包CPU端到端有限音频输出、用户手动音素与头尾拉伸不破坏渲染、自动/手动曲线和预测回显、bundled声码器优先和缺文件可读诊断。后端阶段另证明真实适配器探测、稳定设备映射、CPU/DML各适用阶段确实调用对应provider、会话并发限制、强制CPU模型策略、设备失败/切换/取消与旧结果丢弃、传统/不支持引擎下拉状态、洁净部署和fixture独立性。只写计划不等于这些实现门槛当前已PASS。
