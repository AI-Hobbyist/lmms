# DiffSinger A0 原生依赖与音素化冻结依据

日期：2026-10-08。范围：A0 的依赖调查；不表示 A2/A3 已通过真实模型推理。六包配置以本机 `refs/Singers` 为 fixture，声库资源不进入 SDK/Release。

## 核心依赖

| 组件 | 冻结版本 | 用途与运行形态 | 许可、第一方来源 |
| --- | --- | --- | --- |
| ONNX Runtime | 1.23.0，Windows x64 native CPU | C/C++ API，原生 DLL；A 不注册 DML/CUDA EP | [版本发布](https://github.com/microsoft/onnxruntime/releases/tag/v1.23.0)、[MIT](https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.0/LICENSE) |
| nlohmann/json | 3.12.0 | header-only；配置、音素/语言 ID 映射 | [版本发布](https://github.com/nlohmann/json/releases/tag/v3.12.0)、[MIT](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT) |
| yaml-cpp | 0.8.0 | 原生 C++ YAML；静态链接到引擎，避免另一份运行时 DLL | [版本发布](https://github.com/jbeder/yaml-cpp/releases/tag/0.8.0)、[MIT](https://raw.githubusercontent.com/jbeder/yaml-cpp/0.8.0/LICENSE) |

上述版本是 A 的固定调查/实现基线，不跟随 latest 自动更新。ORT 的 C++ API 是 C API 包装，官方提供 native CPU 发布包，不需要安装 .NET 或 Python：[官方 C++ 文档](https://onnxruntime.ai/docs/get-started/with-cpp.html)。头文件、导入库和 DLL 必须来自同一版本/架构；部署时记录实际文件 SHA-256，保留该发布包的 LICENSE/ThirdPartyNotices。不能将引用工程内不同 ORT 版本的 DLL 直接混配。DirectML 依赖属于 B0，不在 A 添加。

## 六包实际音素化要求

`fu2_ning2_na4`、`na4_wei2_lai2_te4`、`na4_xi1_da2`、`wen1_di2`、`xi1_ge2_wen2`、`xia2_die2` 六个包的 `character.yaml` 均声明 `OpenUtau.Core.DiffSinger.DiffSingerChinesePhonemizer`。每包三个预测器目录含 `dsdict-zh.yaml`（拼音到模型音素的 entries）、`dsdict-en/ja/ko.yaml` 和合并 `dsdict.yaml`；语言表含 en/ja/ko/zh。具体目录/结构见 [既有声库调查](DiffSinger-reference-findings.md)。

中文所需链为原生汉字转无声调拼音，再按当前包的中文词典查符号；词典符号必须在当前模型角色的 phonemes JSON 中存在。拼音输入可直接查词典，不应重新猜声母韵母。示例芙宁娜 `dsdur/dsdict-zh.yaml` 的 `ba` 为 `zh/b, zh/a`，并提供 SP/AP。保留词典作者方案、entries 与 replacements 优先级；duration 与 acoustic 的 ID 空间不能共用。

第一方证据：[DiffSingerChinesePhonemizer](https://raw.githubusercontent.com/stakira/OpenUtau/master/OpenUtau.Core/DiffSinger/Phonemizers/DiffSingerChinesePhonemizer.cs) 指定 `dsdict-zh.yaml`、`zh`，调用 Romanize；[BaseChinesePhonemizer](https://raw.githubusercontent.com/stakira/OpenUtau/master/OpenUtau.Core/BaseChinesePhonemizer.cs) 使用 NORMAL 拼音并处理歌词序列中的汉字。[OpenUtau MIT](https://raw.githubusercontent.com/stakira/OpenUtau/master/LICENSE.txt) 仅证明该源码许可；不证明声库模型/图片/词典再分发授权。原生实现不加载该 C# 类型或程序。

用户补充要求（2026-10-08）：字典解析参考 `tlds_ref`，可选语言直接根据声库能力提供，不固定为中文列表。对齐 `VoicebankConfig.ResolveLanguages`，从声库声明的语言映射/序列取得稳定语言键；`use_lang_id=false` 不代表单语言。对齐 `DiffSingerPredictor.LoadDsDictFile` 的专属字典、`dsdict-{lang}.yaml`、`dsdict-zh-{lang}.yaml`、`dsdict.yaml` 顺序，以及 entries、symbols、replacements 的职责和前缀映射。语言列表与自动 G2P 支持诊断分别处理；语言 token 或随包词典不能作为完整 en/ja/ko 算法 G2P 的证据。未知字/未知词、缺失音素或不支持音素器应给出明确诊断，不用空音素或静音伪装成功。`+`/`-` 延续、SP/AP/休止和手工音素优先级在 A2 实现及验证。

## 原生中文数据选择

为避免引入额外汉字转音二进制，选用原生 C++ 查询器配合以下固定数据。它们是独立第三方公开拼音数据，不来自六个私有 fixture 包。

| 数据 | 固定 revision / 文件 | 第一方格式与许可 |
| --- | --- | --- |
| mozillazg/pinyin-data | `9193766130af24d2ac54230be979b2e98ac66223` / `pinyin.txt` | [格式说明](https://github.com/mozillazg/pinyin-data/blob/9193766130af24d2ac54230be979b2e98ac66223/README.md)、[MIT](https://raw.githubusercontent.com/mozillazg/pinyin-data/9193766130af24d2ac54230be979b2e98ac66223/LICENSE) |
| mozillazg/phrase-pinyin-data | `cee0ed6e6e4898580cafd2bd5e3723e20b214aa0` / `pinyin.txt` | [格式说明](https://github.com/mozillazg/phrase-pinyin-data/blob/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/README.md)、[MIT](https://raw.githubusercontent.com/mozillazg/phrase-pinyin-data/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/LICENSE) |

2026-10-08 通过第一方 GitHub commits API 取得上述 SHA。单字格式为 Unicode code point 与逗号分隔候选拼音；词语格式为词语与逐字空格分隔拼音。原生查询优先确定的最长词语，再取单字默认读音；同项多个读音保持稳定选择，用户手工音素可以覆盖。不调用这些仓库的数据生成脚本，不安装 Python；部署固定数据与对应许可，记录哈希。语境分词/多音字效果应独立测试，不能声称与 OpenUtau Pinyin 实现所有输入逐字等价。保留 ü 与去声调规则应按声库实际 grapheme 验证。

A2 部署文件名及 SHA-256（configure 和下载脚本均校验）：

| 文件 | SHA-256 |
| --- | --- |
| data/pinyin.txt | `621f8ca9eff8519f47e2b17b564fd318161e13bca07eea8c8e04993cd5d3b52e` |
| data/phrases.txt | `dcc769607c220b312fea3e71cb63421298b4b891b1f7356a95ab58f2c96fff81` |
| data/LICENSE-pinyin-data | `9c048697be2502a16e8bcb282d5d465a07295b2def0ffb05a269c5d39dbe1586` |
| data/LICENSE-phrase-pinyin-data | `89ac55df747e4776088c3e77531ef61b973a1a59dd8e6a4548a58996da9a4f70` |

原生字典/word grouping 移植保留 `licenses/tlds-MIT.txt`。普通配置仍限制 4 MiB/100k YAML 节点；真实英文词典约 13.5 MB，字典单独限制 32 MiB/4M 节点，按所选语言延迟加载。六包中文自动歌词是 A2 验收目标；en/ja/ko 保持声库语言选项，随包词典和明确读音可用，词典未命中且没有对应原生自动算法时返回诊断，不声称具有完整多语言 G2P。

未选用 [cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin)：它使用 C++17、额外字典资源，并采用 [Apache-2.0](https://raw.githubusercontent.com/wolfgitpr/cpp-pinyin/main/LICENSE)，不是 MIT；其 README 的覆盖范围限制为 U+4E00–U+9FFF。上述 native 数据查询满足当前中文 fixture 需求，无需把它加入本轮依赖。未选用 Python/.NET G2P，也不加载任意声库 C# 音素器。

## 缺依赖及独立性验收边界

- 显式启用引擎而缺 ORT include/import library、JSON/YAML 开发依赖，应在 configure 给出组件名和要求版本；不得悄悄编译成无推理占位。
- 未安装任何声库应仍能协商引擎/显示零声库状态。声库缺文件与引擎运行库缺文件分开诊断。
- ORT DLL 缺失/版本不符应报告无法加载 native runtime；模型不支持、坏张量、缺词典应报告对应模型/角色/配置路径，不尝试其它语言或后端掩盖。
- A0 骨架不提供已完成 G2P/合成的虚假 capability。A2 引入固定拼音数据后，其缺失应有组件名/路径诊断和负向用例。
- Release/独立 SDK 不依赖 `refs`、TuneLab/OpenUtau 安装目录、.NET、Python、本机图片；模型、图片及模型专属词典只从用户配置的外部声库读取。
- A0 版本/许可冻结不代替 A2 真实 duration、A3 六包全链推理与 A4 安装验证。
