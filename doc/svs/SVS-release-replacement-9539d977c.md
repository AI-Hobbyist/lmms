# 自动音素拉伸修复 Release 替换包

- 产品源版本：`9539d977c108a02a5e6a154d6843dc18a369648a`，已推送 master。
- 交付：`build/packages/lmms-modern-ui-full-9539d977c-win64.zip`，59,180,166 字节。
- SHA256：`60A23A75CBBC15DBCA474E828C1BDAA0B1ECE8E90A8BE5E77073EFCB3A96A66F`。
- 本包替代 `91c5fd69c` 包，包含自动音素拉伸误写 segments:null 的修复及前一包琴键/音素最小时长修复。

沿用 build 和 build/Release：本次只需重新编译受修改影响的主程序与 SVSIntegrationTest；SVSExample、普通插件、支持库沿用前一包已验证的原目录 Release 二进制。没有迁移构建或部署目录。实际主程序、示例 DLL 哈希见 `SVS-automatic-stretch-null-fix.md`，与包清单一致。

打包前 Windows 原生 SVS 自动回归 64/64 通过；Computer Use 实机首尾伸缩、音素条首尾伸缩和独立音素交界调整均 Ready，音素块保留。实测窗口已关闭。

前台环境脚本＋Tee-Object build.log＋LASTEXITCODE 打包及安装器 VerifyOnly 均通过：3471 个运行文件、4 个安装/清单文件，逐项 SHA256 校验成功。日志为 `build/Release-automatic-stretch-package.log` 和 `build/Release-automatic-stretch-verify.log`。包内没有本机头像/立绘 PNG、个人配置、portable_mode.txt、Debug CRT、停用 DLL 或 PDB，示例随包 SVG 独立运行。

解压后关闭 LMMS，运行 Install-Replace.cmd 并选择原安装目录；没有代用户执行原安装版覆盖。工作区仍有其他任务 Song.h/Song.cpp 和 Vst2CompatibilityTest.cpp 未提交修改，当前包来自现有工作区 Release 构建，并非纯提交重建。Release 指编译配置，程序仍使用工程 alpha 版本号。
