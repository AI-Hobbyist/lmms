# 钢琴窗滚动与播放指针交付

2026-10-09。

- SVS 阶段提交 `fadb7dee7`、普通钢琴窗阶段提交 `c661eca7e` 均已推送到 `origin/master`。
- 成品：`build/Release/lmms.exe`；插件继续使用同级 `plugins` 和 `svs/SVSDiffSinger` 文件夹。
- 原生窗口验证：4 passed / 0 failed / 0 skipped；截图见 `validation/MIDI-piano-roll-song-sweep-native.png` 和 `validation/SVS-follow-song-timeline-native.png`。普通钢琴窗使用片段内播放指针，SVS 保持总体时间轴指针。
- 增量包：`build/packages/lmms-enhanced-incremental-c661eca7e-20261009-161008-win64.zip`。
- 基线：`lmms-enhanced-full-236c5f3fe-win64.zip.manifest.json`；3425 个运行文件和 4 个安装包条目，55,884,814 字节。
- SHA256：`7AEBCE1C051BD5A7C1B546CE23E9AEA5635DF63BF02A4DD375F98F28746D4D24`。
- 测试构建临时生成的 SVSExample DLL/清单已移回既有 `build/SVSExample/Release`，避免发布示例歌手。打包脚本验证启用插件与 DirectML/ONNX Runtime 依赖。
- 安装脚本 `-VerifyOnly` 校验通过：3425 个载荷文件、6 个允许退役的示例路径和安装路径；没有修改目标目录。

关闭 LMMS 后，从增量包中解压安装脚本并通过包内脚本替换安装版，目标为 `D:\Program Files\LMMS`。脚本处理文件校验、备份及旧示例插件退役；不要只复制 exe。此次未改写安装版。
