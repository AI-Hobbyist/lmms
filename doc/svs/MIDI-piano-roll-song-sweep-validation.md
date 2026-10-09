# 普通钢琴窗片段扫描与播放指针验收

2026-10-09。本阶段在 SVS 跟随主时间轴阶段 `fadb7dee7` 已提交推送后实施。

- Song 播放时，只有主时间轴扫过当前打开的普通 MIDI 片段，才换算片段起点和裁剪偏移更新本地播放指针，并遵循钢琴窗原有分页/连续/关闭滚动模式。
- 时间轴离开片段后，视图和本地时间保持，播放指针隐藏；循环重新进入片段后重新开始跟随。Pattern Store 片段不误套 Song 坐标。原有单片段播放及录音分支保持原逻辑。
- SVS 仍遵循总体时间轴，与普通 MIDI 片段扫描分别处理；默认跟随 Song Editor，独立开关关闭或主编辑器自动滚动关闭时不滚动，到内容边界停止。
- 原目录前台构建成功：`build/Release/lmms.exe`。同工程其他任务状态为 idle/notLoaded，没有并行构建。
- `midiPianoRollSongSweepNative` 与 `svsFollowSongTimelineNative` 在真实 Windows Qt 窗口运行，合计 4 passed / 0 failed / 0 skipped。验证裁剪坐标、进入/离开、循环、连续/分页、关闭滚动后指针仍移动，以及单片段播放回归。
- 截图 `validation/MIDI-piano-roll-song-sweep-native.png` 已查看，时间线三角指针和竖线正常显示。窗口已关闭，没有使用 offscreen。
- 初次 GUI 用例未显示 MDI 父主窗口，导致窗口暴露等待失败；修正测试为显示真实父窗口后通过，没有为测试修改生产布局。
- 仅改动 `PianoRoll::updatePositionAccompany` 的 Song 播放分支及对应验收；格式检查 `git diff --check` 通过。既有 JACK、Carla 和测试部署资源提示不属于本次修改范围。

下一步完成本阶段提交推送，再生成增量替换包。
