# DiffSinger A2 验收

状态：PASS。范围为原生发音、真实 duration、手工音素和说话人；完整音频属于 A3。

## 实现

- 原生汉字/词语查询冻结的 MIT 拼音数据，去声调、ü→v、最长词语优先及行尾注释处理。六包默认中文音素器映射到原生实现，不加载 C#；未知字符/读音/音素器明确诊断。
- 按 `tlds_ref` 专属字典、语言字典、`dsdict-zh-<lang>`、公共字典顺序读取 entries/types/replacements；支持精确及小写查词。保留声库所有语言选项，自动 G2P 支持独立诊断。真实大英文词典采用单独有界读取、按语言延迟加载。
- acoustic/duration 分别使用自身词表和语言 ID；CPU 模型读取实际 ports，检查名称、dtype、rank、固定维、字节数、finite 和输出长度，不将 acoustic ID 传给 duration。
- duration linguistic 输入 tokens/word_div/word_dur，duration 输入 encoder_out/x_masks/ph_midi，以及模型实际要求的可选条件。按前置辅音、主体、休止、`-` 相邻延续和 `+` 多音节延续分组，最小 5ms、最多提前 150ms。显式手工 segments 固定实际时间、空列表表示用户静音；自动结果不写入用户 segments，移除覆盖恢复自动。
- 时间转换使用共享 `svs_time.hpp`。自动邻接段避让手工边界；用户静音占据边界时，下一辅音在其音符内保持最小时长。非法重叠/太短/越界有诊断。
- 稳定 speaker/subbank 枚举使用现有轨道插件参数；模型需要 spk_embed 时读取对应阶段 embedding，验证 hidden_size/finite/授权路径，按显式权重归一化。六包只有芙宁娜声明一个 subbank，实际 12 个 duration 模型没有 speaker 输入；多嵌入选择/权重采用独立 fixture，不冒充六包多说话人推理证据。
- session 拷贝提交的全部输入，保留工程词典优先结果及手工字段，取消和退出同步；A2 render 返回真实 duration 反馈和 A3 尚未实现的明确错误，PCM 指针为空，不制造示例音频。

## 自动证据

| 验收 | 结果 | 证据 |
| --- | --- | --- |
| 六包中文歌词、词语多音字/ü、真实 12 个 duration ONNX、finite/时间/延续/休止/覆盖/重置/最小时长/取消 | PASS | validation/A2-native-complete.log |
| 原位 DLL catalog/语言/发音/session 深拷贝/工程词典优先/取消/结果释放 | PASS | 同上 |
| 真实 13.5 MB 英文词典及多 speaker embedding fixture | PASS | 同上 |
| 原生 Windows Qt 发音/四语言/说话人枚举与工程保存重开；既有首尾伸缩/短音素/歌词操作 | PASS，6 passed/0 failed | validation/A2-host-native-QtTest.txt |
| 独立 SDK 原目录构建及 native duration/ABI | PASS | validation/A2-independent-sdk-build.log / A2-independent-sdk-native.log |
| 完整文件夹中12个引擎/依赖/数据/许可证文件及哈希 | PASS | validation/A2-deployed-manifest.json / A2-deployed-package.log |

GUI 使用正常 Windows Qt 窗口、开发主题；截图 `validation/A2-native-speaker.png` 已检查文字/枚举正常，不包含外部声库美术。没有使用 offscreen，测试窗口正常关闭。日志中的普通 instrument DLL 警告与既有 harness 相同，完整普通插件部署验收留 A4；未为此修改相关模块。

## 部署与边界

开发包保持 `build/Release/svs/SVSDiffSinger/`，包含引擎、原生 ORT DLL、manifest、data 和许可证；主程序仍为 `build/Release/lmms.exe`，普通插件仍位于 `build/Release/plugins`。用户补充的整个文件夹交付已纳入 A4 安装验收；音频和 `.tensor` 统一到 `cache/SVS/DiffSinger` 在 A3 缓存接入时验证。声库资源不复制入交付。

遇到的实际问题：大英文词典超过配置上限，改为独立词典限额和延迟加载；公开拼音数据行尾注释误当读音，按格式剔除注释；手工静音与下一辅音相邻约束，保留手工边界并在下一音符内满足最小时长。日志保留修复与复测证据。工作区其他 Song/VST 修改不纳入本阶段。

规定自动验收已通过，更新计划后检查 git status/diff，只提交 A2 文件并推送；远端确认后才开始 A3。
