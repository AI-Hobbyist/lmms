# B4：回归、原位部署与独立 SDK

状态：PASS（必需自动验收）。B0～B3 已分别提交并推送；本记录只覆盖 B4。

## 全局模型驻留管理

用户追加的设置位于 SVS AI 全局设置，适用于所有共享计算后端：

| 策略 | 行为 |
| --- | --- |
| 渲染完成后立即释放 | 最后一个完整渲染租约结束后释放模型 |
| 空闲后自动释放（默认） | 默认 60 秒，可设为 1～86400 秒 |
| 保持模型常驻 | 保留模型，直到正常关闭或既有资源限制要求回收 |

CPU 释放模型内存；DirectML 同时释放模型 GPU session、device 和 queue。系统分配器和驱动可能保留缓存，不承诺进程内存归零。释放后保留模型/session 描述，下次请求重新加载；已返回的张量和 PCM 仍有效。该策略不进入音频身份、不递增后端策略 revision、不使有效缓存失效。

宿主覆盖整个分段合成/导出任务；DiffSinger 独立消费者也在完整多阶段渲染中持有租约。各个 session 初始化和 Run 另有内部保护。最后一个 Context 关闭时停止并等待监控线程；最后一个 DiffSinger engine 在插件卸载前显式关闭计算 Context，避免在 Windows DLL loader lock 内等待线程退出。

共享 C ABI 仍为 1.0，新增可选尾部和 `SVSC_FEATURE_MEMORY_POLICY`。原必需前缀消费者兼容；新 C++17 包装先检查 size/feature，再访问新增函数。

`validation/B4-memory-conformance.log` 已验证 CPU、AMD Radeon 780M、NVIDIA GeForce RTX 5060 Laptop GPU 的三种策略、1 秒自定义空闲、完整租约期间不释放、相同 session 自动重载和释放后输出所有权。CPU 驻留 session 归零；两 GPU 驻留 session 和观测 GPU local bytes 均归零；随后恢复推理成功。仍复用 B0 冻结的 ORT 1.23.0 / DirectML 1.15.4 和真实 LUID。

## 实窗与实际 Release

所有 GUI 验证使用 Windows 原生 Qt backend，没有 offscreen。实际窗口稳定后通过屏幕抓取检查；中文歌词、读音和设置标签正常，无方框。

`validation/B4-host-GUI-QtTest.txt`：5 passed / 0 failed / 0 skipped。包括全局设置持久化、第二 AI 引擎消费 AMD DirectML、真实 DiffSinger NVIDIA DirectML 合成、歌词编辑、只读参考曲线、播放/导出及 XML 恢复 PCM。真实 acoustic、pitch、variance、duration 和 vocoder 均记录 DML 节点，缓存命中不被当作新推理证据。节点计数可能累计于 session，不将其解释为不同节点数量。

`validation/B4-release-window-QtTest.txt`：3 passed / 0 failed / 0 skipped。启动原路径 `build/Release/lmms.exe`，读取外部声库配置并打开实际工程；核对实际加载的 DiffSinger/ORT DLL 路径；空声库配置也能启动并正常关闭。子进程移除开发用 LMMS/共享计算/Qt plugin 路径环境覆盖，使用部署目录。私人声库、头像、立绘和试听音频不进入提交或分发包；含私人图片的窗口截图仅留本地。

最后的生产修复后再次执行 `validation/B4-native-final-QtTest.txt`：**6 passed / 0 failed / 0 skipped**，85781 ms，进程正常退出。使用最终原位 DLL 验证全局设置、第二 AI、完整 NVIDIA DiffSinger 实窗编辑/保存恢复/导出、实际 Release 打开及空声库启动。实际 Release 工程配置保存 DirectML/NVIDIA；CPU 路径由完整标准套件及前一次 Release 专项验证。新设置与不可用设备、空声库 Release 的实窗截图已逐张检查；含私人立绘的编辑器截图只作本地字体/曲线检查。

补跑原有 GUI 用例时，6 个用例通过，另 2 个旧 fixture 的前提不完整：音高重置实际 CPU 合成 13.25 秒超过 10 秒等待；前缀显示 fixture 未保存默认 vocoder 根，首次设置 Apply 同时显式补入默认根，按既有规则触发重新合成。分别采用已有真实合成的 180 秒上限、在初始 fixture 写入相同默认根，保留音频指针和缓存身份断言；不改生产 UI 或设置失效规则。临时设置差异日志已移除。`validation/B4-native-fixture-final-QtTest.txt`：**4 passed / 0 failed / 0 skipped**，54323 ms，正常退出。首次批次 `B4-native-regression-final-QtTest.txt` 保留 8 passed / 2 failed 的真实记录；其余 6 个旧用例均通过，包含外部单声库实际合成。结合重启 child 和 Release 专项，标准套件跳过的 10 个具体用例均另行执行通过。

## 回归定位记录

首次完整标准套件输出 86 passed / 2 failed / 10 skipped，但输出总计后进程未退出，不能将其报告为 PASS。原始日志保留于本地 `validation/B4-full-SVS-first.log`。

缓存恢复失败缩小为 `missingPluginCachedPlayback`：原片段缓存恢复和播放成功，克隆缓存失败。临时对照证明克隆缓存身份仅 seed 不同。之前 UUID 派生的 seed 在克隆更换 UUID 时变化；现在将 seed 作为内容持久化，克隆保留它，旧工程仍按原 UUID 推导原 seed。保留独立子进程缓存恢复回归和 seed/cache 身份断言。

退出问题缩小为单独 `diffSingerCpuAudioPlaybackAndExport`：合成、导出、cleanup 和总计均完成，worker 已退出，但进程停在 Context 关闭。临时边界日志确认清理位置；修复将 Context 关闭移至最后一个 engine 的 destroy，发生在插件卸载之前。所有临时 `[DEBUG-b4-*]` 日志均移除。`validation/B4-lifetime-fixed-QtTest.txt`：缓存恢复及真实 CPU 合成最小复现 4 passed / 0 failed / 0 skipped，进程正常退出。

随后完整套件正常退出，剩余 1 项窗口对齐失败：该测试的固定 tick 断言使用默认 140 BPM，但前面的工程测试留下 120 BPM；声库最大提前量 0.2 秒转换为 tick 后，拖动受限于 76.8 tick，得到 30.8 而非 28。测试自身固定默认 tempo 并在结束后恢复；生产 UI 未修改。`validation/B4-tempo-fixture-QtTest.txt`：实窗用例 3 passed / 0 failed / 0 skipped。

最终完整标准套件 `validation/B4-full-SVS-final-QtTest.txt`：**88 passed / 0 failed / 10 skipped**，434486 ms，进程正常退出（exit 0）。第二 AI 测试 fixture 的 manifest 在此套件期间暂时停用并原位恢复，保留旧用例“首个声库为旧示例”的前提；第二 AI 单独验收。10 个 skip 是带有显式前提的 8 个 GUI 用例、1 个外部声库/共享 vocoder fixture 用例及 1 个重启专用 child；全局设置 child 和实际 Release 等已在专项运行，不把 skip 当作 PASS。其余原有工程/播放/导出/编辑/取消/缓存/分段用例均执行，无失败。

## SDK / 部署交付

独立 SDK 已沿用 `build/svs-sdk-external` 构建、安装至 `build/svs-sdk-install`。纯 C ABI 检查、旧 minimal ABI 1.0/full ABI 1.3、六声库 DiffSinger ABI 验证、共享计算 CPU/两 GPU conformance 均通过。安装包含 compute 客户端、独立 worker、冻结 ORT/DirectML DLL、依赖锁和许可（ORT、DirectML、nlohmann/json），保留 `VOCODERS.md`。

公开 `examples/compute-consumer` 已从安装后的源代码单独配置与构建，只使用公开 C++17 头文件和动态加载器。`validation/B4-public-consumer-PE.log` 记录消费者及客户端 PE imports，不依赖 LMMS/Qt/ORT。CPU 模式暂时移走包内 `DirectML.dll` 仍实际推理成功，随后原位恢复；NVIDIA DirectML 实际 Add 节点推理成功。

SDK ZIP 按每个 entry 的 SHA256 对照源文件验证：

| 包 | 文件数 | SHA256 |
| --- | ---: | --- |
| `build/packages/svs-sdk-source-B4.zip` | 88 | `3448F8D76785038AC44DD9B6A798A67F2F38AE68BA6CCB6EB3B33F27FF027F47` |
| `build/packages/svs-sdk-native-B4-win64.zip` | 130 | `C5435383D0704A395FDE756B112FDF3F90190075447B9F6BE4D0817EA95DA1D4` |

源码 ZIP 已解压到既有 `build/svs-sdk-unpacked-final`，从包内独立配置、构建至 `build/svs-sdk-final-build`，安装至 `build/svs-sdk-final-install`。`validation/B4-sdk-source-{configure,build,install}.log` 均通过；构建不读取 LMMS 模块、Qt 或 refs。包内新生成的 pure C/minimal/full、共享 CPU/AMD/NVIDIA 生命周期、公开 CPU（缺 DirectML.dll）/DML 消费者均通过，见 `validation/B4-source-*.log`。

`validation/B4-source-DiffSinger-DML.log`：源码包编译的原生 DiffSinger 测试使用该包安装的实际共享 client/worker；外部声库由命令参数提供。duration、pitch、variance、acoustic、vocoder 的真实 DML 节点与 fresh CPU 数值对照通过；强制 CPU vocoder、缓存重放、种子、取消等检查通过。模型字节、头像和试听 WAV 不打包。

全量替换包 `build/packages/lmms-enhanced-full-d37677308-win64.zip` 已生成并逐 entry 校验：6844 个运行文件 + 4 个安装入口/清单文件，113557280 bytes，SHA256 `84C33ECD2D0F780DCE6489887F8BD5DC461D44A9FDC5DC5345027512191C7FB5`。包名/`ProductCommit` 是 B3 基线 `d37677308`，安装说明明确记录 B4 工作区构建差异；不声称纯 B3 提交重建。

`validation/B4-deployment-audit.log` 和 `B4-deployed-hashes.json` 核对原位主程序、52 个启用 UI 插件及共享计算/DiffSinger 包共 78 文件与替换包清单相同。主程序仍为 `build/Release/lmms.exe`；普通插件与支持库位于 `build/Release/plugins`（实际共 76 个顶层 DLL，包含支持库和运行库），worker 位于 `build/Release/svs/compute`。禁用 Sid 的旧 `sid.dll.disabled` 原位保留，不进入包。包内保留官方 LMMS 采样/模板与既有工程转换运行时；不包含 refs、声库模型、私人配置、SVS 试听 WAV 或调试/停用 DLL。完整清单见 `validation/B4-replacement-manifest.json`。

构建、测试和打包均在现有目录内，前台 PowerShell PTY，先载入 `buildtools/Enter-LmmsEnvironment.ps1`，实时 stdout/stderr 经 `Tee-Object build.log` 保存，立即检查退出码，归档每次日志。

主观听感/操作体验：MANUAL/PENDING。非 Windows 实机运行未在本机验证；不将 Windows CPU/DML 验收外推为其他平台实测。

用户后续授权（2026-10-09）：本阶段检查点提交推送后，移除 DAW 部署中的 SVS 示例 DLL，保留 SDK/仓库参考代码，再交付增量替换包。该交付另作检查点，不改变本阶段已执行的旧 ABI/第二 AI 验收证据。原有 Carla/JACK 环境提示不属于 B 阶段改造范围，日志保留，未为此修改对应模块。

B4 检查点已提交推送 `027ea2e8e`。后续生产清理及增量包验收见 [增量交付记录](SVS-production-incremental-delivery.md)；用户最终替换包采用该记录中的增量 ZIP，上述全量包保留为 B4 检查点历史证据。
