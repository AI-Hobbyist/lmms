# DiffSinger A3 验收

状态：PASS，规定自动验收已通过。范围为原生 CPU 全链、有值反馈、时间协议、受限分块和缓存。A4 的最终 Release 构建、完整既有回归及用户补充的音域/前缀/拼音显示尚未实施。

## 实现与边界

- 六个真实声库使用原生 duration → pitch linguistic/predictor → variance linguistic/predictor → acoustic → vocoder，按照各阶段实际端口和自身词表/语言表输入。单声道模型结果重采样为宿主请求的 float32 stereo；验证有限值、非静音、非静态及结果长度。
- 音高使用 fractional MIDI/Hz 转换；手工音高覆盖及偏差参与真实声学输入。breathiness/voicing 的实际声学单位为 dB，tension 为 ratio；预测值加声明的偏差，显式绝对值覆盖后按声明范围限制。当前六包没有声明其他 voicing codec，不把 dB 当作线性幅值。仅声学接收的参数可编辑，仅真实预测输出声明只读参考曲线。
- 对照 tlds_ref 的 use_*_embed/predict_* 条件，六包提供 Breathiness、Voicing、Tension 三条只读回显，Energy 条件不满足时不伪造。回显反映预测、偏差和绝对覆盖后的实际值，与用户曲线分别存储，各自显隐；默认颜色沿用参考声明。
- 共享 SDK 的 tempo/curve 插值用于冻结 tempoMap、fractional tick 和曲线间断；音素采用累计帧边界修正，前后各 0.5 秒上下文。PCM start_seconds 为全局秒，音高/音素反馈为内容局部秒，variance 曲线为内容局部 tick。position−contentOffset 是内容原点。
- 单个连续模型上下文限制为 4096 帧。超过时只在至少 1 秒的真实空隙/显式 SP 休止处分块，为两侧保留各 0.5 秒 padding；独立帧网格重采样后按全局样本位置放置，间隔补零、参考曲线标注 gaps。没有合法边界的超长连续句明确报错，不任意切断发音。总帧数不超过 60000，PCM 不超过宿主既有 16M 帧，并在推理前检查；无无限结果缓冲。
- ORT CPU 顺序执行、最多四个 intra-op 线程；每个 render session 延迟加载且只持有所需阶段模型，释放结果和销毁 session 在所属模块内完成。取消在模型阶段前后、缓存边界及重采样循环检查，不声称可立即中断正在执行的单次 ORT Run。错误包含阶段、声库路径，模型错误包含具体 stage/role/model path/port。
- 默认 seed=1（native.v2），已有 seed 保留并验证 uint32。只在内存中按 ONNX protobuf v1.19.0 的字段设置标准随机算子 seed；不改声库 ONNX 文件。随机 session 在运行前重置，使同输入同 seed 与先前运行次数无关；非随机模型复用。seed、运行时、模型/配置指纹和完整输入 tensor 进入缓存身份。

## 缓存与试听

宿主缓存根为 LMMS workingDir 下的 `cache/SVS/<engine>`，DiffSinger 为 `cache/SVS/DiffSinger`；同一路径冻结传给原生插件。路径及声库搜索目录不影响音频身份，声库实际内容指纹仍参与身份。

- 最终音频为 `<完整 WAV 文件字节的 SHA-256>.wav`。输入身份使用独立 `.svsmeta` 索引保存反馈和映射；相同音频内容共用 WAV，音符 ID 回绑到当前工程。读取校验文件 SHA、WAV 格式、长度和 finite，损坏成为 miss 并可重新生成。内存及磁盘预算保持有界；只清理自己拥有的索引及无引用 WAV。
- `.tensor` 使用完整 64 位 SHA-256 名称，身份包含模型/声库/config/runtime/seed、输入名称/dtype/shape/全部字节；另有内部 payload 校验。损坏/未知格式成为 miss，原子写入、有界 512 MiB LRU，不清理 WAV 或其他文件。
- 旧引擎目录及既有 `svs-v1` 的 `.svscache` 只读兼容，不批量删除。没有缓存写权限时推理仍可工作。
- 六包 CPU 试听各保留一份在工作区 `cache/SVS/DiffSinger`，PCM16 WAV 也按完整文件 SHA 命名；不归一化、不用示例振荡器替代，私人声库和试听文件不提交进源码。

## 自动证据

| 验收 | 证据 |
| --- | --- |
| 六包真实全链、有限/非静音/非静态 PCM、实际手工音高/音素/控制量改变输出、同 seed 无缓存重算一致、取消 | `validation/A3-six-final-chain.log` |
| seed 改变及恢复、自然空隙和显式长休止分块、冻结变速 tempo 与非零内容原点、曲线 gap、PCM 预检查 | 同上 |
| tensor 格式/输入身份/损坏恢复/LRU/不删除试听 | 同上 |
| 宿主音频 SHA 文件名、缓存损坏后磁盘恢复、旧格式读取、实际 PCM/反馈、播放导出与保存重开 | `validation/A3-final-host-QtTest.txt`（5 passed/0 failed）、`A3-final-host-test.log` |
| 原位插件完整公开 ABI/session/输入拷贝/反馈/取消/释放及既有 duration 行为 | `validation/A3-final-duration-ABI.log` |
| 默认主题 Windows 实窗三条只读曲线和拒绝绘制修改 | `validation/A3-reference-gui-QtTest.txt`（3 passed/0 failed） |
| 最终阶段构建 | `validation/A3-final-native-build.log`、`A3-bounded-chain-build.log` |

实窗截图 `A3-native-reference-breathiness.png`、`A3-native-reference-voicing.png`、`A3-native-reference-tension.png` 已逐张检查并发送用户：三条有值曲线、独立选中、中文正常，窗口关闭。截图包含用户声库美术，仅本地保留；不随源码/插件分发。没有使用 Qt offscreen。测试 harness 的普通 instrument DLL 加载警告留待 A4 完整原位 Release 构建验证，不为其重构普通插件。

## 修复记录与后续范围

新增分块回显测试两次稳定复现原生 access violation；故障定位到 ordered_json 插入 gaps 后 vector 扩容使旧 points 引用失效。先补齐字段再取引用，原始分块 PCM/反馈用例和六包矩阵通过；诊断日志 `A3-bounds-diagnostic.log`、修复复测 `A3-six-bounded-chain-fixed.log` 保留，临时诊断代码已清理。

用户已补充 A4 验收：comfort.json 的可用/舒适/弱点音域琴键明暗及侧栏文字，无文件保持默认；插件全局音素语言前缀开关；音符上方拼音、内部原歌词。按照阶段检查点，A3 提交推送并确认后才开始这些 A4 改动。B0～B4 仍未授权实施。
