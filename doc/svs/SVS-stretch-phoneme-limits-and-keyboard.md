# SVS 拉伸后的音素约束与原生琴键风格

2026-10-07。范围仅为用户报告的音符伸缩后 Failed (2)／音素块丢失及 SVS 琴键风格，不修改普通 PianoRoll 或音频系统。

## 复现与原因

新增实窗回归 stretchShortPhonemeRenders：120 BPM，48 tick 音符，手动前辅音长 0.6 tick（6.25ms）。拖动尾端缩短到 24 tick，原来的比例缩放把辅音压到 0.3 tick（3.125ms），低于 SVSExample 声明的 5ms。插件提交返回 SVS_INVALID_INPUT，状态为 `Failed: SVS synthesis failed (2)`。失败后没有有效合成音素反馈，用户看到音素块消失。修复前测试稳定失败，见 validation/SVS-short-phoneme-red.txt。

之前的伸缩测试主要验证几何与撤销，没有在每种拉伸后断言合成成功。本轮对原有八种首尾拉伸组合补齐再次合成及四个音素反馈断言。

## 修复

共享伸缩操作读取声库声明的最短时长／最大提前量，并通过既有 tempo snapshot 在实际项目时间中检查边界。比例缩放后，过短音素保持最低允许时长，后续起点顺移，避免重叠。尾部填充完成后再次验证提前量、顺序、最短时长和音符尾端。整个手势若无法容纳音素，则限制到可合成的边界，同时检查受修剪的相邻音符。音符画布和音素条上半区均使用此约束，保持原有预览、一次提交及撤销语义。

SVSExample 提交时给 -0.2 秒提前量比较加入与已有时长检查一致的 1e-8 秒舍入容差，不改变声明能力或 SDK ABI。没有通过忽略错误、接受任意无效音素或保留失效音频来掩盖失败。

新增三音素极限缩短验证：音素条要求缩到 1 tick，但三段各需 0.48 tick，实际边界停在约 1.44 tick，三音素仍能成功渲染。原短辅音用例、正常八种首尾方向、自动生成音素独立调整均通过。

## 琴键

对齐原版 PianoRoll 的小白键 1.5 行、大白键 2 行、黑键 1 行和黑／白键宽度比例，先画白键再叠黑键，使用黑色轮廓和随琴键高度缩放的字号。保持 SVS 原有键盘区域宽度、坐标与交互，不修改普通编辑器。

SVSPianoRoll 增加与原版同名的白键背景、黑键背景、白键文字 QSS 属性；默认值使用原版默认主题的 #E5EAF0／#20262D／#000。没有单独皮肤系统；无声明时沿用 palette 并保证文字明暗对比。

## 验证与部署

- 前台 PowerShell + Tee-Object + build.log + LASTEXITCODE 构建通过；没有切换或新建编译目录。
- 主程序 `build/Release/lmms.exe`；SVSExample `build/Release/svs/SVSExample/SVSExample.dll`；启用原生插件及支持库仍在 `build/Release/plugins` 原位重新链接。
- 当前主题 CSS 同步到既有 `build/Release/data/themes/default/style.css`，与源文件 SHA256 一致。最后实窗专项使用此部署数据目录与上述实际插件目录。
- 完整 SVS 回归：63 passed / 0 failed / 0 skipped；CTest 1/1 通过。
- 实际部署主题 GuiApplication 原生专项：5 passed / 0 failed / 0 skipped。真实截图已检查，音素块、Level／Peak 和原版琴键风格正常，C4／C5 清晰；测试窗口关闭。
- 全回归曾出现截图显隐比较被遮挡的间歇失败，独立与再次全回归通过；只给截图测试窗口加入临时置顶，不改变生产窗口行为，最终完整 CTest 通过。

证据：validation/SVS-short-phoneme-red.txt、SVS-short-phoneme-green.txt、SVS-phoneme-keyboard-final-build.log、SVS-phoneme-keyboard-native-deployed.txt、SVS-phoneme-keyboard-regression.txt、SVS-phoneme-keyboard-ctest.log、SVS-native-keyboard-style-on.png。

用户最终操作手感验收 MANUAL/PENDING。未使用 offscreen、Computer Use 或原安装版 LMMS；既有 journal ID／非 SVS 测试宿主插件加载诊断仅记录 follow-up。
