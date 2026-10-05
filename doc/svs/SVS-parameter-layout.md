# SVS 共享参数栏（自动验证 PASS，人工布局验收 PENDING）

2026-10-05，按用户两张截图仅调整参数栏。原先每参数独立纵向画布/高度控件改为共享曲线图，下方单行参数按钮；参数多时横向滚动，整体高度沿用编辑区分隔条和 areaSizes 持久化。右侧属性、音符/波形/音素区保持原有结构，没有加入参考图的其他功能。

参数集合与名称仍来自声库声明。新增可选 color 元数据（#RRGGBB）；SVSExample 声明自己的颜色，宿主不根据参数 ID 分配颜色。无 color 的旧插件使用 LMMS 主题高亮回退；非法颜色拒绝声明。背景/网格/按钮质感继续使用 Qt Widgets 和 LMMS Theme CSS/QSS。数值范围、步长、线性/对数尺度、离散 choice/bool 类型均来自插件现有声明。叠加曲线按各自范围归一化，左轴标注当前编辑参数范围；只读反馈可查看、不可写。

本地 TuneLab 参考源码 ParameterButton/ParameterTabBar/PianoWindow 用于核对按钮语义：左键选中并显示曲线，再次左键保持编辑；右键切换非当前曲线的显隐，当前编辑曲线保持可见。显示状态沿用 lanes 中稳定的 input:/feedback: ID；selectedParameter 保存当前选择。声库暂缺参数时保留原始曲线和状态，恢复声明后恢复按钮与选择。切换参数取消进行中的手势并清除旧锚点选择；时间滚动和缩放与音符画布同步。

Release 主程序、SVSExample、集成测试构建通过。Windows 原生 Qt（QT_QPA_PLATFORM=windows）专项 7 passed，完整回归 50 passed/0 failed/0 skipped。测试断言共享两张画布（音符/参数）、底部按钮位置、取消独立高度控件、float/bool/enum 编辑与只读反馈、插件 RGB 实際渲染像素、不同数值范围、右键显隐和左键编辑、声库切换与 XML 状态恢复、主题回退、嵌入/分离生命周期。早期测试发现恢复声明尚未完成时直接解引用控件，以及旧测试使用无名称 findChild 误选新共享画布；已修正异步等待、存在断言和按名称选择画布，失败日志本机保留。

用户要求后续禁用 offscreen，已经写入 AGENTS.md。最终截图来自真实 GuiApplication 的可见 Windows 窗口，测试通过 QScreen::grabWindow 直接采集，没有用 QWidget::grab 模拟截图，也没有 Computer Use 操作应用。首次采集窗口部分超出可见屏幕，调整测试窗口到可用屏幕范围并等待原生绘制后重新采集。原安装版未打开，测试窗口随测试结束清理。Computer Use 仅保留给之后对比 TuneLab 钢琴窗操作逻辑；当前布局开发不用。

证据：validation/SVS-parameter-layout-build.log、SVS-parameter-layout-test-build.log、SVS-parameter-layout-native-QtTest.txt、SVS-parameter-layout-native-test.log、SVS-parameter-layout-native-regression-QtTest.txt、SVS-parameter-layout-native-regression-test.log、SVS-parameter-layout-native-window.png。截图中参数栏字体正常，颜色来自示例插件，两条不同范围曲线共享绘图区。用户最终布局/操作验收仍为 MANUAL/PENDING。

开发目录 build/svs-lmms-clean-install 的主程序、测试程序、SVSExample DLL 已更新并核对哈希，保留默认主题、独立配置及 Launch-SVS.ps1。证据 validation/SVS-parameter-layout-deploy.log。先前 M5 ZIP 保留为历史产物，未将其声称为包含本次布局修改。
