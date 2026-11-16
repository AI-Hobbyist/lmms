# M2 API 与附件阶段验收记录

日期：2026-11-16。依据根目录实施计划书第6、7、11节。本阶段实现及自动验证完成；真实厂商验收按下表保留MANUAL/PENDING，不计为PASS，不阻塞阶段提交。M3内部工具执行、M4搜索/下载和M5整体DAW/ZIP尚未实施。

## 自动验证

- Release开发程序：`build/Release/lmms.exe`。本轮增量8/8源文件成功；全部8项Chat目标随后更新/重链接成功（没有待编译CPP）。QM4131 finished、0 unfinished。
- CTest：8/8通过，13.24秒。QtTest合计71 passed、0 failed、0 skipped（含初始化/清理）。日志`build/agent-chat-m2-checkpoint-{build,tests}.log`；重试专项`build/agent-chat-m2-retry-{build,tests}.log`。
- ChatStoreTest：会话/设置存储、导出及提示词路径。
- ChatConnectionTest：公开配置与Windows原生凭据、导出无Key、损坏配置不覆盖、非法连接及中文翻译。
- ChatEventStreamTest：UTF-8跨块、CR/LF/CRLF、BOM、多data、未终止事件。
- ChatRequestTest / ChatResponseTest：八格式请求/事件/JSON、温度与模型思考参数、认证位置、Azure地址、并行工具参数完整性、原生状态来源隔离、自定义模板/错误映射。
- ChatProviderClientTest：本机HTTP异步POST、SSE/普通JSON、取消/错误脱敏、会话绑定/删除、附件实际字节与历史快照、恢复后原生签名回传、八格式预设工具结果往返、连续失败后重试恢复。夹具工具结果没有执行LMMS工具。
- ChatAttachmentTest：实际UTF-8文件/MIDI本地引用、七原生协议PNG/PDF字节编码、Chat及Gemini WAV、能力/大小/读取错误。PDF夹具仅验证编码，不验证厂商阅读PDF能力。
- ChatDockTest：真实Windows Qt窗口，保存/恢复、关键Penpot坐标、设置页、模型/思考选择、附件移除与发送、流式上滚保持、切换会话、停止/销毁取消、错误后真实鼠标点击重试。无offscreen/Computer Use。

## 协议与外部验收矩阵

| 协议 | 本机请求、SSE/JSON及预设工具结果续接 | 实际厂商API Key调用 |
| --- | --- | --- |
| OpenAI Chat | PASS | MANUAL/PENDING（未提供Key） |
| OpenAI Responses | PASS | MANUAL/PENDING（未提供Key） |
| Anthropic Messages | PASS | MANUAL/PENDING（未提供Key） |
| Gemini generateContent | PASS | MANUAL/PENDING（未提供Key） |
| Azure Chat | PASS | MANUAL/PENDING（未提供部署/Key） |
| Azure Responses | PASS | MANUAL/PENDING（未提供部署/Key） |
| 通用OpenAI兼容 | PASS | MANUAL/PENDING（未提供具体服务/Key） |
| 自定义HTTP JSON | PASS | MANUAL/PENDING（未提供具体端点/Key） |

厂商预设16项和参考版本/许可证来源见`chat-provider-sources.md`。预设可修改、不覆盖用户连接；模型能力明确配置，未知模型不靠名称猜参数。

## 附件边界及错误重试

文本按UTF-8实际读取；MIDI明确本地引用且未上传/解析。图像为PNG/JPEG/WebP/GIF；结构化文件当前实现PDF；Chat音频为WAV/MP3，Gemini音频经原生inlineData。结构化附件须同时满足协议和模型inputTypes以及可选inputMimeTypes；其他类型明确不支持，不静默丢弃。自定义HTTP当前使用文本及明确的OpenAI工具历史映射，不猜任意结构化附件格式。进程内联上限64MiB，可配置更低上限。

失败且输入/附件草稿为空时，现有发送按钮成为重试入口，底部说明及无障碍名称同步；不新增未设计按钮或改变几何。重试保留原用户消息和失败尝试，只追加retryOf关联的新assistant尝试；已发送附件复用内容快照，不重新读取旧路径。缺快照时提示重新选文件；成功/取消记录不作为失败重试。网络错误不会自动重放。

## Penpot与真实窗口

实时连接正确file/page并读取01/11/13；本阶段API/预设/高级/生成/附件等布局此前分别按对应实时画板核对。沿用LMMS ToolButton、Qt Widgets、系统字体及默认主题SVG；没有独立Web UI。真实截图见本目录chat-api-native.png、chat-presets-native.png、chat-advanced-native.png、chat-generation-native.png、chat-reasoning-native.png、chat-attachments-native.png、chat-transcript-native.png、chat-attachment-chip-native.png、chat-send-native.png。当前build/agent-chat-send-native.png已再次查看，中文与主题正常。完整DAW内集成、实际厂商语义和多显示器实机验收MANUAL/PENDING。

生产安装未修改；没有本阶段ZIP。插件未因聊天改动重建或迁移。本记录仅关闭M2范围，不表示整个持续目标完成。

阶段实现提交：ad36183547f480882acd5db81cdf59d28c9cf1a3；已推送origin/master，git ls-remote与本地一致。M2检查点DONE。
