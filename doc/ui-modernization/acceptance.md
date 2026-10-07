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
| F3 | DONE / MANUAL-PENDING | 52 插件编译；模型/SVS PASS；控件实窗双 DPI 各 8 PASS；场景双 DPI PASS | PNG 资产保留；人工项 MANUAL/PENDING | 6f90902e7，已推送 origin/master；部署补修见下 |
| F4 | DONE / MANUAL-PENDING | 52 DLL 原目录编译；核心 3/3 PASS；双 DPI 场景及编辑交互 | PNG 保留；观感/IME/跨屏 DPI MANUAL/PENDING | e5fba8574，已推送 origin/master |
| F5 | DONE / MANUAL-PENDING | 52 DLL；核心 3/3 PASS；控件双 DPI 各 12 PASS；47 有效面板双 DPI 与预设检查 | 5 外部待验收；私有 PNG 逐项 SKIPPED | 93770265d，已推送 origin/master |
| F6 | IMPLEMENTED / MANUAL-PENDING | 原目录安装；核心 3/3；双 DPI 场景；125%/200% 抽查；独立 PATH 主程序冒烟 | PNG/人工/外部例外见交付审计 | 7b44c5487，已推送 origin/master |

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

- 经典/现代主题切换入口：用户于 2026-10-07 明确要求列为未来计划，本轮不实现，不作为 F0–F6 完成门槛。现有 Settings → Paths → Theme directory 保留；测试中的 legacy 自动切换仅用于回退验证，不代表已提供专用切换功能。
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
## F3 后插件窗口崩溃与部署补修（2026-10-07）

- 用户复现路径：从左侧乐器插件拖入 Song Editor，再点击轨道名称打开插件窗口。相同原生 Windows 测试加载旧 `build/Release/plugins` DLL 时以 `0xc0000005` 崩溃；仅切换到 F3 新 DLL 后通过。之前的 52 插件构建写入 `build/plugins/Release`，手动启动的开发程序仍加载旁边的旧 DLL，形成控件 ABI 不一致。之前的自动通过记录未覆盖手动启动实际部署目录，这一缺口在本次补齐。
- `BUILD_PLUGIN` 在 Windows 同时设置 MODULE 的库输出和 SHARED 的运行时输出为原程序旁的 `build/Release/plugins`；Carla 支持库也输出到同处。52 个冻结 UI 目标全部直接覆盖原 DLL，另重建三项导入导出插件。路径、大小、修改时间及 SHA256 见 `F3-plugin-directory-manifest.json`；最终构建日志 `F3-plugin-directory-build-final.log`。程序仍为 `build/Release/lmms.exe`，没有另建构建或部署目录。
- 原目录存在当前缺少 Perl 而未构建的旧 `sid.dll`，全插件面板检查在 Sid 处再次复现崩溃。已原地改名 `sid.dll.disabled` 停用，文件未删除或迁移；GigPlayer DLL 原目录不存在。保留失败记录 `F3-plugin-directory-panels-stale-sid-results.txt`。Sid/GigPlayer 依赖例外继续明确记录，不算已编译。
- 验证改为加载实际部署目录：`F3-plugin-directory-tests.log` 必要回归 3/3 PASS；最终 `F3-plugin-directory-tests-final.log` / `F3-plugin-directory-theme-results.txt` 控件 10 PASS，包含实际 DnD 入口、异步加载后点击轨道标签开关 TripleOscillator/Kicker、浏览器三轮展开/折叠/搜索和事件循环响应。`F3-widgets-100/F3-dropped-*` 与 `F3-instrument-browser.png` 为原生实窗证据。
- `F3-100-plugins-results.txt` 全面板实窗测试 3 PASS / 0 FAIL；`F3-100/plugin-panels.json` 登记 47 个面板打开与截图，5 个外部环境项 MANUAL/PENDING（Carla Rack/Patchbay、VST effect、LV2 instrument/effect）。这只是运行与截图证据，人工观感仍待验收；未修改 PNG 资产。最初重复 Tee-Object 争用 build.log 的检查已终止并按脚本自身前台日志管线重跑，不能算 PASS。
- 用户要求已写入 AGENTS.md：沿用原构建/部署目录，插件及支持库直接覆盖原 plugins；验证使用同一目录；核对残留禁用插件。F6 计划也改为沿用原开发前缀。补修独立提交推送后恢复 F4，F4–F6 未提前标完成。
## F4 检查点

- Song/Pattern/MIDI/Sample/Automation/SVS 片段使用默认主题 3 DIP 圆角路径裁剪和单层边框，最窄片段半径收缩；普通 PianoRoll/SVS 音符最多 2 DIP。缺少现代属性的旧主题继续原矩形/边框绘制。波形、步进、音符预览与原命中矩形保持；普通音符的音量/声像端点使用两条纯色数据带。
- 统一画布背景 #14181D、细网格/拍线/小节线层级、选择/ghost/静音状态；Automation 坐标标签取消阴影，时间轴循环区纯色单层边框，播放头矢量绘制。原标尺位置、循环手柄、参数分界、曲线求值、头像/立绘和滚动算法未改。
- clip 缓存按物理像素/DPR 分配，轨道背景在主题属性、字体、palette 或 DPR 改变后重绘。原生窗口像素检查确认修改背景色无需 resize 即生效。
- F4-build-final.log：主程序、ThemeWidgetTest/UiBaselineCapture、模型/SVS 与 UiPluginCoverage 编译通过。52 个启用插件直接覆盖 build/Release/plugins，路径/时间/哈希见 F4-plugin-artifacts.json；程序仍为 build/Release/lmms.exe。Sid 的旧 DLL 原地停用、GigPlayer 缺依赖例外沿用 F3。
- F4-tests-final.log：AutomatableModelTest、SVSIntegrationTest、ThemeWidgetTest 3/3 PASS。原生 ThemeWidgetTest 共 11 PASS，增加 clip 移动、两端拉伸、Ctrl 选择、菜单 Copy/粘贴、撤销、最窄片段几何，以及 PianoRoll 创建/移动/右端拉伸/复制粘贴/撤销；模型结果与命中几何有断言。F4-theme-150.log/results 同套原生控件 11 PASS。
- F4-100 / F4-150：S01–S08、标准状态及 fixture 独立进程重开通过；S03/S04/S05 是实际 Windows 窗口，SVS noteRect 到时间/音高反变换断言通过。截图测试明确选音符画布而非参数画布，并将视口移到 fixture 的 C4，保证音符可见。F4-clip-actions、F4-short-clip、F4-piano-actions 是实际交互后的窗口。100% 中文轨道名正常，无方块字。
- 初轮编译的 ClipView 命名歧义修正为 lmms::gui::ClipView；150% 像素检查的采样点移入可见轨道，并等待实际主窗激活重绘后通过。失败记录保留，不计 PASS。150% 截图超过当前屏幕可用范围的外围区域不作为布局正确证据；全窗适配继续在 F5/F6 的既定布局检查内处理。
- SKIPPED / PNG：旧工具栏和私有插件 artwork 位图保持原内容，留待 F5 逐项登记；本阶段未修改 PNG 资产。截图 PNG 是实窗证据。人工观感、真实跨屏 DPI、完整中文 IME 为 MANUAL/PENDING，不阻塞阶段检查点。

## F5 检查点

- Mixer 通道与效果卡片采用默认主题纯色单层圆角边框，效果名称取消阴影；Controls 按文本尺寸取紧凑宽度并保留完整 tooltip。选通道、效果 LED 启停和 Controls 打开/关闭使用真实输入验证，模型结果保持。旧主题继续原绘制。
- 仪器公共页不再只受插件的 256 DIP 固定宽度约束。现代 TabWidget 报告内容布局尺寸，宿主按公共页实际最小宽度设置约束；包络/LFO 长标签在 100%/150% 均完整显示，有可见标签宽度及父区域边界断言。现代滤波器内容使用 GroupBox 的标题高度，legacy 保持原 18 DIP 内容偏移；插件私有 artwork 仍保持原尺寸和坐标。
- SVS 顶部工具栏在窄窗时可水平滚动，保留原按钮、信号和输入入口；F5-150/S05-svs.png 的 Settings/Properties 均可见，参数侧栏和只读分界保持。当前屏幕的场景主窗口/MDI 与控件测试主窗口均适配可用区域；真实跨屏 DPI 仍为 MANUAL/PENDING。
- F5-final-build.log（最终）及 F5-tab-size-build.log、F5-unloaded-wrapper-build.log、F5-visible-tool-build.log、F5-host-wrapper-build.log：产品、测试与 52 个 UiPluginCoverage 目标编译成功。DLL 原地位于 build/Release/plugins，清单/哈希见 F5-plugin-artifacts.json；支持库仍在相同目录。主程序为 build/Release/lmms.exe。Sid 原 DLL 保持 .disabled，GigPlayer 缺依赖、未启用；没有新建部署目录。
- F5-tests-final.log：AutomatableModelTest、完整 SVSIntegrationTest、ThemeWidgetTest 3/3 PASS（44.93 秒）。F5-theme-results.txt / F5-theme-150-results.txt：原生控件各 12 PASS；包括此前用户的浏览器/拖入乐器/点击名称打开窗口路径、编辑交互、重载与旧主题回退，以及本阶段 Mixer/效果卡片操作。实际截图 F5-mixer-rack / F5-effect-controls 分别归档在双 DPI 目录。
- 最终重跑曾在 SVS 导出上下文用例超时，保留 F5-final-regression-timeout.log / F5-final-svs-timeout-results.txt；同一套件保持原 60 秒上限重跑后 17.95 秒通过，未扩大超时、未修改导出/DSP 行为。该间歇性导出用例超时列为既有 follow-up，不计失败轮为 PASS。
- F5-100 / F5-150：S01–S08、五个仪器公共页、标准状态与全新进程 fixture 重开通过；场景各 4 PASS / 0 FAIL，两个 SKIP 是分别独立执行的槽。插件各 3 PASS / 0 FAIL，52 项中 47 个有效宿主面板实际打开、截图，45 个 instrument/effect 做 UI 显示前后状态及 restore/save 比较，2 个 Tool 明确预设 N/A。逐项源入口、目标、共享/私有控件、资源键、截图与例外见 [plugin-coverage.md](plugin-coverage.md) 和 F5-plugin-coverage.json。
- Carla Rack/Patchbay、VST effect、LV2 instrument/effect 共 5 项外部运行环境 MANUAL/PENDING。VeSTige 按实际可独立打开的 LMMS 空宿主页检查，不依赖异步发现的个人外部插件；其外部插件编辑器/声音不算通过。ZynAddSubFX 只计 LMMS 宿主，不把 vendored 原生编辑器算作换肤。人工验收全部按用户要求跳过。
- 实窗审查发现 VST 初始化失败后 isOkay() 仍可能为真，返回零 controls 的空框；仅排除 DummyEffect 不够。测试增加空 VST wrapper 判断，旧 placeholder 截图和旧 48 项记录仅留诊断、不能算最终 PASS。Tool 原截图未显示 MDI 父窗口，现已用实际显示/前置/可见区域检查重跑；S08-unshown-* 仅为旧错误取证，最终 S08-plugin-ladspabrowser/taptempo 含真实控件。
- 首轮预设比较在 View 绑定前取快照，Eq 构造的既有单选归一化导致差异。当前快照在绑定后获取，准确验证显示操作和预设恢复；该构造归一化作为既有 follow-up，不扩大本阶段音频/预设行为范围。PeakController 按原语义在恢复时重新生成运行时 effectId，仅恢复后的比较排除此字段，显示前后仍完整比较。测试修正日志保留，不把失败算通过。
- SKIPPED / PNG：逐项保留私有背景、bitmap 图标、嵌入装饰标签与其固定几何；不是已重绘。Monstro 的 artwork_op/artwork_mat 已补入资源清单。宿主纯色框与共享控件已实施；有意义的数据波形/频谱/矩阵保持算法，未修改任何插件 DSP、参数 id 或预设实现。听感、完整中文 UI/IME、真实 DPI 与外观偏好为 MANUAL/PENDING。F6 的安装、125%/200% 有限抽查及最终逐条交付审计尚未完成。
## F6 检查点

- F6-build.log：主程序、必要测试和 52 个启用插件在原目录编译通过；F6-capture-final-build.log 完成最终安装/缩放测试目标编译。程序仍为 build/Release/lmms.exe。F6 产品实现没有继续扩展，仅补齐交付验证与文档。
- F6-install.log：在既有 build/Release 前缀完成安装；3250 条安装清单全部位于该目录，没有触及原 Program Files 安装。F6-installed-resources.json 的 CSS/17 SVG 哈希逐项相同，F6-plugin-artifacts.json 为 52 个原 plugins DLL 的当前清单，与 F5 哈希一致。Sid 原地 disabled、GigPlayer 未构建的例外保留。
- F6-tests.log：核心 3/3 PASS（45.35 秒）；完整 SVSIntegrationTest 59 PASS，ThemeWidgetTest 12 PASS。F6-theme-150-results.txt：直接使用安装资源的原生控件 12 PASS。主题重载、字体/DPR、旧主题回退、DnD 打开乐器、编辑模型与机架交互均保留实际断言。
- F6-100 / F6-150：安装资源下 S01–S08、五个仪器公共页、标准控件状态和全新进程工程重开，各 5 PASS；实际 build/Release/lmms.exe 通过临时 --config 正常启动/关闭。F6-isolated-launch-results.txt 3 PASS：子程序清除源码数据/插件覆盖变量、Qt 插件路径覆盖，PATH 仅安装目录/plugins 与 Windows 系统目录，仍正常运行。个人工程与配置未改。
- F6-125 / F6-200：标准控件与最拥挤 SVS 窗口有限抽查，各 4 PASS；200% 工具栏可横向滚动到 Settings/Properties，参数侧栏可纵向滚到 Mode/Gain。所有缩放为 QT_SCALE_FACTOR 模拟，真实跨屏 DPI 仍 MANUAL/PENDING；中文 fixture/标签没有方块字，不把系统 locale 当作完整中文翻译/IME 验收。
- 第 12.1 节逐条结果、截图索引、安装清单、人工/PNG/外部范围例外和回滚步骤见 [delivery-audit.md](delivery-audit.md)。F0–F2 完成条件和 B/D 历史 follow-on 状态同步为已有实际证据，不增加新功能。经典/现代主题切换只列未来计划。
- 人工验收、听感、外部编辑器和需要修改 PNG 的项目按用户要求跳过并注明；不能称 ACCEPTED。三个其他任务未提交文件继续保留。完成本检查点提交/推送和远端 SHA 核对后，按用户新增要求制作全量替换包。

## 完成后的全量替换包

已制作 `build/packages/lmms-modern-ui-full-7b44c5487-win64.zip`（60,648,335 bytes）。3480 个运行文件、4 个安装/说明文件；ZIP 全 entry 哈希校验、Windows PowerShell 校验/实际原目录覆盖、配置保留检查及覆盖后原生实窗 3 PASS 均完成。详见 [replacement-package.md](replacement-package.md) 与 validation/package-artifact.json。此包不包含个人配置/工程；旧包外插件 DLL 原地停用以避免 ABI 混用，未实际覆盖用户的原安装版。
