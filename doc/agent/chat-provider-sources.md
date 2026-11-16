# M2 接口与厂商预设依据

更新：2026-11-16。M2实施中，本文件记录事实来源，不表示所有协议或厂商已经实现/实测。

- Cherry Studio参考版本：`c53a5e02941bc2f35d3270dc73a33b13da82df15`，文件[provider registry](https://github.com/CherryHQ/cherry-studio/blob/c53a5e02941bc2f35d3270dc73a33b13da82df15/packages/provider-registry/data/providers.json)。已核对项目[AGPL-3.0许可证](https://github.com/CherryHQ/cherry-studio/blob/c53a5e02941bc2f35d3270dc73a33b13da82df15/LICENSE)。只将厂商名称、地址和协议作为配置事实参考，自建精简结构，不导入其适配器、能力映射或应用实现。
- DeepSeek[首次请求文档](https://api-docs.deepseek.com/)当前列出OpenAI格式基址`https://api.deepseek.com`，请求路径`/chat/completions`，Bearer Key。当前示例模型为`deepseek-flash`；不能将Penpot中的旧示例模型ID当作服务端模型清单。模型列表与手工ID仍须由实际接口/用户配置确定。
- Anthropic[API overview](https://platform.claude.com/docs/en/api/overview)：原生Messages与Models接口。完整请求字段、版本头和流事件在实现适配器时另行核对。
- Gemini[List Models](https://aistudio.google.com/api-reference/models/list-models)：`/v1beta/models`，需要处理实际分页；不制造假模型列表。
- Windows Key持久化使用[CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)、[CredReadW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw)、CredDeleteW。凭据目标按应用配置目录哈希与连接UUID隔离；普通JSON仅存连接字段，写入失败报Windows错误码，不回退明文。

后续每项协议实现须补请求、流事件、附件、思考参数和真实验证状态；夹具或本机凭据测试不能代替真实厂商调用。

API基础页面已内置16个精简预设，用户可编辑为自己的连接；厂商更新不重写已保存配置。腾讯混元地址另核对[腾讯云官方OpenAI兼容文档](https://cloud.tencent.com/document/product/1729/111007)。厂商品牌图标未复制，统一复用LMMS主题Key SVG。

模型列表基础实现另参考[Anthropic Models](https://platform.claude.com/docs/en/api/models/list)、[DeepSeek Models](https://api-docs.deepseek.com/api/list-models/)、[Gemini Key](https://aistudio.google.com/docs/api-key)及[Azure API参考](https://learn.microsoft.com/en-us/azure/foundry/openai/reference)。当前Anthropic版本头默认2026-10-21；Gemini与Anthropic保留各自分页参数。现阶段只完成本机OpenAI格式HTTP列表验证，其他格式及真实服务仍需后续验证。

SSE分帧依据[WHATWG Server-sent events](https://html.spec.whatwg.org/multipage/server-sent-events.html#parsing-an-event-stream)：UTF-8、三种换行、data拼接及空行派发；EOF不派发缺少结束空行的事件。此处实现分帧，不实现EventSource自动重连，也不重放模型生成。

文本请求和流事件另核对[OpenAI Chat请求](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)、[Responses请求](https://developers.openai.com/api/reference/resources/responses/methods/create)及[Responses流事件](https://developers.openai.com/api/reference/resources/responses/streaming-events)、[Claude流事件](https://platform.claude.com/docs/en/build-with-claude/streaming)、[Gemini生成响应](https://aistudio.google.com/api-reference/generate-content)、[Azure Responses参考](https://learn.microsoft.com/en-us/rest/api/aifoundry/azureopenai/responses)。当前出站只完成文本构造，入站完成基础SSE事件及工具参数收集；网络验证仅本机OpenAI格式夹具。签名/原生内容回传、附件、工具完整往返和普通JSON响应仍需M2后续完成。

附件编码核对[OpenAI图像输入](https://developers.openai.com/api/docs/guides/images-vision)、[OpenAI文件输入](https://developers.openai.com/api/docs/guides/file-inputs)、[Claude图像输入](https://platform.claude.com/docs/en/build-with-claude/vision)、[Gemini文件输入](https://aistudio.google.com/docs/file-input-methods)。新增内容编码为本地准备的base64/文本到各协议原生parts；截至本次仅测试本地文件与请求对象，未接附件UI/实际上传。PDF等未测分支仍待M2后续验证，不能将读取/编码当网络上传完成。


普通JSON响应另核对[OpenAI Chat对象](https://developers.openai.com/api/reference/resources/chat)、[Responses JSON输出](https://developers.openai.com/api/reference/resources/responses/methods/create.md)、[Claude Messages响应与stream](https://platform.claude.com/docs/en/api/http/messages/create)、[Gemini generateContent与streamGenerateContent](https://aistudio.google.com/api-reference/generate-content)。这些官方字段用于独立实现，不复制SDK实现。上述早期“仅文本/无附件UI/JSON待实现”为历史状态：现已接通附件UI/异步读取、PNG真实HTTP字节和历史快照，PDF七协议/Gemini WAV编码断言通过；普通JSON八格式本机HTTP回归通过。真实厂商API、原生签名和完整工具回合仍未验收。

高级JSON配置示例（mapping对象内）：`{"stream":false,"path":"generate","responseTextPath":"result.answer"}`。自定义完整JSON可省略完成路径；SSE须配置`responseDonePath`，可用`responseDoneValue`指定结束值。该映射不执行任何模型返回的工具，也不修改批准设置。


原生思考状态回传核对[Claude多轮及工具工作流](https://platform.claude.com/docs/en/build-with-claude/thinking-tool-workflows)、[Gemini thinking与generateContent签名位置](https://aistudio.google.com/docs/thinking)、[OpenAI stateless reasoning](https://developers.openai.com/api/docs/guides/reasoning)。当前官方Responses文档说明store:false默认返回encrypted_content，无需新增legacy include参数；保持既有store:false，不引入不必要参数。实现保留原生块及来源绑定，不解密或把不透明状态显示成思考文本。Claude/Gemini/Responses恢复后常规下一轮的本机HTTP回传已测，真实服务/完整工具结果往返尚待完成。


工具结果传输核对[OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling)、[Claude tool result格式及顺序](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls)、[Gemini generateContent FunctionCall/FunctionResponse字段](https://aistudio.google.com/api-reference/generate-content)。Gemini的服务端id可缺省，不能把程序内部合成编号冒充服务端返回值；结果的name和结构化response按schema保留。Claude多结果放在同一user消息且文本在结果后。当前测试覆盖8格式本机HTTP调用→预设结果→最终回复的传输，内部工具批准/执行仍属于未开始的M3。

自定义工具历史示例：mapping添加`"toolHistoryFormat":"openai-chat","responseToolCallsPath":"calls"`。响应calls须为完整数组快照，如`[{"id":"call-1","name":"tool_name","arguments":{}}]`，不是OpenAI字符串arguments或未知厂商delta。实际工具入口和schema将在M3接线，本配置不会执行返回调用。

自定义请求模板（高级 mapping 配置，M2）：

```json
{
  "path": "generate",
  "stream": false,
  "requestTemplate": {
    "payload": {"history": "${messages}"},
    "options": {"model_id": "${model}", "streaming": "${stream}"}
  },
  "responseTextPath": "result.text",
  "responseDonePath": "result.done",
  "responseErrorPath": "fault.detail"
}
```

requestTemplate为完整出站JSON对象，替代默认正文；不配置时保留原有字段重命名方式。占位符必须占据整个字符串值，引用模板应用前的请求字段（model/messages/stream，以及实际启用的temperature、思考参数），保留数组、对象、数值和布尔类型。必须包含${messages}；其中已有system消息、对话和工具结果，不再递归解释消息里的占位符。模板常量保持原样，不运行任何脚本，也不做字符串拼接。温度或思考参数启用时，模板需显式引用对应字段以发送它们。API Key在模板应用后按认证配置写入，不作为模板变量。

responseErrorPath默认error，可使用对象字段及数组索引的点路径，例如failures.0.message。指向字符串或含message字符串的对象；缺失、null、false和空字符串表示没有错误，其他类型报告映射错误。普通JSON和SSE都先检查错误，再读取文本/完成状态；配置路径不允许为空。协议错误仍经过现有Key脱敏。自定义字段含点号的键名不支持转义；不将未知路径语法当脚本执行。
