# UI 现代化执行与验收记录

执行基线：`146212993`。当前工作区另有 `include/Song.h`、`src/core/Song.cpp`、`tests/vsthost/Vst2CompatibilityTest.cpp` 的未提交修改；本计划不提交这些文件。构建会使用当前工作区，故基线证据不是纯 HEAD 的重建。

## 用户补充的执行范围

- 每个阶段直接提交、推送 `origin/master`，不使用 codex 分支。
- 人工观感、真实跨屏 DPI、实际输入法及需要人工操作的验收直接跳过，记 `MANUAL/PENDING`，不等待人工确认。
- 不修改 PNG。确实需要重绘 PNG 且未以 SVG 替代的部分记 `SKIPPED / PNG`，保留原资源；不算已现代化。
- 可以用 SVG 重绘的图标和装饰允许用 SVG 替代，保留旧资源兼容引用。PNG 跳过项作为用户批准的范围例外。

## 阶段状态

| 阶段 | 状态 | 自动证据 | 人工/例外 | 提交 |
|---|---|---|---|---|
| F0 | DONE / MANUAL-PENDING | 清单、规格、fixture；52 个 DLL 构建；45 个面板双 DPI 基线；2/2 必要回归 | 观感及 7 个外部运行环境项 MANUAL/PENDING | 4235348fb，已推送 origin/master |
| F1 | DONE / MANUAL-PENDING | 原生双 DPI 场景/状态、17 SVG 引用、2/2 必要回归 PASS | 观感与未替换 PNG 图标注明 | 5ad278c44，已推送 origin/master |
| F2 | DONE / MANUAL-PENDING | 主题属性/代理框、自绘框；模型及 SVS 回归 PASS；ThemeWidgetTest 6 PASS；双 DPI 实窗 | 观感 MANUAL/PENDING | a2eb86f36，已推送 origin/master |
| F3 | DONE / MANUAL-PENDING | 52 插件编译；模型/SVS PASS；控件实窗双 DPI 各 8 PASS；场景双 DPI PASS | PNG 资产保留；人工项 MANUAL/PENDING | 本阶段检查点 |
| F4 | TODO | — | — | — |
| F5 | TODO | — | 逐项登记 PNG 例外 | — |
| F6 | TODO | — | 人工项不阻塞交付 | — |

## 实窗证据

`UiBaselineCapture` 使用产品 GuiApplication 和实际页面，在独立临时配置中启动 Windows QPA；先等待窗口暴露，再等 600ms，使用 QScreen::grabWindow，最后销毁实际主窗口。没有使用离屏 QPA 或 QWidget::render。

F0 证据位于 `validation/F0-100/`、`validation/F0-150/`，包含 S01 主窗口、S02 五类设置与导出、S03 Song/Pattern、S04 Piano/Automation、S05 SVS 及插件设置、S06 Mixer/效果/控制器、S07 MDI、S08 仪器公共页。100%/150% 使用 QT_SCALE_FACTOR 模拟；真实跨屏 DPI 为 MANUAL/PENDING。S08 的每个插件后续状态按 inventory 的 D03 明细登记，基线公共页不代替插件现代化覆盖。

构建配置的 Qt 为 6.10.3，窗口基准 1280×800 DIP，默认主题；实际字体、平台、DPR、系统语言见各目录 environment.json。当前测试未加载产品翻译包，UI 为英文并加入中文轨道名；完整中文界面/中文 IME 人工项为 MANUAL/PENDING。

## 工程 fixture

`fixtures/modernization/modernization.mmp` 是可搬移的工程 bundle，sample 仅引用 bundle 内 `resources/tone.wav`；生成音频为 0.5 秒 440Hz 正弦。包含 MIDI、Pattern/步进、Sample、Automation、SVS、两个额外 Mixer 通道、Amplifier 效果及 LFO 控制器。截图程序在临时目录保存后，使用实际 bundle 内的 mmp 重新载入，并检查轨道数和 MIDI 内容。

## 验证过程中的修正

- Windows 插件导入 lmms.exe：截图程序沿用现有 VstEntryPoints 的方法命名为 lmms.exe，放在 build/tests/ui/Release，确保插件共享同一个 Engine，避免第二份静态 Engine 导致 LV2 枚举崩溃。只修改测试目标，不改变产品。
- 增加 UiPluginCoverage 聚合目标，让 MSBuild 在同一构建图中并行安排 F0 冻结的插件；原串行批次主动停止，保留 partial.log 但不作为完整通过记录。
- DataFile 的 bundle 保存不覆盖已有 bundle；重复截图在临时目录创建新的 bundle，持久 fixture 只在首次生成时复制。重开指向 bundle 内真正的 mmp 文件，由全新测试进程按应用启动路径加载；同时验证 MIDI、Sample、Automation、SVS、Mixer、效果与控制器内容。

## Follow-up（不修改产品行为）

- 初版截图程序在刚创建完整工程后立即同进程 loadProject，恢复轨道期间处理事件时崩溃。证据：F0-reopen-crash.log / results。当前 F0 使用全新进程验证可重开的工程，后续窗口生命周期测试独立执行；同进程反复替换工程的崩溃需要另行定位，不在 F0 修改产品加载流程。

## 回滚

每阶段独立提交是回滚单位。恢复对应阶段的 QSS/SVG 与 View 属性引用，保留旧资源及用户工程、配置；不使用 reset --hard 或递归清理工作区。提交号在每个阶段的下一次记录中补齐，阶段本身可由提交标题定位。

## F0 检查点

- 52 个已配置插件 DLL 均已构建：F0-plugin-build.log，F0-plugin-artifacts.json 含 SHA256。GigPlayer 缺 libgig、Sid 缺 Perl，当前配置未启用，单列且不计成功。
- 100% / 150% 各捕获 45 个插件面板：plugin-panels.json 与 F0-*-plugins-results.txt。Carla Rack/Patchbay 的外部原生引擎初始化崩溃；VeSTige、VST/LV2/LADSPA 效果与 LV2 instrument 缺外部 fixture，7 项 MANUAL/PENDING，不作为宿主技术通过证据。
- STK rawwaves 使用当前 CMake SDK 路径写入截图程序的临时配置；不改产品路径或 UI。Carla DLL 本身编译通过，运行依赖问题与未编译项分开登记。
- 核心回归：AutomatableModelTest、SVSIntegrationTest 均 PASS，后者 59 个用例通过。修正 CTest 工作目录为源码根，使既有相对主题路径可读取；不改断言及产品实现。
- 中文轨道名在原生 Song 窗口截图中正常显示；完整中文界面和输入法 MANUAL/PENDING。所有实窗测试结束后已由测试清理窗口。

- 实窗截图目标抽查后修正：浮动 EffectControlDialog 捕获其自身原生窗口，MDI 子窗捕获主窗口；双 DPI 全部重跑，避免只拍主窗口误记面板通过。

## F1 检查点

A01–A11 的标准控件规则已改为纯色、单层边框及明确状态；替换现有高优先级按钮/轨道/侧栏规则中的渐变和阴影图片，保留紧凑按钮 20 DIP、步进按钮 16 DIP 与设置分类 48 DIP 图标槽。Qt 自绘控件的属性、数字 sprite、MDI painter 和画布改造仍由 F2–F4 实施，不通过全局选择器冒充完成。

- 新增 17 个被主题引用的白色/禁用 SVG：branch、scrollbar、checkbox/radio/menu、SVS 播放/停止/曲线工具；主题安装规则已包含 *.svg。旧 PNG 保留兼容，不修改内容。Qt-native slider 的装饰帽改为 QSS 几何，不修改旧 PNG。
- 状态测试在实际 Windows 窗口中检查 Space 切换、输入框键入、焦点几何稳定、按钮切换、可见子控件处于窗口边界内；拍摄 focus/hover/pressed/菜单与 checked/disabled/read-only 状态，资源由 QPixmap 实际解析。
- F1-100 / F1-150：S01–S08 和 standard-* 图片；设置窗口底部按钮可达，中文文字正常。结果 4 PASS / 0 FAIL，两个 SKIP 是由独立进程执行的重开槽及单独执行的全插件槽。
- 必要回归：F1-tests.log，AutomatableModelTest 与 SVSIntegrationTest 全部通过（2/2）；主题相关 SVS 既有断言保留。
- SKIPPED / PNG：尚未替换的设置分类、通用工具栏中的 legacy 位图图标保留；不是已重绘资源。外观偏好、完整中文 IME、真实跨屏 DPI MANUAL/PENDING。

## F2 检查点

- ComboBox、TabWidget、GroupBox 和 SubWindow 由默认主题显式启用平面绘制；旧主题缺少新属性时保留原绘制。自绘标签的绘制与命中使用同一几何，支持稀疏 tab id；不改变参数模型和菜单入口。
- 代理边框使用单层描边，MDI 边框为 1 DIP；分离/回嵌、最大化/恢复、关闭通过原生窗口交互。GroupBox 标题按字体高度布局并调整既有内容偏移。
- 主题文件重载同步 palette 并清理 QPixmapCache。测试修改独立临时主题及 SVG，确认颜色、图标实际更新，重新打开控件及旧主题回退通过。
- 用户反馈的滑块轨道消失：水平/垂直 groove 从与面板相同的 #20262D 改为 #35414D。F2-100/S02-audio.png 与 F2-150/S02-audio.png 确认实际采样率、缓冲区轨道可见；没有修改 PNG 资产。
- 编译产物：build/Release/lmms.exe。F2-build.log、F2-theme-build.log；AutomatableModelTest PASS、SVSIntegrationTest 全套 PASS、ThemeWidgetTest 6 PASS / 0 FAIL。首轮 SVS 超时后相同套件 18.69 秒通过，保留 timeout.log，不通过扩大超时隐藏问题。主题重载初轮失败为测试未更新 resources 搜索路径，修正临时主题配置后通过。
- 双 DPI S01–S08、标准控件状态及 fixture 重开通过；F2-widgets 保存原生交互窗口证据。观感、真实跨屏 DPI、人工拖动边缘缩放/中文 IME 仍为 MANUAL/PENDING。F3 的自绘数字/旋钮与 F4 的编辑画布尚未实施。


## F3 检查点

- 默认主题显式启用旋钮矢量绘制、LCD 文本数字与小数点、推子纯色帽/电平、LED indicator、CPU 条、琴键和图表平面外框；缺少新属性的旧主题继续使用原资源。数值模型、映射、输入和采样计时未改动。
- 旋钮现代分支每次按当前 palette/DPR 绘制；旧缓存按尺寸与 DPR 校验，并在主题、字体、状态、尺寸和 DPR 变化时清理。LCD/LED 按字体度量更新尺寸。
- F3-final-build.log：主程序、必要回归目标和 UiPluginCoverage 编译成功，覆盖 F0 冻结的 52 个启用插件 DLL。输出 build/Release/lmms.exe。GigPlayer/Sid 的配置依赖例外沿用 F0；外部运行环境不算构建失败。
- AutomatableModelTest PASS、SVSIntegrationTest 全套 PASS（17.89 秒）；F3-theme-tests.log / F3-theme-tests-150.log 在原生 Windows 窗口各 8 PASS。验证增益/对数旋钮滚轮、拖动撤销、双击数值、自动化对象绑定、整数与小数 LCD、推子 -inf/0 dB/电平、LED 单次切换、黑白琴键按下释放、字体变化、主题重载及旧主题回退。
- 测试初始化补齐主程序的 NotePlayHandleManager::init，并使用 Windows 原生扫描码；模型保留到 GUI 销毁。LED 新属性 setter 仅调用 QWidget::update，避免主题重载对已解除绑定的模型执行同步读取。这些修正保留实际输入路径，不增加离屏路径。
- F3-100 / F3-150：S01–S08、标准状态及独立进程 fixture 重开通过。F3-widgets-100 / F3-widgets-150：数字、禁用、字体变更和琴键实窗。设置采样率与缓冲区滑块轨道在 150% 实窗中清晰可见。
- SKIPPED / PNG：插件私有背景与嵌入 artwork 图标保留，F5 逐项登记；本阶段没有修改 PNG 资产。截图 PNG 是原生窗口证据。观感、真实跨屏 DPI、完整中文 IME 为 MANUAL/PENDING，不阻塞检查点。