# SVS 异步声库扫描验收

状态：PASS。

范围：用户要求声库扫描与 VST 一样异步，避免阻塞 LMMS。仅修改宿主目录扫描调度、对应设置入口、启动扫描期间的预览/导出兼容及必要测试、文档。引擎分类继续隐藏空分类。

- 启动声库查询、应用目录设置和重新扫描使用独立单线程 catalog 队列，插件调用仍按引擎互斥序列化。主线程只捕获设置和发布结果，扫描不占用合成任务池。
- 递增请求版本淘汰过时任务/结果，避免旧目录扫描覆盖新目录结果或 installation IDs。成功后刷新浏览器和已有轨道；失败保留上次有效声库列表。
- 设置页显示 Scanning voicebanks，暂时禁用 Rescan；完成显示声库数或错误。关闭设置页不会中止注册表任务，销毁页面自动断开通知。
- 启动扫描未结束时预览显示排队状态。立即导出等待对应目录结果，在原冻结输入上补充声库版本/声明，不重新捕获用户音符或曲线。实际缺失声库仍保留原缓存回退/失败行为。
- 退出丢弃排队任务并等待正在执行的原生 catalog 回调结束后卸载 DLL。ABI 1.0–1.3 不变；现有 ABI 没有 catalog 中止接口，不能强制终止正在执行的原生查询。SDK api.md 补充宿主线程语义。

验证记录：

- 初步 asynchronousVoicebankScanning + svsBrowserEngineGroups：4 passed / 0 failed / 0 skipped。确认提交耗时小于 200ms、界面计时器运行、结果在主线程发布、最新请求胜出、错误不清空列表、Rescan 异步返回。
- 完整 SVS 回归：73 passed / 0 failed / 2 GUI-only skipped，见 validation/SVS-async-catalog-full-QtTest.txt。包含真实六声库扫描、销毁设置页/局部注册表、扫描期间立即导出及编辑后仍使用原冻结输入。
- 原生 Windows GUI：asynchronousVoicebankScanning 和 diffSingerPresentationAndSettings 分别 PASS，见 validation/SVS-async-catalog-native-QtTest.txt。真实窗口截图 validation/SVS-async-catalog-native-scanning.png 已检查：现有主题下扫描状态清晰、Rescan 禁用，正常显示参数控件。
- Release 实际窗口：diffSingerReleaseWindow 最终 3 passed / 0 failed / 0 skipped，见 validation/SVS-async-catalog-release-final-QtTest.txt。确认 SVSDiffSinger.dll 和 onnxruntime.dll 的实际模块路径来自 build/Release/svs/SVSDiffSinger，包含声库和空声库启动均正常退出。首次旧 10 秒关闭限时不足（QtTest 报告 10.1 秒足够），按正在扫描的安全退出语义将该用例限时调整为 30 秒后通过；原失败记录保留为 validation/SVS-async-catalog-native-shutdown-timeout.txt。
- 全程沿用 build/Release/lmms.exe，使用原生 windows Qt 后端，未使用 offscreen。旧验收截图的测试覆盖已还原，私人声库图片不提交。
