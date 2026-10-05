# SVS 钢琴窗默认嵌入与可选分离（PASS）

2026-10-05，用户人工验收指出 SVS 钢琴窗应默认嵌入，按普通钢琴窗方式可选分离。本次只修正 SVS 窗口承载与生命周期，不扩展钢琴窗布局、编辑功能或普通 PianoRoll/SubWindow/MainWindow。

SVSPianoRoll 内容由 QDialog 改为 QWidget；SVSClipView 双击通过 MainWindow::addWindowedWidget 接入现有 SubWindow/MDI 工作区。沿用 LMMS 子窗口标题栏、最大化、分离按钮、全局分离设置及分离窗口关闭后的回嵌行为。默认 detachbehavior=show 时在主窗口中打开；用户选择全局默认分离时与普通窗口一致。

每个片段视图使用 QPointer 复用编辑器；重复双击、关闭后重开及分离时重开保留同一个窗口和编辑状态，不额外创建窗口。片段销毁时删除受托管的编辑器，现有 MainWindow 清理连接随之释放子窗口；未托管的自动测试内容窗口仍可独立创建。窗口图标沿用普通钢琴窗 piano 主题图标。

Release 主程序及测试构建通过。embeddedWindowLifecycle 在独立 offscreen 子进程启动真实 GuiApplication，实际双击 Song Editor 的 SVSClipView，断言默认嵌入/MDI 归属、可选分离、重复双击不增窗口、关闭分离窗回嵌、画布/选择保留、关闭嵌入窗后重开、分离状态删除片段后窗口清理。专项3 passed/0 failed/0 skipped；完整SVS回归50 passed/0 failed/0 skipped。普通回归中该专项实际启动并校验子进程结果，不是无操作返回。

测试首次编译误用私有主窗口构造/析构及不存在的简写接口，修正为真实 GuiApplication/片段数据/鼠标操作。首次测试触发 LMMS 首次配置对话框而超时，测试临时配置补 app.configured=1 和有效 Dummy 音频设备后通过；未更改应用启动逻辑或提高超时。失败日志本机保留，最终证据为 validation/SVS-embedded-window-test-build.log、SVS-embedded-window-QtTest.txt、SVS-embedded-window-test.log、SVS-embedded-window-regression-QtTest.txt、SVS-embedded-window-regression-test.log。

已将新主程序/测试程序更新到 build/svs-lmms-clean-install 并核对文件哈希，保留默认主题、独立配置和 Launch-SVS.ps1，见 validation/SVS-embedded-window-deploy.log。M5 先前 ZIP 为历史产物，本次窗口验收使用更新后的开发目录；不把旧 ZIP 声称为包含本次修改。

本次未使用 Computer Use，未打开桌面窗口或原安装版。人工观感和后续钢琴窗差异继续由用户验收。
