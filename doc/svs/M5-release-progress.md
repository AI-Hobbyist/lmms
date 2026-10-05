# M5 发布准备（PASS）

2026-10-05。M0→M5 的既定自动验收已完成；M4 提交 dea14c55a。只补充 SVS 模块、SDK、示例与必要构建/安装入口，没有改造相邻系统。按最新用户约束未使用 Computer Use，原安装版 LMMS 未打开。

## SDK 和示例

公开 C ABI major=1/minor=1：资源 open/read/close、ranges、宿主有界缓冲与完成通知采用可选尾部，原 1.0 必需前缀及 pronunciation 前缀兼容。宿主验证资源大小/实际 SHA-256、平台/架构、异包重复 pluginId、范围 ID/有限边界；通知排队，不从工作线程访问 GUI。PCM 返回原分配器释放，销毁 engine 后卸载库。

公开 C++17 封装在调用方编译，提供 Engine/Session/Resource/String/Result 生命周期保护。最小 C11 插件只实现 required prefix；完整 C++ 示例有全能力及精简声库、三语言字典、参数/曲线/音素/音高、资源、进度、取消与错误上下文。example.label 实际反馈绑定 noteId 并保留 JSON 引号，无新增 UI 功能。头像与立绘为可分发 SVG，不使用本机歌手 fixture。

SDK 自带 README、GPL v2 LICENSE、API/生命周期/所有权/线程、清单/格式/版本/错误文档、独立 CMake 安装、源码打包脚本及一致性工具。工具是示例契约探针，不是所有第三方引擎的完整认证；只查有界资源读取/描述符，实际哈希由宿主专项验证。

最终 build/svs-sdk-source-final.zip 解压源码独立构建 C/C++ 示例和工具并安装成功；安装工具实际检查完整/精简 ABI 1.1 两声库及 C ABI 1.0 声库均 PASS。此前安装后的两个示例及工具亦仅凭 find_package(SVSSDK) 重建通过。最终包含演示工程；证据 validation/M5-sdk-final-{package,unpack,configure,build,install,full-conformance,minimal-conformance}.log。

## 干净构建与最终回归

源码副本 D:/Temp/User/lmms-svs-clean-source-20261005 不含 tl_ref、参考图片、旧 build 或本机歌手资源。使用已安装 MSVC/Qt/Windows SDK 和既有依赖工具链，在新的 build/svs-lmms-clean-build 构建 lmms、SVSExample、SVSIntegrationTest、A3CommandsTest、McpExportLifecycleTest 成功。主程序与独立 SVS 模块均实际重新编译，未复用旧 LMMS 对象。

三个 CTest 入口全部通过：SVS 49 passed / 0 failed / 0 skipped；A3 18 passed / 0 failed / 3 skipped（既有扩展 VST fixture 未配置，不属于 SVS）；MCP 3 passed / 0 failed / 0 skipped。性能和分发运行辅助槽在普通回归不设置专用变量、单独执行专项；不能把普通回归中的辅助槽返回当作专项证据。证据 validation/M5-clean-source-{copy,configure,release-configure,build,entry-test}.log 及三个对应 QtTest.txt。

## 演示与性能

plugins/SVSExample/SVSExample-demo.mmp 由真实 DataFile 持久化接口生成：4轨×16片段×64音符，三语言全能力/英文精简、连续 tension/离散 mode、首片段4096-anchor Pitch、不同声像及0/50/100/50%立绘透明度。移除派生缓存引用，包内资源即可使用；示例包、SDK 源码和 data/projects/svs 安装入口均随附。

M0 固定数据集在隔离冷缓存中通过：120/120 拖动预览发生于合成进行时，p95=3.1433ms，最长GUI处理4.3868ms；峰值任务1/预算2、RAM95,258,928B/磁盘92,447,844B，均在原阈值内。详细测量与边界见 M5-performance.md / validation/M5-demo-cold-performance-*，不据此声称真实模型速度。

## 安装、ZIP 和独立性

使用生成的 CMake 主程序/SVSExample/data/SDK 安装入口形成 build/svs-lmms-clean-install，部署 Qt、原生 C/C++ 可再分发运行库及验证辅助程序。build/lmms-svs-native-final.zip 是本阶段 SVS 原生验证包，未扩展为全部普通乐器插件发行包。2869个文件内容核对通过；包有主程序、默认主题、SDK/许可、SVSExample/演示及独立配置 Launch-SVS.ps1。启动脚本按包所在位置选择默认主题和包内 userdata/config，不改原安装版配置；本次没有执行桌面启动。

最终 ZIP 解压到 D:/Temp/User/lmms-svs-final-20261005，PATH 仅包目录和 Windows 系统目录，Qt/数据/声库路径均指向解压包。实际 releaseDemonstrationRuntime 专项 3 passed / 0 failed / 0 skipped：鼠标音符移动/歌词提交、四轨立绘解码、64片段Ready、真实混音播放、保存重开及float WAV导出，4,224,000 samples / energy 4372.47。证据 validation/M5-final-package-runtime-{test.log,QtTest.txt}。早先安装目录及第一次工作区外解压专项也通过。

工作区外主程序 CLI render 退出0，WAV 为44.1kHz/双声道/32bit float，dataBytes=23,283,712；抽查90,952个样本均有限且能量92.4817。最终包60个PE二进制逐个检查，CLR头全为0，无 .NET/TuneLab 导入；包和演示无本机歌手/reference运行路径。证据 validation/M5-relocated-main-export.log、M5-final-independence.log、M5-native-final-package.log、M5-native-crt-deploy.log 和安装日志。

本机装有供 MSBuild 使用的 .NET，未宣称已在物理卸载 .NET/TuneLab 的另一台机器完成验收；该物理目标环境为 MANUAL/PENDING。已完成自动可测的无参考源码构建、原生依赖检查和包外迁移运行。

## 保留的历史失败与人工项

最小C示例遗漏 feedbackParameters 空数组曾真实失败，补齐后宿主与工具通过；临时插件DLL占用由显式卸载解决。QtTest宏参数逗号编译失败修正后重新构建通过。一次构建未结束时探针提前写同一 build.log 的文件占用输出不计通过证据，等待退出后逐个重跑成功。首次共享缓存测量不作为冷缓存正式证据。失败日志本机保留，未删测试或提高超时掩盖失败。

M4首次单次CTest超时、随后有界复验以及既有RenderManager Qt6文件名正则警告见M4记录；本次最终入口未复现超时。既有非SVS编译警告仅记follow-up，不改造相邻系统。

系统IME、实际桌面主题/DPI观感、听感、TuneLab钢琴窗布局/操作及工具切换差异保持 MANUAL/PENDING，由用户在M5后验收并指导修复。自动回归、冷缓存性能、独立SDK构建、演示及安装/迁移运行已完成，不以人工项阻塞Goal。已有 include/Song.h、src/core/Song.cpp、tests/vsthost/Vst2CompatibilityTest.cpp 用户修改不纳入SVS提交。
