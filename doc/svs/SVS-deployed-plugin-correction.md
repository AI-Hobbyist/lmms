# SVSExample 开发部署目录纠正

2026-10-07。用户报告新建音符后 Failed (2)、音素块消失、只读结果仅一条且无值。

## 已确认原因与修复

直接读取运行中开发版进程的模块列表，确认 `build/Release/lmms.exe` 加载 `build/Release/svs/SVSExample/SVSExample.dll`。该 DLL 时间为 21:48:15，上一轮编译／测试却使用 `build/svs/SVSExample/SVSExample.dll`（22:38:36）。上一轮的自动验证不能证明实际开发版完成插件更新。这是部署验证遗漏。

在实际部署目录运行 readOnlyReferenceNativeWindow 可稳定复现缺少 example.level：3 passed / 1 failed。该目录旧版仅有 example.energy。其标量数值仅在合成成功后返回，范围为 0–1000000，不适合作为有明显填充效果的曲线示例。

Windows LMMS 工程内的 SVSExample 现直接输出到既有 `$<CONFIG>/svs/SVSExample`，与开发版可执行文件相邻；CTest 使用目标文件目录的父目录查找插件，避免手写第二套路径。独立 SDK 示例构建规则保持原行为。没有新建或替换 build 目录，也没有改变普通原生插件的输出。过去误用的 build/svs/SVSExample/SVSExample.dll 已原位改名为 .dll.disabled，防止继续误测旧输出。

用户授权直接强制关闭旧开发版；执行前原进程已退出。未保存工程没有被擅自保存。当前没有残留测试窗口。

## 验证及尚未确认部分

- 前台 PowerShell 构建通过：lmms、SVSExample、SVSIntegrationTest、既有 UiPluginCoverage；启用的原生插件仍输出到 build/Release/plugins。
- 新增 generatedPhonemeDragRenders：真实 Windows 窗口中通过铅笔输入两个不同音高音符，每次输入后检查 Ready；使用自动生成音素分别拖动两音符的交界，检查四个音素反馈及再次合成成功；单个音素增益可单独修改并再次合成。
- 实际部署目录、开发主题原生专项：4 passed / 0 failed / 0 skipped。两条 Level／Peak 有值、半透明填充、分别开关正常，截图已检查。
- 修复后的完整 SVS CTest：1/1 通过，原生 Windows Qt；CTest 环境来自同一插件目标目录。

原始截图中的 Failed (2) 表示 SVS_INVALID_INPUT。旧 DLL 的直接双音符创建／音素调整测试也通过；本轮未稳定复现该失败，不能声称目录修复已经证明其全部根因。无反馈时音素条目前只有来自模型的手动音素，自动音素需合成成功才能显示；此次不通过隐藏错误或强行保留失效音频绕过问题。更新后真实用户工程中的原始报错仍需人工复核（MANUAL/PENDING）。

证据：validation/SVS-deployed-render-repro.txt、SVS-deployed-phoneme-repro.txt、SVS-deployed-native.txt、SVS-deployed-reference-on.png、SVS-deployed-reference-off.png、SVS-deployed-final-build.log、SVS-deployed-ctest.log。

既有测试宿主加载普通插件的诊断、JACK server 未运行的信息仅记录 follow-up，不修改相邻插件或 Audio Engine。未使用 offscreen 或 Computer Use。
