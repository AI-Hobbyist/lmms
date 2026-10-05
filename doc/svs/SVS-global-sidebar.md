# SVS 关闭回嵌与全局基础值侧栏（自动验证 PASS）

2026-10-05，按用户最新要求修改独立 SVS 钢琴窗。

分离后的原生关闭按钮直接将同一个 SVS 编辑器回嵌并保持可见，调用现有 SubWindow::attach 恢复窗口标志和工作区内几何。SVS 自身事件过滤器优先处理关闭，回嵌期间暂时禁用再次分离，随后恢复可分离状态；不修改全局分离配置、SubWindow 框架或普通钢琴窗。实际 GuiApplication 验证 show/hide/detached 三种全局设置下关闭均回嵌，保留窗口实例与选中音符，后续仍能分离；嵌入状态关闭和片段删除沿用原生命周期。

侧栏参考 TuneLab 的名称、横向滑块和数值排列，使用 LMMS Qt Widgets/QSS。未绑定声库时只有 Singer 选择。选择已注册的插件/歌手后，从该引擎为该声库返回的能力声明生成 track/clip 数值基础项，不内置音量/气声/张力/性别等参数清单，不创建无声明的参数。类型、范围、步长、默认值、线性/对数尺度、顺序、显隐条件及可编辑状态取自声明；换声库隐藏不支持的行，稳定 ID 控件和保存的输入保留，恢复声库后重现。

侧栏数值显示“基础值 − 声库声明默认值”，因此默认显示 0；输入偏移后通过既有参数 setter 保存实际基础值。track 作用域经原有轨道参数进入各片段，clip 作用域经片段参数进入当前片段；数据格式/SDK ABI 无变更。数值调节不批量移动音符、覆写曲线或制造宿主固定的合成参数。引擎负责解释声明的参数和曲线：SVSExample 的张力/气声/性别现在以 curve(t) + base − default 组合，并在声明范围内限幅，无曲线处使用基础值。默认基础值时保持原结果；已有曲线也能受整体基础值调节。

原有立绘、透明度、语言/字典、曲线移动选项和各作用域详细属性仍可从工具栏 Settings 打开，不占据默认侧栏。Properties 仍控制侧栏显隐；未增加截图中的预设或模型等未要求功能。

Release 主程序、示例插件和测试构建通过。Windows 原生专项 4 passed/0 failed；最终完整 SVS 回归 53 passed/0 failed/0 skipped。新测试验证默认只有歌手选择、真实歌手绑定入口、默认相对值 0、滑块实际写入基础值、曲线数据不变、合成 PCM 改变并与整体平移曲线的结果逐样本误差小于 1e-6、声库切换/控件复用/保存恢复以及 Settings 可达。原有合成/导出/主题/图像/参数/编辑回归全部通过。

真实窗口截图 validation/SVS-global-sidebar-native-window.png 已检查侧栏结构、数值和原生字体。未使用 offscreen 或 Computer Use。配置好主题的开发目录 build/svs-lmms-clean-install 已更新 lmms.exe、测试程序和 SVSExample.dll 并核对哈希，测试结束进程为 0；原安装版未打开。用户最终人工布局/操作验收 MANUAL/PENDING，历史 M5 ZIP 未重新打包。

证据：validation/SVS-global-sidebar-build.log、SVS-global-sidebar-native-QtTest.txt、SVS-global-sidebar-native-test.log、SVS-global-sidebar-native-regression-QtTest.txt、SVS-global-sidebar-native-regression-test.log、SVS-global-sidebar-native-window.png、SVS-global-sidebar-deploy.log。现有 JO-ID/测试图标/RenderManager 正则警告保留为 follow-up。
