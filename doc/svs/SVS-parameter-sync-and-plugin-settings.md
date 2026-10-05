# 参数联动与原生 SVS 插件设置（PASS）

2026-10-05，依据用户人工验收反馈，继续修改独立 SVS 模块。

侧栏与底部参数栏共用引擎的有效参数声明。Power、Soft、Mode 等可调曲线基础项不再遗漏；数值使用相对默认值的滑块/数值框，布尔使用开关，枚举使用选项框。Gain 等声明的 track/clip 数值基础项也可选中查看基础线，但未声明曲线能力的项不能画曲线。只读反馈保留在底部参数栏，与可调输入之间有竖线和 Read-only 标识，不出现在可调侧栏。参数按钮超过可用宽度时横向滚动，侧栏沿用纵向滚动。

数值滑块实时调节时自动选中对应输入参数，主曲线与叠加曲线采用相同的 base − default 位移。原始曲线、切线、间隙及音符参数不覆写；范围内保留形状，越界锚点按声明范围限幅。坐标反算扣除基础偏移，继续画曲线不会重复叠加偏移。滑块从按下到释放共用一个撤销检查点，捕获丢失/隐藏/销毁恢复 journalling。布尔和枚举是离散基础设置，已有显式曲线继续优先，不对离散 ID 做数值位移。

track/clip 基础值沿用原字段；note 作用域的片段级基础设置保存在 SVSClip.globalParameters（XML 可选 JSON 属性）。新增音符同样使用该基础设置，音符的原始参数保留。宿主在最终插件提交前，依据该次有效能力把数值曲线及 note 参数组合为有效输入；源快照、缓存身份和原始曲线分离。公开 C ABI 不变；SVSExample 直接读取有效曲线，不再重复叠加基础值。本说明替代第 21 节中示例引擎自行组合曲线的实现说明。

轨道头像/名称按钮打开可复用的 LMMS Qt Widgets 插件设置窗口，Voice 页提供说话人、头像路径/浏览、立绘路径/浏览、显隐、0–100% 透明度和恢复声库图片；Effects 页沿用原生效果链。轨道行移除说话人选择控件，保留音量、声像和混音通道。插件窗口选择说话人后恢复跟随声库名称，作为一次轨道操作；随后用户仍可按计划书自定义轨道名。钢琴窗已有 Singer 入口继续保留。

自选图片使用既有异步解码、尺寸/内存限制和缓存；头像覆盖只影响轨道图标，立绘覆盖只影响钢琴窗显示，不改变合成输入。路径及透明度使用现有 portraitSettings 保存、恢复和撤销；插件提供的资源仍受原包路径约束，只有用户显式配置的文件可以使用包外路径。

开发版独立配置 `build/svs-lmms-clean-install/svs-development.xml` 为 SVSExample 设置本机默认测试图片：

- `svs/testAvatarPath`：`D:/AI-Tools/OpenUtau/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4/avatar.png`
- `svs/testPortraitPath`：`D:/AI-Tools/OpenUtau/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4/character.png`

绝对路径只在本机开发配置和可选测试环境中启用，不写入示例目录、SDK 或 Release 默认资源。不带该配置的独立安装仍用可分发的示例 SVG。原 M5 ZIP 未重新打包。

验证：Release 主程序、示例插件与测试编译通过；完整 Windows 原生 SVS 回归 **55 passed / 0 failed / 0 skipped**。覆盖入口移除/头像按钮打开设置、默认图片、缺图恢复、透明度与路径持久化、说话人跟随名称、所有可调项同步、只读分界、实际曲线位移、偏移后锚点编辑（允许一个像素的整数离散误差）、一次拖动撤销/重做、原曲线不变、Power 数值与整体平移曲线逐样本一致、Soft 输出变化、新音符基础值及横向滚动。既有播放/导出/缓存/取消/能力/主题/图像回归通过。

GuiApplication 实窗截图已检查 LMMS 主题、原生字体、参数分界及用户指定立绘；未使用 offscreen 或 Computer Use。开发目录 lmms.exe、SVSExample.dll 和测试程序已更新并核对 SHA，测试结束 LMMS/test 进程为 0。原安装版未打开。用户最终人工布局/操作验收 MANUAL/PENDING。

证据：`validation/SVS-parameter-sync-build.log`、`SVS-parameter-sync-regression-QtTest.txt`、`SVS-parameter-sync-regression-test.log`、`SVS-parameter-sync-native-window.png`、`SVS-plugin-settings-native-window.png`、`SVS-parameter-sync-deploy.log`。首次编译/测试过程中的接口类型、测试控件创建时序与双击事件、整数像素误差修正留在本地诊断日志。现有 JO-ID/测试图标/RenderManager 正则警告记为 follow-up，不修改相邻系统。
