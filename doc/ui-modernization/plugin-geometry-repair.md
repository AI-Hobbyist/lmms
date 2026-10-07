# 固定原生乐器页的裁切/重复补修

用户反馈 TripleOscillator、Vibed 等面板右侧和底部显示被截断的第二块内容。原键盘滚动条本来存在，本次保留；不以滚动条是否出现判定故障，没有修改 PNG。

原生 Windows 复现命令为 ThemeWidgetTest 的 `fixedInstrumentArtworkGeometry`。失败证据 plugin-geometry-red-results.txt：TripleOscillator 背景为 250×250，实际插件 QWidget 被宿主拉成 266×286。固定坐标控件使用的 QPalette 背景纹理按控件矩形平铺，所以多出的区域重复绘制了背景，表现为截断的第二块面板。原面板覆盖测试人为设置了 MDI 框尺寸，不能证明默认窗口尺寸正确；这次改为用实际默认尺寸打开。

修复仅在现代主题下，InstrumentTrackWindow::adjustTabSize 对不可调整大小的插件页使用自身 sizeHint，公共页继续沿用已有字体测量宽度及布局。TripleOscillator/Vibed 都恢复 250×250；公共页长标签仍完整，切换公共页再返回也保持原几何。动态可调整插件及 legacy 分支保持既有路径，不改变模型、参数、预设或音频实现。

- plugin-geometry-final-build.log：主程序、两个 UI 测试目标、52 个启用插件均在原目录编译成功；清单见 plugin-geometry-artifacts.json。程序为 build/Release/lmms.exe。
- plugin-geometry-core-tests.log：核心 3/3 PASS（53.55 秒）；ThemeWidgetTest 13 PASS，包含默认窗口的尺寸、公共页长标签和返回插件页断言。
- plugin-geometry-green-results.txt / plugin-geometry-150-results.txt：两个用户截图插件的原生 100%/150% 检查通过；四张 plugin-geometry-*-100/150.png 为真实窗口证据。
- GeometryFix-100 / GeometryFix-150：以默认窗口尺寸重新打开原生乐器；两组各 3 PASS，47 个有效面板与预设比较通过。既有五项外部环境仍 MANUAL/PENDING，不计完整外部编辑器验收。

失败轮保留且不计 PASS；没有新增离屏路径，没有修改键盘滚动条、PNG 或经典/现代切换功能。修复 b6b867585 已提交并推送 master；已有 packages 目录中的全量包已更新为 lmms-modern-ui-full-b6b867585-win64.zip，替代此前 7b44c5487 版本。ZIP 全文件哈希、原目录实际覆盖及配置保留检查通过；覆盖后的真实程序窗口 3 PASS。见 replacement-package.md。
