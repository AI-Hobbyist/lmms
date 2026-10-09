# SVS 声库说明入口验收

2026-10-10。仅实现声库 README 读取入口及对应中、日、英、韩词条。

- SVS 钢琴窗歌手侧栏新增说明按钮，中文“声库说明”、日文“音声ライブラリの説明”、英文“Voicebank README”、韩文“음성 라이브러리 설명”。标题及提示共 5 条词条加入四种 LMMS TS 文件，编译部署对应 QM。
- 当前声库目录识别 `readme.md` 和 `readme.txt`，大小写不敏感；两者存在时分标签查看。无声库目录或无文件时按钮不可用，不误读引擎插件自身的 README。
- 复用工程备注所用的 Qt QTextEdit 文本控件，设为只读；Markdown 排版，TXT 保留原文，QTextStream 默认 UTF-8 并识别 Unicode BOM。没有复用工程备注的工程保存行为，也没有另改工程备注。
- DiffSinger 声库目录声明增加可选 `voicebankPath`，宿主从当前 Voice 的目录元数据读取说明；其他引擎提供该目录字段即可使用相同入口，无 ABI 改动。
- 原有构建目录前台构建成功，成品 `build/Release/lmms.exe`、`build/Release/svs/SVSDiffSinger/SVSDiffSinger.dll`；编译前确认其他同工程任务 idle。
- `voiceReadmeNative` 真实 Windows Qt 窗口验证 3 passed / 0 failed / 0 skipped：混合大小写、双格式、中文、只读、TXT 原文、无目录、四种已部署 QM 的按钮翻译。截图 `validation/SVS-voice-readme-native.png` 已检查，中文正常，窗口已关闭，无 offscreen。
- `git diff --check` 通过。范围仅为入口、读取辅助代码、DiffSinger 目录声明、词条和对应验证。既有启动时 JACK/TLS/图标提示未扩张修复。
