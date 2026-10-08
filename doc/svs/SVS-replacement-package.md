# SVS / DiffSinger 增强分支全量替换包

状态：PASS。用于覆盖已有 Windows x64 LMMS 安装，沿用 `build/Release` 和 `build/packages`，未覆盖或打开原 Program Files 安装。

- 文件：`build/packages/lmms-enhanced-full-90e92f6cb-win64.zip`。
- 大小：66,032,783 bytes（约 62.97 MiB），3,484 个运行文件和 4 个安装/说明条目。
- SHA256：`0B30841B5E669795BCAA03EB360A588103103C02E5A83A3EBB631400039A973B`，相邻 `.sha256` / `.manifest.json` 可核对。
- 产品提交：`90e92f6cbca9fb89ff77cbcc113678b2f9bbc021`，包含分段渲染、英文 README、SVS 插件浏览器按引擎分类及异步声库扫描。显示版本 `1.3.0-alpha.2.90+90e92f6`。后续证据提交不改变产品输入。异步扫描及当前包检查见 `SVS-async-catalog-validation.md`；下方原覆盖测试证据保留。

## 使用

关闭 LMMS，解压 ZIP，双击 **Install-Replace.cmd**，输入原安装目录（包含 lmms.exe）。Program Files 通常需要以管理员身份运行。脚本先验证 SHA256，再覆盖运行文件并验证目标；包外旧 LMMS 插件 DLL 原位保留为 `.dll.disabled`，避免混用 ABI。个人配置、工程、外部 VST 和声库路径保留。

DiffSinger 为 `svs/SVSDiffSinger` 完整文件夹，含 native DLL、CPU ORT 1.23.0、冻结发音数据和许可。通过 Settings → SVS → DiffSinger → Voicebank directories 配置自己的授权声库并 Rescan；没有分发任何私人声库模型或图片。当前 CPU 实现已完成，DirectML 未实施。

官方启动模板保留 TripleOscillator、Sample track、Pattern 0、Automation track，Pattern Editor 包含 Kicker。开发版原先的个人空白 `templates/default.mpt` 已备份并恢复官方文件。安装时仍保留个人模板；若原安装也存在自定义空白覆盖，请先备份/停用该个人文件再新建工程。替换包中的 factory default 与上游原文件 SHA256 相同。

主 README 顶部的英文对比表列出全部已实施分支增强、AI 辅助开发、独立分支维护及同步上游意图；下面的原 README 内容完整保留。包内同时提供该 README。

## 内容与验证

- 主程序、58 个原位普通 DLL（含 52 个 UI 目标及支持/导入导出插件）、32/64 位 VST helpers、Zyn helper、Qt/音频运行库、主题/预设/采样/官方模板、SVSExample 和完整 DiffSinger 包。Sid 旧 DLL 保持停用，GigPlayer 未配置启用。
- `SVS-replacement-configure.log` / `SVS-replacement-main-build.log`：原 Release 主程序更新产品版本；未向 CMake 的 Program Files 前缀执行安装。主题 CSS 原位同步为当前源文件。
- `SVS-replacement-package-build.log`：3,488 个 ZIP entry 全流 SHA256 与输入一致。`SVS-replacement-artifact.json` / `SVS-replacement-architecture.log`：360 个 PE 位数正确，x86 仅位于 helpers 的 32 子目录，官方四轨模板与私人数据排除通过。
- `SVS-replacement-verify.log` / `SVS-replacement-apply.log`：Windows PowerShell 5 校验/实际覆盖原 `build/Release` 后再次检查 3,484 个文件。配置、portable marker 和已恢复的用户模板前后 SHA 相同，见 `SVS-replacement-config-preserved.json`。
- `SVS-replacement-native-QtTest.txt`：覆盖后 3 passed / 0 failed，实际主程序在清除源码数据/插件覆盖变量、PATH 限于运行目录与系统目录的子进程中启动并正常关闭。真实 Windows Qt 截图 `SVS-replacement-native-window.png`，中文正常，无 offscreen。
- `SVS-replacement-deployed-ABI.log`：覆盖后实际 DiffSinger DLL 的 ABI 1.0～1.3、空 catalog、六包分段能力与参数颜色、资源生命周期/边界通过。
- 功能回归见 [分段验收](SVS-segment-rendering-validation.md)：70 passed / 0 failed，以及原生 GUI 专项 5 passed / 0 failed。主体 A0～A4 均已按阶段推送。

这是现有工作区的构建，保留其他任务的 Song.h / Song.cpp / Vst2CompatibilityTest.cpp 修改，不称纯 HEAD 重建。原 Carla 外部运行时警告、听感及人工观感事项沿用既有验收限制；本次没有扩张这些模块。
