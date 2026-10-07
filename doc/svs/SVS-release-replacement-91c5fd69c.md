# Windows x64 Release 替换包

- 产品源版本：`91c5fd69c4a67b7f4a2689360103121e672e1630`。
- 编译目录沿用 `build`；主程序沿用 `build/Release/lmms.exe`，原生插件位于 `build/Release/plugins`，SVS 示例位于 `build/Release/svs/SVSExample`。
- 交付：`build/packages/lmms-modern-ui-full-91c5fd69c-win64.zip`，59,180,028 字节。
- SHA256：`B9DDC792B243509E05D10ADE67B755FDBA0CB863A6D8E824204ED120DBCC93EC`。

Release 运行目标构建、原目录安装资源、打包与安装器 VerifyOnly 均通过。ZIP 包含 3471 个运行文件及 4 个安装/清单文件；所有 ZIP 文件均逐项校验 SHA256。包内主程序、SVSExample.dll、默认主题与当前部署文件哈希一致。52 个启用 UI 插件、导入导出插件、支持库、32/64 位辅助程序及原生 Windows Qt 平台文件均经打包脚本检查。

本包包含音符拉伸音素最小时长修复及原版风格琴键。本轮修复自动回归 63/63，通过实际部署插件和主题的实窗专项 5/5；截图及证据见 `SVS-stretch-phoneme-limits-and-keyboard.md`。最终操作体验按用户要求待人工验收。没有启动原安装版 LMMS。

本机 PNG 测试头像/立绘、个人配置、portable_mode.txt、Debug CRT、PDB、停用 DLL 不进入交付包。SVS 示例采用随包 SVG。直接部署的 SVSExample.dll 不在当前顶层 install_manifest 中，打包脚本显式从原 Release 路径纳入并校验，避免漏包。

包内 `Install-Replace.cmd` 调用既有替换安装器：解压后关闭 LMMS，运行脚本并选择包含 lmms.exe 的安装目录。安装器校验内容，覆盖运行文件并停用包外旧原生插件；不覆盖个人配置和工程。本次没有对原安装目录执行替换。

工作区仍含其他任务的 Song.h/Song.cpp、Vst2CompatibilityTest.cpp 未提交修改，包由当前工作区构建，并非纯提交重建。版本号仍沿用工程当前 alpha 版本；Release 指编译配置。

全目标首次构建的非交付 MCP 测试库 McpServerLifecycleTest.lib 出现 LNK1114（无法覆盖文件，错误 5）；没有改动该测试。随后针对交付运行目标的构建全部通过。日志位于 `build/Release-replacement-build.log`、`build/Release-replacement-runtime-build.log`、`build/Release-replacement-install.log`、`build/Release-replacement-package.log`、`build/Release-replacement-verify.log`。
