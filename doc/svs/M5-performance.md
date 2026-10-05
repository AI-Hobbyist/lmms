# M5 固定演示与性能记录

2026-10-05，沿用 M0 同一 Windows x64 / Ryzen 7 260 / 16 逻辑线程 / 约 16 GiB RAM、Qt 6.10.3 MSVC Release。测试入口 `SVSIntegrationTest releaseDemonstrationAndPerformance`，设置 `SVS_RELEASE_FIXTURE_DIR` 输出目录，使用 offscreen 自动操作，不启用 Computer Use。该槽需单独运行；普通回归不设置该变量。新进程使用测试模式的唯一应用缓存目录，断言初始 RAM/磁盘缓存均为 0。

工程 `plugins/SVSExample/SVSExample-demo.mmp` 由真实 SVSTrack/SVSClip/DataFile 持久化接口产生，不含派生音频缓存引用或本机资源路径。4 轨、每轨 16 片段、每片段 64 音符，共 4096 音符。前三轨完整声库使用 zh/ja/en，第四轨精简声库 en；轨道音量 35、声像 -30/-10/10/30，共 32 小节、120 BPM。完整轨包含连续 Hermite tension、离散 mode 和字符串/布尔/整数音符参数；首片段另有 4096 个绝对 Pitch 锚点。各轨立绘透明度 0/50/100/50%，开关开启，资源均来自可离线分发的示例包。

| 指标 | 冷缓存实测 | M0 门槛 |
| --- | --- | --- |
| 120 次拖动预览 p95 | 3.1433 ms | <50 ms |
| 输入+重绘+GUI 事件处理最长 / 等待合成期间最长处理 | 4.3868 ms | <100 ms |
| 合成进行中的预览样本 | 120 / 120 | 拖动期间实际有活任务 |
| 峰值活任务 / 预算 | 1 / 2 | ≤预算；同一非并发示例实例≤1 |
| PCM 内存缓存计费 | 95,258,928 bytes | ≤128 MiB |
| 磁盘缓存 | 92,447,844 bytes | ≤512 MiB |
| 合成等待与交互测量合计 | 4,355 ms | 记录，不作真实模型速度门槛 |

专项 3 passed / 0 failed / 0 skipped。证据 `validation/M5-demo-cold-performance-build.log`、`M5-demo-cold-performance-test.log`、`M5-demo-cold-performance-QtTest.txt`、`M5-demo-cold-performance.json`。首次非隔离缓存试跑也通过，但正式数据采用上述冷缓存运行；不将用户历史缓存大小当作该数据集的产物。

预览测量从鼠标 move 派发到同步 repaint 与当前 GUI 事件处理，拖动结果在结束时取消，保证输出工程保留原始固定数据。合成结果随后逐片段等待 Ready 并核验计费上限。默认窗口 1000×520、水平 zoom=2，绘制只覆盖可见时间段；4096 锚点曲线和真实波形/缓存专项共同覆盖长曲线求值与分层波形。音频线程只读取不可变 PCM 的规则由 M4 自动播放/导出路径及代码约束保证，该时间统计不能证明任意第三方插件的实时性能。

分发工程已另在新进程通过真实 Song::loadProject 的重开、四轨/64片段/4096音符和曲线校验、各轨独立立绘解码、鼠标移动音符/歌词编辑、全部合成 Ready、多轨播放、保存后重开及 32 kHz float WAV 导出。专项 3 passed / 0 failed / 0 skipped；导出 4,224,000 samples，能量 4372.47。证据 M5-demo-runtime-build/test.log、M5-demo-runtime-QtTest.txt。初次 QtTest 宏参数逗号编译错误已修正，失败日志本机另存。完整 LMMS 无 tl_ref 干净源码构建、安装目录及最终 ZIP 工作区外解压运行均已通过；最终专项同样导出4,224,000个有限样本/energy4372.47，见 M5-release-progress.md 和 M5-final-package-runtime-*。桌面观感、实际 IME 和 TuneLab 钢琴窗布局/操作由用户在 M5 后验收，保持 MANUAL/PENDING。
