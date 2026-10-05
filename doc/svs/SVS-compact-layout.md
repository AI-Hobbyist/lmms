# SVS 音素区、参数标签和音名选项修正（自动验证 PASS）

2026-10-05，按用户提供的四张截图仅修正三项：音素编辑区过高、参数标签居中与去除眼睛图标、原有 Enable all note labels in piano roll 设置对 SVS 生效。

波形/音素条固定为 80 个 Qt 逻辑像素，其中波形保持现有 52px 绘图范围，音素单行约 22px；额外高度分配给音符画布。窗口大小和编辑区分隔条变化不再放大音素单元格。保持原有音素命中坐标、边界拖动、属性编辑、时间同步和撤销逻辑；不添加 TuneLab 截图的其他波形控件。

参数按钮由眼睛图标+文字改为纯文字彩色标签，使用插件声明 color 着色文本和淡色底，选中标签有同色边框；隐藏曲线标签变暗。按钮基底仍通过 Qt style/QSS 绘制。两侧等量伸缩使整组按钮居中，参数较多时仍可横向滚动。左键编辑、右键显隐及按 ID 保存恢复保持原逻辑。

SVSCanvas 读取 LMMS 既有 ui/printnotelabels，监听该设置变化以实时重绘，不新增设置或修改普通 PianoRoll/SetupDialog。关闭时琴键只标 C；开启时与普通钢琴窗一致为全部白键标音名，音符足够宽时在歌词前显示音名。短音符优先保留歌词，标签开关不改变音符/歌词/曲线数据。

Release 主程序和测试构建通过。Windows 原生 Qt 专项 7 passed/0 failed，完整 SVS 回归 51 passed/0 failed/0 skipped。新增测试覆盖两个窗口尺寸下音素条保持 80px、可见参数标签整体中心误差不超过 2px、无图标/纯文字、音名开关前后的琴键/音符绘制变化及关闭恢复、原始音符数据不变；原有参数绘制/反馈只读、音素边界/变速约束、主题与窗口生命周期全部通过。没有使用 offscreen 或 Computer Use 操作应用。

真实 GuiApplication 窗口通过 QScreen::grabWindow 直接采集为 validation/SVS-compact-layout-native-window.png，人工检查截图确认音素单行、参数标签居中与插件颜色，字体正常。截图路径可通过 SVS_PARAMETER_WINDOW_CAPTURE_PATH 指定，避免覆盖上一轮截图。用户最终布局/操作验收仍为 MANUAL/PENDING。

证据：validation/SVS-compact-layout-build.log、SVS-compact-layout-native-QtTest.txt、SVS-compact-layout-native-test.log、SVS-compact-layout-native-regression-QtTest.txt、SVS-compact-layout-native-regression-test.log、SVS-compact-layout-native-window.png。开发目录 build/svs-lmms-clean-install 已更新主程序/测试程序并核对哈希，保留已配置的主题和独立配置；validation/SVS-compact-layout-deploy.log。测试结束没有残留 LMMS/测试进程，原安装版未打开。M5 先前 ZIP 仍为历史产物。
