# LMMS 界面现代扁平圆角化计划书

版本：1.0　日期：2026-10-06　源码调研基线：`7f54e8ea0`

状态：**计划书已编写，UI 改造尚未实施**。本文是后续开发的执行依据；不把已有主题能力、原有测试通过或本文完成等同于界面改造完成。

执行补充（2026-10-06，用户明确要求）：阶段提交直接推送 `origin/master`；人工验收直接跳过并标 `MANUAL/PENDING`；不修改 PNG，必须重绘 PNG 且未用 SVG 替代的项标 `SKIPPED / PNG`。允许适合矢量表达的图标、边框及装饰用 SVG 重绘替代。以上跳过项作为明确范围例外记录，不计为视觉改造 PASS。阶段实际证据与进度见 `doc/ui-modernization/acceptance.md`。

## 1. 目标与范围

在现有 **Qt Widgets + Theme CSS/QSS + QPainter** 体系内，将 LMMS 界面统一为现代、扁平、适度圆角的桌面音频工作站。继续使用默认主题入口、主题目录回退和现有数据模型，不建立独立皮肤引擎，不迁移到 QML、Web 或另一个 GUI 框架。

本计划中的“CSS”均指 Qt Style Sheets（QSS），不是浏览器 CSS。最终效果不能只停留在设置页：主窗口、工具栏、浏览器、轨道区域、Pattern Editor、普通 Piano Roll、Automation Editor、SVS Piano Roll、Mixer、仪器窗口公共页、效果器机架、控制器机架及 LMMS 自有插件面板都要进入覆盖清单。

### 1.1 必须达成

1. 去除默认主题中的装饰性渐变、厚重浮雕、双重明暗边框和文字阴影；以纯色层级、细边框、间距和状态色表达结构。
2. 按本文清单区分：**A：直接 QSS；B：现有自绘属性可配置；C：必须替换或修改自绘层；D：资源及固定布局适配；E：宿主不可控制的边界**。
3. 按钮、输入框、菜单项、标签页、面板和片段外框有一致圆角规则；数据画布保持读数和编辑精度。
4. 普通、悬停、按下、选中、键盘焦点、禁用和只读状态清晰，缩放后字体和 SVG 清晰，无裁切、重叠或失去点击区域。
5. 现有鼠标操作、快捷键、数值范围、自动化绑定、撤销、播放、合成及工程持久化不因换肤改变。
6. 提供真实开发版窗口截图、必要自动回归、资源独立性检查和按阶段提交记录。

### 1.2 边界

- 可以修改宿主控件及自有插件的 **View、布局、paintEvent、主题属性和视觉资源**；不能借机重构 Audio Engine、Mixer 运算、Automation 数据模型、InstrumentTrack、VST/Carla 桥接、SVS 调度/SDK 或音频算法。
- 第三方 VST/VST3、Carla 外部编辑器、系统原生文件对话框和 Windows 原生标题栏不保证受 QSS 控制。只统一 LMMS 所有的容器和入口，保留原生窗口行为；不得用截图覆盖或强行代理这些窗口。
- 不增加新编辑功能、GPU 后端、动画系统、主题商店、布局预设或新的快捷键。发现相邻问题记 follow-up。
- 本计划允许普通 Piano Roll 等模块的必要视觉修改，但不合并普通与 SVS 编辑器的控制器或交互状态机。SVS 已确认的 TuneLab 操作语义保持不变。
- 原主题资源未确认无引用前不得删除；兼容主题或第三方自有 artwork 可以保留旧资源路径。现代默认主题覆盖是验收对象，不要求重新设计第三方作者的独立皮肤。

## 2. 已核实的代码事实

以下结论来自当前工作区的 CodeGraph 符号定位及目标文件核对，不是根据截图猜测。

| 入口或模块 | 当前事实 | 对实施的约束 |
|---|---|---|
| [GuiApplication.cpp](src/gui/GuiApplication.cpp)、[ConfigManager.cpp](src/core/ConfigManager.cpp) | 初始化 `LmmsStyle`、`LmmsPalette`；`artwork:` / `resources:` 搜索当前主题及默认主题 | 沿用主题发现与回退；新图标放主题目录，不写本机绝对路径 |
| [LmmsStyle.cpp](src/gui/LmmsStyle.cpp) | 读取 `resources:style.css`；监听文件并重载 QSS；基类使用 Fusion；`drawPrimitive()` 仍绘制多层阴影边框；含标题栏及固定 pixelMetric 逻辑 | QSS 与代理样式必须分工，不能假设换 CSS 后所有凹凸边框自动消失 |
| [LmmsPalette.cpp](src/gui/LmmsPalette.cpp) | 通过 Q_PROPERTY 取主题颜色，再生成 QPalette | 自绘颜色继续接入 palette / qproperty；QSS 重载不等于所有缓存和全局 palette 已同步 |
| [style.css](data/themes/default/style.css) | 已有圆角输入框、SVG、clip `qproperty-gradient: false`，也仍有工具按钮、琴键和子窗口渐变 | 保留已有可用能力；不要把已关闭的 clip 渐变再列为必须开发的新接口 |
| [Knob.cpp](src/gui/widgets/Knob.cpp)、[Knob.h](include/Knob.h) | 有 `Styled` 矢量分支与位图分支；已有半径、线宽、弧线颜色属性；Styled 当前有尺寸/标签限制和绘图缓存 | 可复用 Styled，但不能用全局选择器强行切换所有历史旋钮 |
| [Fader.cpp](src/gui/widgets/Fader.cpp)、[Fader.h](include/Fader.h) | 电平由 QPainter 绘制，推子帽为位图；已有 peakOk/peakWarn/peakClip 属性；位置与 dB 映射相关 | 外观与数值映射必须分开，不能简单换成普通 QSlider |
| [ComboBox.cpp](src/gui/widgets/ComboBox.cpp) | LMMS 自绘 `ComboBox` 含硬编码文字阴影、箭头位图和框线 | `QComboBox { ... }` 不覆盖这个类；必须处理其绘制层 |
| [LcdWidget.cpp](src/gui/widgets/LcdWidget.cpp) | 按字符格从位图裁出数字，已有 textColor 主要用于标签 | 改 font 或文字颜色不能把位图数字变成现代字体 |
| [TabWidget.cpp](src/gui/widgets/TabWidget.cpp)、[GroupBox.cpp](src/gui/widgets/GroupBox.cpp) | LMMS 自有类自行画矩形、标题和标签区，存在固定像素计算 | `QTabWidget::pane` / `QGroupBox` 规则不能替代其内部绘制 |
| [LedCheckBox.cpp](src/gui/widgets/LedCheckBox.cpp)、[PixmapButton.cpp](src/gui/widgets/PixmapButton.cpp) | 直接绘制 on/off 图像；PixmapButton 继承 AutomatableButton | 替换视觉必须保留模型绑定、菜单、双击及按钮状态语义 |
| [SideBar.cpp](src/gui/SideBar.cpp)、[TrackLabelButton.cpp](src/gui/tracks/TrackLabelButton.cpp) | SideBarButton 虽有 paintEvent，仍通过 QStylePainter 画工具按钮；TrackLabelButton 最终委托 QToolButton | 不能把所有出现 paintEvent 的类都误判为需要重写；先用 QSS 验证 |
| [SubWindow.cpp](src/gui/SubWindow.cpp) | 负责 MDI 与分离窗口、标题区域和 attach/detach | 圆角不能破坏边缘缩放或关闭后回嵌；分离窗口原生框不属于 QSS 圆角承诺 |
| [CPULoadWidget.cpp](src/gui/widgets/CPULoadWidget.cpp) | 缓存背景与 LED 位图，自有 100 ms 更新周期 | CSS 背景声明不足以替换实际位图；改显示，不改采样/平滑逻辑 |
| [PianoView.cpp](src/gui/instrument/PianoView.cpp) | 仪器窗口底部键盘仍直接使用琴键位图 | 它与普通 PianoRoll 的键盘绘制是两个入口，必须分别覆盖 |
| [TripleOscillator.cpp](plugins/TripleOscillator/TripleOscillator.cpp) | 自有插件示例使用整张 artwork、绝对定位和自有按钮；部分旋钮已 Styled | 自有插件面板不能仅靠全局 QSS 宣称完成，需逐项登记布局/资源工作 |
| [themes/CMakeLists.txt](data/themes/CMakeLists.txt) | 主题安装规则包含 png、svg、css | 新 SVG 应走现有安装规则；仍需检查实际安装产物与引用 |

当前已做的是关键入口与控件类别调研，**尚未逐个枚举所有自有插件面板**。M0 的受控清单是实施范围闭合的必要步骤；不得把“未调研”登记成“已兼容”。

## 3. 视觉规格

本节给出可直接执行的初始值。M0 只允许基于同屏对比做一次规格校准并记录最终值，后续按此执行，避免各窗口各定一套风格。

| 项目 | 初始规格 |
|---|---|
| 背景层次 | 画布 `#14181D`；窗口 `#20262D`；控件/面板 `#29323B`；悬停 `#35414D` |
| 文字 | 正文 `#E5EAF0`；次要 `#A8B4C0`；禁用 `#6F7B87`；不画位移阴影 |
| 强调与反馈 | 沿用 LMMS 绿色系 `#42A065`；选中底色 `#244A35`；错误 `#E05B65`；警告 `#DAB55A`；状态同时有图标、文字或轮廓 |
| 圆角 | 普通按钮/输入框 4 DIP；菜单项 4 DIP；面板 6 DIP；片段 3 DIP；音符最多 2 DIP；极短音符自动减小半径 |
| 边框 | 默认 1 DIP；焦点通过边框颜色或内侧描边增强，不通过增厚边框改变布局 |
| 间距 | 基本单位 4 DIP；控件间 4–8 DIP；面板内边距 8 DIP；组间距 12 DIP |
| 控件密度 | 普通表单高度目标 28 DIP；编辑工具栏目标 28–32 DIP；轨道紧凑按钮保留独立 20 DIP 级别，不用全局 min-height 撑坏轨道 |
| 图标 | 工具栏按 20/24 DIP 槽位；设置分类保持 48 DIP；白色单色 SVG 为主，活动/禁用由状态底色和适当明度区分；声库头像不强制单色 |
| 字体 | 正常文本以系统 UI 字体 9–10 pt 为起点；中文回退有效；数字用系统可用等宽字体或等宽数字度量，不引入必须安装的新字体 |
| 图形规则 | 网格、播放线、曲线保留细直线；小节线明显强于拍线、拍线强于细分线；抗锯齿用于曲线/圆角，不让网格线整体发糊 |

“现代圆角化”不要求把每条轨道、每个琴键都切成互相分离的卡片。连续轨道行和时间标尺保持连续；可交互数据的时间边界、音高位置、波形振幅和选区不允许因视觉留白而改变。

## 4. CSS 可直接修改清单（A）

主文件为 `data/themes/default/style.css`。以下工作优先只改 QSS/图标资源；若实际控件被固定尺寸限制，则将布局修正记入 D 类，不把它伪装成纯 CSS。

| ID | 控件/区域 | 直接执行的改动 | 必查项 |
|---|---|---|---|
| A01 | QPushButton、QToolButton、TabButton、设置分类 | 去渐变；统一纯色、4 DIP 圆角和状态；清理已有 `#btn`、mute/solo 等高优先级覆盖 | checked 与 pressed 可区别；icon-only 和 menu-button 不被裁切 |
| A02 | QLineEdit、QTextEdit、QPlainTextEdit | 纯色输入底、细边框、选区、focus/readonly/disabled；限制选择器范围 | 中文输入、选区、只读复制、错误提示可见 |
| A03 | **Qt** QComboBox、QSpinBox、QDoubleSpinBox | 框体、箭头/增减子控件、弹出列表及焦点 | 可编辑组合框、下拉列表文字、上下箭头命中区；不能代替 C01/C02 |
| A04 | QCheckBox、QRadioButton | 扁平 indicator，SVG 勾/圆点，indeterminate 与禁用状态 | 键盘 Space 和原有切换行为保持 |
| A05 | QSlider、QProgressBar、QScrollBar | 纯色 groove/handle、圆角、最小柄尺寸和进度文本 | 横竖方向、0/100%、page step、滚轮、箭头区域 |
| A06 | QMenu、QMenuBar、QToolTip | 去顶部装饰条和阴影感；菜单项圆角、选中与分隔线统一 | 子菜单箭头、快捷键、勾选和长中文；原生系统菜单排除 |
| A07 | QTreeView/QTreeWidget、QListView/QListWidget、QHeaderView | 行背景、选中/hover、表头分隔、展开箭头 SVG | 选中与失焦选中；branch、排序箭头及横向滚动 |
| A08 | **Qt** QTabWidget/QTabBar、QGroupBox、QFrame、QScrollArea | 平面标题、面板边框、内外间距 | 标签过多可滚动；滚动视口与外框无异色；不能代替 C03/C04 |
| A09 | QMdiArea、主工具栏及窗口内容容器 | 背景分层、分隔线；避免大面积纹理和浮雕 | 不覆盖编辑画布自行绘制的颜色；不改变 docking/MDI 行为 |
| A10 | SideBar 的 QStyle 工具按钮、TrackLabelButton | 复用工具按钮状态与扁平外观 | 竖排旋转文字、紧凑轨道、重命名框尺寸 |
| A11 | SetupDialog、ExportProjectDialog、SVSSettingsPage 的标准控件 | 应用 A01–A08；SVS/VST 分类、设备禁用和 AI 滑块状态保留 | 长引擎名、长设备名、滚动和默认值；全局规则不挤压 48 DIP 分类图标 |

QSS 实施顺序：基础面板 → 按钮 → 输入 → 列表/菜单 → 滚动 → 特例。修改已有规则，避免在文件尾不断追加同权重覆盖。禁止 `QWidget { border-radius: ... }` 一类无差别规则；不要为无边框的数据画布添加 padding。

## 5. 现有属性可调，但形状不由 CSS 决定（B）

| ID | 对象与路径 | 当前可以直接改 | 必须另做的部分 |
|---|---|---|---|
| B01 | `LmmsPalette`：include/LmmsPalette.h、src/gui/LmmsPalette.cpp | 现有颜色属性 | 若重载后 palette 未更新，在 M2 修正主题应用入口；不另建颜色文件格式 |
| B02 | `KnobType::Styled`：include/Knob.h、src/gui/widgets/Knob.cpp | 半径、中心、线宽、outerColor、line/arcActive/InactiveColor | 标签、固定尺寸、缓存和非 Styled 分支见 C05 |
| B03 | `Fader`：include/Fader.h、src/gui/widgets/Fader.cpp | peakOk/peakWarn/peakClip、unityMarker 等 | 推子帽、边界形状及缓存见 C06 |
| B04 | 普通 PianoRoll：include/PianoRoll.h、src/gui/editors/PianoRoll.cpp | 音符、选中、ghost、网格、琴键 brush；把琴键渐变改纯色 | 音符圆角或固定几何需要局部 painter 修改；不改手势算法 |
| B05 | AutomationEditor：include/AutomationEditor.h、src/gui/editors/AutomationEditor.cpp | 网格、节点、切线、曲线、比例尺、ghost 色 | 如有残余硬编码边框，局部补属性；曲线求值保持原样 |
| B06 | ClipView 及 Midi/Automation/Sample 派生 View | `gradient: false`、选中、静音、文字/文字阴影等 | 默认主题已禁用 gradient；圆角边框、内容裁剪及旧双层边框需要 C08 |
| B07 | TrackContentWidget、TimeLineWidget | 已有网格、循环区、标尺颜色及部分宽度属性 | 圆角循环范围、位图播放头和边缘命中须按实际 painter 处理 |
| B08 | SVSPianoRoll：include/SVSViews.h、src/gui/editors/svs/SVSCanvas.cpp | 背景、音符、状态、小节/拍线等现有主题属性 | 音符圆角与参数栏局部布局；保留只读分界、滚动、头像/立绘功能 |
| B09 | 自绘 TabWidget、SubWindow、EnvelopeGraph/LfoGraph 等已有主题属性 | 现有颜色/brush | 属性能换色不代表 QSS border-radius 能改变其内部图形；仍按 C/D 检查 |

对 B 类先改已有属性并截图，缺少哪个视觉参数才新增哪个 Q_PROPERTY。新增属性的 setter 必须触发必要的缓存失效和 update；需要几何变化时才 updateGeometry。不要依赖 `qproperty-*` 在 `:hover` / `:pressed` 中自动实时重算，自绘状态应由控件状态驱动并选择已配置的状态色。

## 6. 必须修改或替换自绘层清单（C）

这里的“替换”默认指 **替换旧 paintEvent/位图绘制分支**，保留公共类、模型绑定、信号及交互。只有通过调用点核查证明不存在自动化或专有行为时，才可局部改用标准 Qt 控件；不能批量替换类名。

| ID | 文件入口 | 必需实现 | 保留和验收 |
|---|---|---|---|
| C01 | src/gui/widgets/ComboBox.cpp、include/ComboBox.h | 去硬编码阴影与立体分隔；画纯色圆角框、单层文字和 SVG 箭头；补 theme 色/圆角属性 | ComboBoxModel、滚轮、菜单选项、图标、控制器/自动化上下文菜单 |
| C02 | src/gui/widgets/LcdWidget.cpp、include/LcdWidget.h | 为现代默认主题提供 QPainter 文本数字模式，替换数字 sprite；按字体度量安排数字、负号、边距；保留 legacy 位图回退 | LcdSpinBox/LcdFloatSpinBox 的拖动、进位、小数、单位、范围、无缝拼接和输入弹窗；不改数值模型 |
| C03 | src/gui/widgets/TabWidget.cpp、include/TabWidget.h | 圆角选中标签、纯色标题区、单层文字；绘制与 findTabAtPos 使用同一组几何 | 稀疏 tab id、图标标签、caption、切页信号、现有页面所有权 |
| C04 | src/gui/widgets/GroupBox.cpp、include/GroupBox.h | 纯色圆角面板和标题分隔，按字体计算标题高度；去 darker 浮雕矩形 | 可切换 LED 标题和内容 enable 关系；不误用普通 QGroupBox 覆盖它 |
| C05 | src/gui/widgets/Knob.cpp、include/Knob.h | 复用 Styled 的矢量弧/指针；补齐标签/尺寸与 disabled/focus；对登记的位图旋钮转现代绘制；尺寸/主题/DPR 变化清缓存 | FloatModelEditorBase、精细拖动、滚轮、双击输入、reset、自动化菜单和对数参数；绘图时不写 model |
| C06 | src/gui/widgets/Fader.cpp、include/Fader.h | 矢量纯色推子帽、细槽和电平条；显示尺寸与实际命中几何一致 | 原 dB/线性映射、-inf、峰值保持、0 dB 线、修饰键步进、复制链接及撤销 |
| C07 | src/gui/widgets/LedCheckBox.cpp、src/gui/widgets/PixmapButton.cpp | 对通用 LED/开关用矢量 indicator 或现代 SVG 状态；必要时新增 opt-in 视觉模式，保持旧 artwork 调用可用 | AutomatableButton、on/off/pressed、只读/禁用、双击和信号次数；不简单换 QCheckBox |
| C08 | src/gui/clips/MidiClipView.cpp、AutomationClipView.cpp、SampleClipView.cpp、include/ClipView.h | 引入默认 0 的 clip 圆角属性，默认现代主题设 3 DIP；替换双层矩形边框、按同一圆角路径裁剪内容；检查 Pattern/SVS clip 各自分支 | 波形/步进/音符预览不变；短 clip 半径收缩；原拖动/裁剪命中矩形不缩小；静音和选中仍明显 |
| C09 | src/gui/widgets/CPULoadWidget.cpp、include/CPULoadWidget.h | 用矢量纯色条与文本替代背景/LED sprite，尺寸从现有工具栏约束计算 | 负载采样、100 ms 刷新、平滑和 tooltip 原样保留；不改 AudioEngineProfiler |
| C10 | src/gui/instrument/PianoView.cpp | 替换或扁平化位图琴键，必要时用 QPainter 纯色键；补主题颜色和 DPR 处理 | 黑白键命中、按下/禁用/根音/范围标记与 MIDI 键盘演奏保持一致 |
| C11 | src/gui/widgets/Graph.cpp 及 M0 清单中的图表 View | 对硬编码背景、框线和 foreground artwork 增加最少主题入口，改平面外框 | 波形/包络/LFO 的数据及拖动算法不变；数据曲线不强制圆角化 |
| C12 | src/gui/LmmsStyle.cpp | 处理现代主题下 PE_Frame/PE_FrameLineEdit/PE_PanelLineEdit 残余浮雕；用适用的 QStyle 委托或单层绘制；按 M0 实测修改必要 pixelMetric | 不重复画 QSS 已接管的边框；标题栏高度、菜单指标、平台窗口操作不回归 |
| C13 | src/gui/SubWindow.cpp、include/SubWindow.h | MDI 外框与标题区纯色；若圆角属性不足，最小增加自绘属性；内容避免越过圆角外框 | 不切掉 resize 热区；关闭、最大化、分离、回嵌、状态恢复保持；不新增全套无框顶层窗口 |

对 C02/C05/C07 的 legacy 回退，使用现有模式或局部主题属性选择，默认值保持旧调用兼容，现代默认主题明确开启新外观。不得为此给每个控件加一套独立用户设置。

## 7. 资源与布局适配清单（D）及平台边界（E）

| ID | 范围 | 执行方式 |
|---|---|---|
| D01 | 默认主题功能图标、箭头、LED 和有立体背景的按钮资源 | 按实际引用登记；纯功能符号优先 SVG；不把纯白图标缩在过大的 viewBox 中；状态资源命名成组；C++ 与 QSS 引用同步 |
| D02 | SetupDialog / TabBar、工具栏、轨道控制区、MixerChannelView、仪器公共页 | 先处理 fixedSize、固定边距与字体度量冲突，再调 QSS padding；保持紧凑模式；容器不足优先使用已有滚动机制 |
| D03 | 自有 instrument/effect 插件面板 | M0 从当前启用的构建目标列出面板，逐个记录源码、artwork、绝对定位、共享控件；按组迁移，不能把 TripleOscillator 示例替代全部覆盖 |
| D04 | 插件 artwork 中的面板边框、文字、旋钮槽 | 将装饰背景拆成纯色面板；功能标签改成可翻译 QWidget/QPainter 文字；保留有实际内容意义的图像；绝对坐标依赖必须随 View 调整 |
| D05 | 主题缓存、图标大小和 DPI | 沿用 embed::getIconPixmap / logicalSize；SVG 指定逻辑尺寸；检查 Knob、LCD、CPU/clip 的缓存键或失效路径，避免放大旧 pixmap |
| D06 | 安装与配置 | 新 CSS/SVG 走现有 data/themes 安装；开发安装保持独立配置；不能依赖源码绝对路径或用户 Desktop 图片 |
| E01 | 第三方 VST/Carla 原生内容、系统对话框 | 仅验证宿主容器、弹出/嵌入、关闭与焦点；截图标注为外部 UI，不虚报已换肤 |
| E02 | 分离后的 Windows 标题栏和系统阴影 | 由系统窗口装饰负责；原生关闭、拖动、缩放优先；不将系统圆角差异列为计划失败 |

M0 的插件清单限定为本仓库当前构建配置启用的 **LMMS 自有 UI**；未启用的自有目标单列“未构建”，第三方 vendored UI 单列“外部”。最终报告必须给出覆盖数量和这些明确例外，不能只写“所有插件已现代化”。

## 8. 实施约束与局部接口

1. **单一主题来源**：颜色和可调尺寸写入当前主题；QPainter 从 palette / 现有或新增 Q_PROPERTY 读取。QSS 不引入 CSS variables、box-shadow、flex 等浏览器语法，不新建运行时 token 编译器。
2. **有限复用**：只有多个已纳入范围的控件重复同一视觉原语时，才提取小型绘制 helper；不建设通用 UI 组件库或改全项目继承关系。
3. **不触碰数据通道**：View 换肤不修改 project XML 属性、参数 id、SVS 输入 JSON、自动化端点、播放逻辑或插件 ABI。
4. **几何一致**：新圆角可视路径不缩小原编辑命中矩形；标题高度、图标槽、文字区与点击位置共享测量结果，不能只移动画出来的图形。
5. **状态完整**：自绘控件自己响应 enabled/focus/hover/style/font/resize/DPR 的必要事件；不把所有状态都交给一次性的 qproperty 设置。
6. **缓存有界**：主题、尺寸、字体或 DPR 变化时失效相关缓存，模型数值变化复用可复用部分；paintEvent 不读磁盘、不解析 SVG、不进行合成、不分配无界缓存。
7. **主题重载**：先验证现有 watcher；仅修正本轮新增外观在重载后失效的问题。资源不能热刷新的部分允许记录“重启后生效”，但正式启动必须一致，不能留下混合 palette。
8. **字体优先度量**：QSS 改字体后清查 fixedSize 和源码 `adjustedToPixelSize`；中文/长设备名不被藏掉。必要省略显示 tooltip，不能靠缩小到难读字体解决。
9. **兼容样式**：新增属性默认回退安全；不修改第三方独立控件的模型。旧主题中缺少新 SVG 时走默认主题回退。

## 9. 分阶段执行（F0 → F6）

使用 F 前缀与此前 SVS 的 M0–M5 区分。每阶段完成必要自动验证、更新本文状态与证据索引、单独 commit/push，然后进入下一阶段。阶段达到以下完成条件即结束，不追加无关测试矩阵。

### F0：冻结范围、基线与视觉规格

**输入**：本计划、当前源码与开发版。**允许改动**：文档、fixture 和后续测试所需最小登记，不改产品行为。

步骤：
1. 按 A/B/C/D/E 建 `doc/ui-modernization/inventory.md`，每项包含路径、类/函数、绘制方式、现有属性、调用页面、动作、阶段、验收编号。展开 D03 自有插件清单，按当前构建配置封口。
2. 只启动已配置主题的开发版。使用原生 Qt 测试截图建立 S01–S08 基线；记录 Qt、主题、窗口大小、语言、DPI 和源码版本。
3. 将第 3 节值整理为 `doc/ui-modernization/visual-spec.md`，一次校准后冻结；记录紧凑轨道与普通表单的不同密度。
4. 建立一个可重开工程 fixture：至少有 MIDI、Pattern、Sample、Automation、SVS 轨道、两个 Mixer 通道、仪器/效果/控制器面板。使用仓库自有或程序生成的小资源，避免个人声库路径。
5. 记录当前工作区无关未提交修改，不混入本计划提交。

**完成条件**：inventory 没有未分类的已启用自有面板；S01–S08 基线与视觉规格可用；无新增 GUI 框架。状态：TODO。

### F1：标准控件与主题资产

**范围**：A01–A11、D01 中标准控件使用的资源；主要 `style.css` 和默认主题 SVG。

步骤：按第 4 节顺序修改现有规则；合并冲突选择器；为输入框/按钮/菜单补全状态；保持白色 SVG 与统一槽位；在设置、导出、浏览器和 SVS 设置中验证。

**完成条件**：S01/S02/S07 的标准控件已达到规格，焦点可见，禁用/只读明确；没有因 padding 改变导致不可达按钮；CSS 不支持的自绘问题已对应 C 项而非继续堆选择器。证据：状态截图、资源解析检查。状态：TODO。

### F2：主题属性、代理样式和自绘外框

**范围**：B01/B06/B09、C01/C03/C04/C12/C13、D05 的必要缓存处理。

步骤：先解决 LmmsStyle 与 QSS 的边框分工；再改 ComboBox、TabWidget、GroupBox 和 MDI 外框；按需加 Q_PROPERTY；检查重载/重新打开后样式一致；保护 detach/attach。

**完成条件**：自绘框无残余立体边框；标签绘制与点击命中一致；子窗口关闭/最大化/分离回嵌通过；旧主题缺属性有有效回退。证据：S02/S03/S07 与 ThemeWidgetTest 对应交互结果。状态：TODO。

### F3：旋钮、推子、数字与小型显示

**范围**：B02/B03、C02/C05/C06/C07/C09/C10/C11。

步骤：先补现代 Styled 旋钮和 LCD 文本模式，再迁移已登记通用调用；推子/LED/CPU/键盘逐一替换绘制；图表仅改外框和主题色；保留所有模型和输入路径。

**完成条件**：增益旋钮、对数旋钮、推子 -inf/0 dB、带小数 LCD、LED 切换、琴键都正常；数值拖动撤销和自动化绑定通过；主题/DPI 变化不会显示旧缓存。证据：S06/S08、状态与行为断言。状态：DONE / MANUAL-PENDING；自动证据见 doc/ui-modernization/acceptance.md 的 F3 检查点。

### F4：编曲与编辑画布视觉

**范围**：B04–B08、C08、D02 的编辑器部分；Song/Pattern、普通 PianoRoll、AutomationEditor、SVSCanvas 的绘制与必要布局。

步骤：统一网格和标尺层级；clip 圆角路径与内容裁剪；音符轻圆角；更新选择/ghost/静音状态；统一工具栏与侧栏密度；保持 SVS 参数分界、滚动、头像/立绘、音高与参数曲线。

**完成条件**：S03/S04/S05 中时间/音高对齐正确；短音符、最窄 clip 和缩放不丢失内容；创建/选择/移动/两端拉伸/复制粘贴/撤销行为不变；SVS 既有相关回归通过。证据：坐标断言、实窗截图及操作记录。状态：TODO。

### F5：宿主面板与自有插件覆盖闭合

**范围**：D02–D04、F0 冻结的自有插件清单。

步骤：先统一 Mixer/机架/仪器公共页，再逐组处理内置插件 artwork、固定布局和私有控件；每个清单项记录替换后的入口、截图和构建目标。第三方 E 项只验证宿主边界。

**完成条件**：F0 已启用的 LMMS 自有 UI 每项均有 PASS 或明确的人工待验收证据，不留“后续适配”的技术缺口；插件声音/参数绑定与预设加载保存未改变。外部和未构建项有单独清单。状态：TODO。

### F6：整体验证、安装与交付

**范围**：仅修复本计划造成的界面回归；更新文档和默认主题安装产物。

步骤：按第 10 节有限矩阵做最终回归；安装到独立开发目录；从安装目录启动复核主题/资源；关闭测试窗口；完成覆盖表、截图索引、遗留人工项和回滚记录；最后 commit/push。

**完成条件**：第 12 节逐条有证据；无未完成 C/D 技术项；构建与必须自动回归通过；原安装版未被覆盖。若仅人工观感未验收，交付状态写 `IMPLEMENTED / MANUAL-PENDING`，不得写“人工已通过”。状态：TODO。

## 10. 验证规格

### 10.1 实窗场景（固定八组，按受影响阶段执行）

| 编号 | 场景 | 核心验收 |
|---|---|---|
| S01 | 主窗口、菜单、主工具栏、浏览器 | 字体、图标、状态、菜单快捷键、侧栏方向与选中 |
| S02 | Settings 的 General/Audio/Paths/VST/SVS、导出对话框 | 输入/下拉/slider/checkbox；引擎名称与设备禁用；长文本和滚动 |
| S03 | Song Editor、Pattern Editor、多个 clip/轨道 | 片段外框、步进、轨道控制、紧凑模式与滚动；不改变轨道实际高度数据 |
| S04 | 普通 PianoRoll、AutomationEditor | 网格、键盘、音符、节点、ghost、框选、缩放和滚动 |
| S05 | SVS Piano Roll、SVS 插件界面 | 音高/参数曲线、只读分界、头像/透明立绘、标签滚动、关闭回嵌 |
| S06 | Mixer、效果/控制器机架 | 推子/峰值、旋钮、启用/禁用、0 dB/-inf、长效果名称 |
| S07 | MDI 与分离窗口 | 标题与按钮、最大化、拖动、边缘缩放、关闭回嵌、窗口恢复 |
| S08 | 仪器公共页及 D03 所有自有面板 | LCD、LED、琴键、固定 artwork 标签、共享及私有控件、预设操作 |

以 100% 和 150% 缩放对八组场景各做一次截图；窗口布局以 1280×800 DIP 为基准并适应实际可用屏幕。中文和英文只需覆盖包含长文本的 S01/S02/S05/S08。125% 与 200% 对代表性控件页和最拥挤窗口做抽查，不扩展成所有状态的笛卡尔积。自动设置 Qt scale factor 的截图须注明为模拟缩放；跨屏真实 DPI 切换若设备不具备，登记 MANUAL/PENDING，不伪称已测。

### 10.2 必要自动化

- F0/F2 起新增 **计划中的** `tests/ui/ThemeWidgetTest.cpp`，在 `tests/CMakeLists.txt` 注册 `ThemeWidgetTest`。该目标目前不存在，必须创建和成功编译后才能执行后文命令。
- ThemeWidgetTest 复用真实控件与数据模型，验证：主题能加载、关键 SVG 非空；有标签控件未裁切；自绘 tab 命中与画面一致；Knob/LCD/Fader 编辑结果与原契约一致；主题/字体/DPR 更新后缓存有效；窗口生命周期正常。
- 颜色/圆角像素断言只作为辅助，避免整窗逐像素 golden test 受字体抗锯齿影响。截图必须由原生已暴露窗口获取，不能用 QWidget::render 图替代实窗验收。
- 复用现有 `SVSIntegrationTest` 的受影响用例：`embeddedWindowLifecycle`、`canvasThemeProperties`、`compactEditorLayoutAndNoteLabels`、`parameterPanelStateAndFocus`；F4/F6 跑完整套。测试列表若源码变化，以当前 slots 为准，不依赖历史通过数量。
- 自动化模型有变更风险时运行现有 `AutomatableModelTest`。视觉改造不为无关 VST/DSP 添加新测试矩阵；宿主嵌入操作在 S07 做必要冒烟。
- 不写只检查“代码包含某字符串”的产品测试代替行为；不因纯图标修改额外建设测试框架。

### 10.3 必须保留的操作契约

参数：拖动、精细调整、滚轮、键盘、双击数值输入、重置、上下文自动化/控制器入口与撤销。编辑器：创建、选择、移动、拉伸、复制粘贴、删除、缩放滚动及工具切换。SVS：已有歌词输入/批量填词、音素、自由曲线绘制和右键重置语义。换肤前后的相同输入序列应产生相同模型结果；操作体验人工项可登记待验收，不能以此跳过可自动验证的数据契约。

### 10.4 测试环境纪律

- 仅开发版 LMMS，先配置本轮默认主题；不打开原安装版，不用 offscreen，包括子进程。
- 所有 Qt GUI/widget/integration 验证都必须真正 show 窗口；权威流程为 **Build → Launch real window → Wait for stable rendering → Interact → Capture/inspect → Record → Close**。等待字体、布局、主题、图像和动态控件稳定，再截图；不以刚启动时的瞬态画面作验收依据。
- 禁止通过环境、`-platform` 参数、代码、CMake/CI/wrapper 强制离屏 QPA，禁止用 QOffscreenSurface 或隐藏帧缓冲替代真实窗口；启动失败不允许自动回退。中文/Unicode 场景须明确检查无 tofu/方块字。
- 当前外观开发不使用 Computer Use；原生 Qt 测试可直接抓取真实窗口。后续如用户要求对比 TuneLab 操作语义才使用 Computer Use，用完关闭窗口。
- 不默认禁用产品输入法。若测试进程遭本机输入法扩展干扰，可单独诊断后临时设置进程环境，但证据必须写明，真实中文 IME 验收仍为待验收项。
- 头像/立绘使用仓库 fixture 或程序生成资源；个人声库图像仅可本机额外体验，不能进入安装/发行依赖。
- 已知不影响后续工作的纯人工观感项记 `MANUAL/PENDING` 后继续；编译失败、控件无法操作、资源缺失、数据契约失败不能降级成人工项。
- 实窗无法启动时依次检查日志、QPA/plugin 部署、字体、DPI 和环境，修复真实原因；仍无法执行则按受影响用例标 `MANUAL/PENDING`，不得声称 GUI PASS。禁止只为让测试容易通过而修改产品字体、布局或增加专用渲染分支。
- 重复 GUI 检查使用有上限的小批次与可配置次数，每轮关闭窗口并回收进程；每个要求状态只保留代表截图。资源不足时缩小批次，不能改用 offscreen。

## 11. 构建、测试与安装执行模板

遵守当前 AGENTS.md。每次在前台 PowerShell 执行，先 dot-source 环境脚本；标准输出和错误都经过 `Tee-Object -Encoding utf8`；立即保存并检查 `$LASTEXITCODE`，失败时读 build.log。不要 Start-Process 后台构建，也不要静默执行。

以下基于当前已有 `build` 配置（`BUILD_TESTING=ON`）。F0/F1 使用已存在的 `lmms SVSIntegrationTest AutomatableModelTest` 构建目标和 `^(SVSIntegrationTest|AutomatableModelTest)$` 测试正则；`ThemeWidgetTest` 仅在 F2 创建后加到目标和 CTest 正则。实际启用的插件目标从 F0 清单追加，不凭空编造目标名。

```powershell
. ./buildtools/Enter-LmmsEnvironment.ps1
$env:QT_QPA_PLATFORM = 'windows'
$env:LMMS_DATA_DIR = Join-Path $PWD 'data'
$env:LMMS_SVS_PLUGIN_DIR = Join-Path $PWD 'build/svs'
$env:PATH = "$env:QTDIR/bin;$(Join-Path $PWD 'build/Release');$env:PATH"
$uiEvidence = Join-Path $PWD 'doc/ui-modernization/validation'
New-Item -ItemType Directory -Force -Path $uiEvidence | Out-Null

# F2 已创建 ThemeWidgetTest 后使用。
& cmake --build build --config Release --target lmms ThemeWidgetTest SVSIntegrationTest AutomatableModelTest --parallel 8 2>&1 |
    Tee-Object -FilePath 'build.log' -Encoding utf8
$buildExitCode = $LASTEXITCODE
Copy-Item -LiteralPath 'build.log' -Destination (Join-Path $uiEvidence 'build.log')
if ($buildExitCode -ne 0) { Get-Content -LiteralPath 'build.log' -Tail 80; exit $buildExitCode }

& ctest --test-dir build -C Release -j 1 -R '^(ThemeWidgetTest|SVSIntegrationTest|AutomatableModelTest)$' --output-on-failure 2>&1 |
    Tee-Object -FilePath 'build.log' -Encoding utf8
$buildExitCode = $LASTEXITCODE
Copy-Item -LiteralPath 'build.log' -Destination (Join-Path $uiEvidence 'tests.log')
if ($buildExitCode -ne 0) { Get-Content -LiteralPath 'build.log' -Tail 100; exit $buildExitCode }
```

QtTest 在 Windows 终端没有文本不表示测试没有执行；同时读取 CTest 注册的 `build/tests/*-results.txt`，将实际结果归档。截图测试需串行，避免窗口抢焦点。

新建构建目录时，先沿用当前项目必要配置选项，再从共享环境取 `LMMS_CMAKE_GENERATOR`、`LMMS_CMAKE_PLATFORM`、`LMMS_CMAKE_TOOLCHAIN_FILE`、`QTDIR` 和 `SVSSDK_ROOT`。只在相应变量非空且生成器支持时追加 `-A` / toolchain 参数；不要仅用一条简化 configure 命令丢失现有插件配置。configure 同样套用上面的日志与退出码模板。

F6 在受控开发前缀安装：使用 `cmake --install build --config Release --prefix <已确认的独立开发目录>`，同样通过 Tee-Object、记录退出码和日志。若现有 build 的完整 install 依赖未构建目标，先完成 F0 清单要求的目标构建，不能忽略 install 错误。交付前核对安装的 CSS/SVG/程序哈希和资源引用，并从安装目录进行 S01/S02/S05/S07 冒烟。

## 12. 完成定义与交付清单

### 12.1 必须交付

- [ ] 现代默认 `style.css` 和它引用的主题资源，安装可用。
- [ ] A/B/C/D 清单对应的实现完成；E 项边界明确。
- [ ] `doc/ui-modernization/inventory.md`：逐项状态、源码入口、引擎/插件覆盖与例外。
- [ ] `doc/ui-modernization/visual-spec.md`：冻结后的颜色、圆角、密度、字体和状态规格。
- [ ] `doc/ui-modernization/validation/`：阶段构建/测试日志、实窗截图与环境信息。
- [ ] `doc/ui-modernization/acceptance.md`：S01–S08 结果、数据行为验证、人工待验收项、follow-up、对应 commit。
- [ ] 不改变工程音频数据、参数 id、自动化绑定、SVS SDK 或项目格式；前后 fixture 重开正常。
- [ ] 开发安装可独立运行，资源无本机个人路径依赖；原安装版未覆盖。
- [ ] 每阶段独立提交，最终提交已推送；不混入其他任务修改。

### 12.2 判定规则

没有编译/必要自动回归通过记录不能标完成；标准控件变漂亮不能代替自绘或插件覆盖；仅有 QWidget render 图片不能代替原生实窗证据；仅有修改意图或 CSS 规则不能证明实际命中。

技术实施完成且只有观感/实机操作待人工确认时，可交付 `IMPLEMENTED / MANUAL-PENDING`，然后按用户反馈修正。不得让这些非阻塞人工项拖住后续阶段，也不得将其计为已验收。真正的 `ACCEPTED` 需要对应人工确认。

### 12.3 回滚

每阶段以单独 commit 为回滚单位。纯 QSS/资源问题恢复该阶段主题；自绘问题回退对应 View/属性及主题引用，避免新属性仍在 CSS 中悬挂。保留用户工程及个人配置，不使用 reset --hard 或递归清理工作区。若共享控件变化影响旧 artwork，修复该调用的视觉模式或回退该阶段，不通过修改音频模型补偿。

## 13. 当前进度

| 项目 | 状态 | 证据 |
|---|---|---|
| 关键源码调研与 A–E 分类 | DONE | 第 2、4–7 节路径与机制 |
| 可执行规格、F0–F6、验证与完成定义 | DONE（文档） | 第 3、8–12 节 |
| F0 全量面板清单及截图基线 | DONE / MANUAL-PENDING | inventory、visual-spec、fixture；52 个插件构建；双 DPI 45 个插件面板；必要回归 2/2 PASS；外部与人工例外见 acceptance |
| F1 标准控件与主题资产 | DONE / MANUAL-PENDING | F1 双 DPI 实窗及资源状态检查，必要回归 2/2 PASS；PNG 例外见 acceptance |
| F2 主题属性、代理样式及自绘外框 | DONE / MANUAL-PENDING | 原生 ThemeWidgetTest 6 PASS、核心回归 PASS、双 DPI S01–S08；滑块轨道可见性修复 |
| F3 旋钮、推子、数字与小型显示 | DONE / MANUAL-PENDING | 52 插件编译；核心回归 PASS；双 DPI 控件各 8 PASS 与场景实窗 |
| F4–F6 产品改造 | TODO | 按阶段提交推送后依次实施 |

执行者从 F0 开始；每阶段只更新与实际完成证据相符的状态。本文没有将此前 SVS 任务的构建通过记录当作本轮现代化验收。

