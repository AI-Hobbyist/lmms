# 全量覆盖安装包

F0–F6 已按阶段独立提交并推送 master，F6 为 `7b44c5487187d241c2dece22630a573479129c44`。按用户后续要求制作完整 Windows x64 运行包，沿用已有 `build/packages`，没有另建安装或验证目录。

- 文件：`build/packages/lmms-modern-ui-full-7b44c5487-win64.zip`
- 大小：60,648,335 bytes（约 57.84 MiB）。
- SHA256：`32489E569E2004B3363E545BC3CE1EFDF75346EDB82A6FF156EB2970720CA802`
- 相邻 `.sha256` 和 `.manifest.json` 为校验文件；程序源码为 F6 产品状态。工作区的其他任务修改仍在构建输入中，不称纯提交重建。

## 使用

关闭 LMMS，解压 ZIP，双击包内 **Install-Replace.cmd**，输入原安装目录（包含 lmms.exe 的目录）。覆盖 Program Files 下的安装时，以管理员身份运行该 cmd。脚本先校验包内所有运行文件，再覆盖并校验目标文件；不改个人配置、工程和外部 VST/声库路径。

旧 plugins 中包清单之外的 DLL 会原地改名为 `.dll.disabled`，保留文件，以避免旧控件 ABI 再次导致闪退。直接手动复制运行文件也可以，但需要自行完成这一步；优先使用附带脚本。若现有配置使用自定义主题路径，新默认主题可能不会自动生效；经典/现代主题专用切换仍为未来计划，本轮未实施。

## 内容与例外

3480 个运行文件加 4 个包说明/安装文件：主程序、52 个启用 UI 插件、3 个导入导出插件、支持库、32/64 位 VST helpers、Zyn helper、Qt/音频运行库、Windows 原生平台插件、data 资源及 STK rawwaves、预设/示例工程、全部安装主题、SVSExample 和仓库 SVG 头像/立绘、原许可文本。

本机 `.lmmsrc.xml`、portable_mode.txt、lmms-workspace、开发 SDK/include/lib、测试程序和调试符号不进入包。Sid 缺 Perl、GigPlayer 缺 libgig，仍未启用，不假称已编译。五项外部插件运行环境、听感/人工观感/完整中文 IME/真实跨屏 DPI 为 MANUAL/PENDING；私有 PNG artwork 为用户批准的 SKIPPED / PNG，包中复制原资源，未重绘。

## 验证

- package-support-build.log / package-install.log：重新编译导入导出及 VST/Zyn 辅助程序，再安装到原 build/Release；已有 `/mwindows` 链接选项警告被 MSVC 忽略，构建成功，不借此修改无关链接逻辑。
- package-build.log：3484 个 ZIP entry 全部解压流 SHA256 与打包输入一致。package-artifact.json 记录文件数、包哈希及位数；52 UI DLL/主程序均 x64，VST helpers 同时验证 x86/x64。
- package-installer-verify.log / package-installer-apply.log：在 Windows PowerShell 5 上校验，并实际向既有 build/Release 覆盖 3480 个文件、逐项校验；没有另起部署目录，没有操作原 Program Files 安装。
- package-config-preservation.log：现有开发版配置与 portable 标记的前后哈希相同。配置内容未进入日志或包。
- package-smoke-results.txt：覆盖后原生实窗 3 PASS，实际 build/Release/lmms.exe 在清除资源/插件覆盖变量、PATH 仅安装目录与 Windows 系统目录的条件下正常启动/关闭；真实截图为 package-installed-executable.png。

F6 的完整场景、模型/控件/SVS 回归与所有用户批准例外继续见 acceptance.md、delivery-audit.md 和 plugin-coverage.md。压缩包和完整文件清单留在原 packages 输出目录，源码、说明和验证记录提交主仓库。
