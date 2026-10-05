# SVS 音符波形、音素条与视口缩放（自动验证 PASS）

2026-10-05，仅按用户追加要求修改独立 SVS 编辑器。此记录替代上一轮 80px 波形/音素组合条的布局约定。

波形由独立条移到对应音符下方，横向范围限制在该音符的 tick/duration 内，纵向位置跟随音高和琴键高度；滚动、横向缩放与纵向缩放共用音符坐标。使用现有合成 PCM、不可变时间映射及分层峰值缓存，细尺度补充直接峰值采样；没有生成虚构波形、改变合成调度或新增音频依赖。幅度按实际 PCM 绘制，低音量波形较细。

音素条固定 36 个 Qt 逻辑像素，单元格有效高度 32px，较原来约 20px 加高。波形区域不再占据音素条；命中范围、拖动指针和现有音素验证同步调整。音素边界、属性、最短秒数及跨 tempo 约束沿用现有实现。

鼠标滚轮位于琴键区时调节琴键高度，保持指针对应音高；时间标尺上的滚轮横向缩放并保持指针对应时间。工具栏增加可见小节数选项（1/2/4/8/16），按当前音符可用宽度适配；音符、音素及参数图同步横向视口。窗口改变宽度或自由缩放后，选项自动显示实际匹配的小节数或 Custom，不维持过时的标签。保存的仍为既有横向/纵向缩放及滚动状态。原有画布滚动、Ctrl 缩放、Shift 横向滚动保持可用。

Release 主程序/集成测试构建通过；Windows 原生专项 7 passed/0 failed，最终完整 SVS 回归 52 passed/0 failed/0 skipped。新增验证覆盖合成波形位于音符下方且不越过音符横向边界、缩放/滚动后对齐、琴键/标尺滚轮锚点、1/4/16 小节适配、参数图同步、窗口改宽后标签更新、视口保存恢复。原有音素拖动、变速约束、参数和歌词编辑、主题、MDI 生命周期、合成/播放/导出回归均通过。

真实 GuiApplication 窗口由 QScreen::grabWindow 采集，见 validation/SVS-note-wave-zoom-native-window.png；已检查波形位置、加高的单行音素条、参数标签及原生字体。未使用 offscreen 或 Computer Use。用户最终人工布局/操作验收 MANUAL/PENDING。

证据：validation/SVS-note-wave-zoom-build.log、SVS-note-wave-zoom-native-QtTest.txt、SVS-note-wave-zoom-native-test.log、SVS-note-wave-zoom-native-regression-QtTest.txt、SVS-note-wave-zoom-native-regression-test.log、SVS-note-wave-zoom-native-window.png、SVS-note-wave-zoom-deploy.log。开发目录 build/svs-lmms-clean-install 已更新并核对三项二进制哈希，保留原开发主题及独立配置；测试结束 LMMS/测试进程为 0，原安装版未打开。M5 ZIP 保留为历史产物。

现有 JO-ID、测试环境 piano 图标及 RenderManager 正则警告仍为相邻 follow-up，本次不修改。
