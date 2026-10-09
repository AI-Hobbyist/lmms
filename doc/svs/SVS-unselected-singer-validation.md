# SVS 未选定歌手与直接导入验收

2026-10-09。范围仅为未选定歌手、宿主正弦波试听、原文音素条及取消导入时的声库选择。

## 行为

- 导入选项窗口取消默认说话人选择。所有歌声轨以空 pluginId/voiceId 导入，不依赖扫描或安装引擎；原转换选项、损失确认及原工程保护保持原流程。
- 编辑器与插件属性中的歌手选择首项为“未选定”，可主动清除绑定。该状态没有注册声库，不加入 Registry 或左侧引擎目录。
- 未选定时在既有异步渲染队列中生成双声道正弦波，按音符音高、长度及时间映射发声；空拍为零，首尾 5 ms 淡入淡出。支持既有取消、静音与过期结果保护，不调用 SVS SDK/AI 模型。
- 状态和侧栏显示“请选择一个歌手”；音素条即时显示各音符的歌词原文，不读取旧歌手音素反馈。选定歌手后恢复引擎解析。
- 明确绑定但缺失的引擎/声库不被替换成预览；原缺失诊断及缓存恢复分支保留。没有新增插件 SDK ABI。

## 验收

- 原目录前台 PowerShell 构建 `lmms SVSIntegrationTest` 成功；主程序 `build/Release/lmms.exe`，测试窗口来自 `build/tests/Release/lmms.exe`。
- 原生 Windows Qt 窗口：`unselectedSingerPreview projectImportNoVoices projectImportNativeWindows`，5 passed / 0 failed / 0 skipped。验证导入空绑定、未选定选项/提示、选择歌手再清除、缺失绑定不会预览、440 Hz 正弦波、左右声道一致及空拍静音。
- 空引擎目录单独运行 `unselectedSingerPreview`，3 passed / 0 failed / 0 skipped；确认为宿主预览，不依赖示例插件。
- 真实窗口截图 `validation/SVS-unselected-singer-native.png` 已查看，中文正常，音素条显示“你好”。没有使用 offscreen；测试窗口已关闭。
- 测试构建重新生成的 SVSExample DLL/manifest 已移回现有 `build/SVSExample/Release` 参考目录；正式部署继续遵循已完成的示例引擎退休要求。
- 试听缓存：`build/Release/lmms-workspace/cache/SVS/Unknown/9089adfcf8c7eeb91d8052c4e7d4e254420d0a0ea6beb5a4ec47c24a22e2e16c.wav`，352844 bytes；实际 SHA256 与文件名一致。这是测试保存的音频副本，预览生产路径保持内存生成。
- `git diff --check` 通过。保留并行任务的已提交改动，仅提交本功能差异。

## 额外检查 / 后续问题

旧 `missingPluginCachedPlayback` 用例未通过：继承嵌入 GUI 环境时，子进程指定空资源目录导致启动失败；取消该 GUI 环境后，子进程在 `cachedRestoreWithoutPlugin` 恢复缓存音频断言失败。此处是明确绑定的缺失插件分支，未进入新预览路径；本次未改缓存恢复，未宣称该附加检查通过。日志为 `validation/SVS-unselected-existing-cache-failure.log`，记录待单独诊断。
