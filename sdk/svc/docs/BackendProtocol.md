# SVC SDK 后端参考协议

日期：2026-10-09。状态：SDK 设计基线，尚无本轮实现。配套 [LMMS SVC API 支持计划书](../../../LMMS%20SVC%20API支持计划书.md)。

## 1. 标准来源与适用范围

本项目以 `D:\AI-Tools\RVC20260718Nvidia50x0\rvc_api` 当前 API 定义为后端参考标准。已核对 README.md、app.py、schemas.py、registry.py、protocol.py 和 client.py；本文件将协议写入 SVC SDK，使实现者不必只依赖机器上的绝对路径。后续实现需固定该参考快照，发生差异时记录版本变更，不悄悄修改契约。

通用 SDK 提供能力发现、输入字节流、输出音频块、进度、终态、取消和错误抽象。RVC profile 采用本文列出的 HTTP 路由与 multipart/mixed 事件。RVC 专有字段不成为所有引擎的固定旋钮；其他引擎通过适配器提供统一描述。本文后半的前端分块/缓存/区间发布是 LMMS 新增宿主契约，不冒充现有 RVC 已实现的功能。

## 2. 连接与发现

每个引擎配置 API 基地址和可选 Bearer token。基地址不重复拼接 `/api/v1`；引擎适配层负责路由规范化。token 非空时使用 `Authorization: Bearer <token>`，为空时省略。token 不写入工程、缓存、URL、日志或命名材料。重定向不能泄漏到其他来源。

| 方法 | 路径 | 返回用途 |
| --- | --- | --- |
| GET | `/health` | 公开 alive/ready，不表示模型和鉴权检查已成功 |
| GET | `/api/v1/init` | models/models_state/issues、f0_methods、modes、parameters、auth_required、audio_transport、limits、gpu_policy |
| GET | `/api/v1/models` | 模型列表、扫描状态和问题 |
| GET | `/api/v1/models/{model_id}` | 指定模型、权重/索引候选及参数能力 |
| GET | `/api/v1/f0-methods` | F0 候选、available、reason 等 |
| GET | `/api/v1/modes` | processing_modes 与 index_modes |
| POST | `/api/v1/infer` | 原始音频字节请求流与音频/进度混合响应流 |

RVC token 开启时 `/api/v1/*` 均要求鉴权，公开 OpenAPI/docs 可关闭；因此 SDK 不依赖抓取公开 OpenAPI 来生成 UI。模型 ID 是模型文件夹名，weight_id/index_id 是发现返回的名称，不接受本地路径；中文按 URL 规则编码。模型权重有 `usable`、`speaker_count`、`supports_f0`、`sample_rate`、版本/特征维度、`compatible_indexes` 等描述。`has_index` 不等于当前权重存在可用兼容索引。

说话人列表由选定权重的 speaker_count 推导合法 ID `0..count-1`；缺少人名时只生成“说话人 ID”标签。多个权重必须显式选择；索引候选需按当前权重过滤。SDK 保存稳定 ID，不用下拉框行号持久化。发现接口中的 available 只是依赖/资源检查，推理仍可能失败。

## 3. 参数映射

以下数值描述当前参考服务，不替代运行时发现。旋钮范围、默认、依赖以 API/归一化 profile 为准；未返回的额外功能不凭空加入。

| 字段 | 当前参考语义 | 界面 |
| --- | --- | --- |
| model_id | 必填 | 模型下拉框 |
| weight_id/index_id | 多候选时明确选择；索引必须兼容 | 下拉框 |
| speaker_id | 默认 0，最大 speaker_count−1 | 说话人下拉框 |
| pitch_shift | 整数半音，默认 0，需 F0；当前声明无数值上下限 | 数值旋钮，不虚构 API 限制 |
| f0_method | 当前 pm/rmvpe/fcpe，默认 rmvpe，实际需 available | 下拉框 |
| index_rate | 0～1，可用索引时默认 0.75，否则 0 | 旋钮 |
| index_mode | auto/off/required；off 禁索引和非零 rate；required 需可用索引且 rate>0 | 下拉框 |
| resample_sr | 0 保留模型采样率，或整数 16000～48000 | 带“模型采样率”特殊值的数值旋钮 |
| rms_mix_rate | 0～1，默认 0.25 | 旋钮 |
| protect | 0～0.5，默认 0.33，需 F0 | 旋钮 |
| chunk_seconds | 1～30 s，默认 5，后端按 16k 的 160 样本边界处理 | 后端参数旋钮 |
| mode | 当前 chunked_file | 下拉框 |

未知、重复、非有限、越界参数被拒绝。非 F0 模型不执行 F0 提取，适配器不发送有意义的变调/保护修改。API 的描述字符串如 `0.75 if usable_index else 0` 由已知 RVC profile 和元数据解释，不执行来自网络的代码。SDK schema 支持可选范围、条件、稳定 ID、单位与默认值来源；未解析的新参数不得作为错误的可调项发送。

## 4. 音频分块上传

POST 的 Content-Type 为 `audio/*` 或 `application/octet-stream`，请求体是完整可解码音频文件的原始字节，增量发送，支持 HTTP chunked。不是 multipart/form-data、JSON 或 base64，也不是每个传输块一个 WAV。一个请求承载一个前端片段的完整 WAV 字节流；传输分块边界无音频语义。

当前 `chunked_file`：后端增量接收上传，上传结束后解码、预处理、F0，再逐块推理返回。不能宣称全双工实时麦克风转换。客户端使用有界 IO 缓冲和背压；不得先将整段输入/响应读入内存才处理。已知文件长度可以使用 Content-Length，但仍必须分块读写；兼容性测试应覆盖 HTTP chunked 上传。

前端通用切块规则不作为 query 字段发送，避免触发 unknown parameter 错误。RVC `chunk_seconds` 仅控制本请求内部推理块，与前端的 30 s 超长阈值/10 s 强制切块不同。

## 5. 响应与解析

HTTP 200 的 Content-Type 为 `multipart/mixed; boundary=rvc_<request_id>`。每个 part 必须按 `Content-Length` 读取正文，使用 `X-Event-Type` 分派事件。JSON 为 UTF-8，audio 为单声道 little-endian PCM16。每个 audio 不是独立 WAV；不能在二进制 PCM 正文中搜索 boundary。

事件顺序：`start → audio → progress → … → audio → progress → done → closing boundary`。失败以 error 取代成功 done。socket read/readyRead 的边界不等于 part 或 audio 块边界；头部和正文都可能被任意拆分或多块合并读取。

| 事件 | 字段与校验 |
| --- | --- |
| start | request_id、codec=pcm_s16le、sample_rate、channels=1、total、有效 parameters；只能一次，先于 audio |
| audio | X-Chunk-Index 从 1 连续递增；X-Sample-Offset 与已收样本数连续；X-Sample-Count 非负且有效；字节数必须等于 samples×2 |
| progress | request_id、stage=infer、current、total、status=running；紧随对应 audio，current 必须匹配已接收块数 |
| done | status=completed、current/total、samples、有效 parameters、gpu_cleanup_completed 和 CUDA 测量；数量与累计值一致 |
| error | status=failed、current/total、request_id、error.code/message 及可用清理信息；不得判为成功 |

解析器限制头部和 part 大小，检查算术溢出、重复关键头、非法长度/格式及请求 ID 不一致。参考客户端上限为头部 16 KiB、part 8 MiB，SDK 按 profile 固定并文档化有界限制。完整 audio part 校验通过立即发出 SDK 输出事件；终态前的事件为可试听的 provisional 数据。

仅当 done 成功、块数与样本数一致、GPU 清理成功（RVC profile）、closing boundary 完整且没有异常残余字节时，将该请求标记成功。HTTP 200、收到最后一块或正常 EOF 都不足以证明成功。无 done 的截断、重复/乱序块、偏移空洞、done 后多余 audio 均失败。

## 6. 错误、限额与取消

初始化前错误是结构化 JSON，可为 401 鉴权、413 超限、422 参数/音频/索引、429 忙碌、503 依赖/显存、408 超时；客户端同时显示 HTTP 状态、错误码和请求 ID，不暴露凭据。首块返回后发生错误仍可能保持 HTTP 200，必须处理 error 事件。

参考后端默认最大上传 100 MiB、最长解码 600 s，最短音频 0.1 s；限额尽量由 init 返回值驱动。该 profile 的未发现常量需标明来源，不能假定所有引擎相同。后端推理串行，忙时返回 429；音频队列有界并对慢客户端背压。SDK 顺序调度同一 RVC 服务，控制请求与内存数量，不把 429 变成紧循环重试。

取消通过中断上传/响应连接实现，停止后续前端片段；当前 API 没有任意位置断点续传或独立 cancel 路由。后端正在运行的 GPU kernel 完成后才清理，客户端不能确认断开就等于显存归零。失败/取消后的重试提交整个失败前端片段，不从任意 PCM 块继续。

## 7. 宿主分块与时间映射契约

固定通用可调项：silenceThresholdDbfs 默认 −70、lengthThresholdSeconds 默认 30、forcedChunkSeconds 默认 10。三者均可由用户设置；额外推理项来自后端 API。先按静音切，再对严格超长的片段按强制长度细分，保留时间轴与静音，不删除源样本。具体分析窗和短尾策略随版本固定，见计划书第 4 节。

一个前端片段可对应多个后端返回块。SDK 事件携带 generation/segment/request 身份、chunkIndex、输出采样率、offset/count、格式、有效区及终态。宿主以源起点加输出时间映射定位，裁去前端上下文；不同采样率使用有状态重采样和绝对边界，不能直接复用样本编号。

每完整有效 audio 块到达就发布相应可播放区间，更新波形和颜色；仅必要接缝窗口可等待后文，不能等 done 才整体替换。原始 A 永不覆盖；B 区间以不可变对象交换，实时线程无网络/磁盘等待。只听 B 的未就绪处静音；常规轨道渐进播放可在未就绪区用 A，两种行为明确区分。

失败前已发布部分保持 provisional/failed-partial，不能保存成完整成功结果；其他已完成请求保留有效。只在所有前端片段终态确认后宣告整次转换完成。进度包含前端 i/N、后端 j/M 和去重已替换时长；旧 generation 数据不计进度也不覆盖新结果。

## 8. 宿主缓存契约

目录固定为 `cache/svc/<安全引擎名>/input` 和 `output`。每个新前端转换片段生成随机 nonce，将其与输入摘要、模型/参数/分块配置等确定性材料共同进行 SHA-256。最终配对文件为 `input/<hash>.wav`、`output/<hash>.wav`，完全同名；随机数进入哈希前材料，不另附后缀。对 A/B 只计算一次共享身份，排他创建防止覆盖。

输入源内容不因 nonce 改变。相同音频再次转换可有新身份；工程重开复用保存的旧身份，不能通过重新随机计算找旧缓存。传输期间输出使用 `.wav.partial` 和已验证区间清单，实时播放从块快照读取；成功 done 及完整性验证后才原子发布最终同名 WAV。元数据记录原始偏移、上下文、格式、有效参数和各块状态，不含 token。

## 9. SDK 一致性验收

SDK 实现必须用可控参考后端覆盖：发现字段缺失/无效、动态模型和说话人、碎片化头部/正文、二进制包含 boundary 字节、乱序/重复/缺块、不同采样率、错误终态与截断、取消、背压及有界内存。RVC 适配另做真实 HTTP 和真实模型联调，确认首块返回后已替换且整体尚未完成。

还须验证随机命名的 A/B 同名、失败 partial 不升级、保存恢复、参数变化使旧事件失效；GUI 验证只用真实 Windows Qt 窗口。该协议文档是实施与测试依据，不是测试已通过的报告。
