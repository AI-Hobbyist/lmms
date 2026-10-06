# SVS 曲线平滑修正

2026-10-06。用户反馈连续曲线有明显阶梯感，要求参考 TuneLab 的平滑插值算法。限定于 SVS 曲线手势、区间保留、回显绘制和必要测试，不修改普通 PianoRoll、Automation 或音频框架。

## 原因与修正

- 自由笔此前保留全部鼠标采样，并直接沿用原曲线插值。连续浮点笔触现在参考 TuneLab `MathUtility.Simplify(5, 2)` 的规则保留极值、去掉平坦内部点、简化相近的同向斜率；时间坐标仍为实数 tick，不量化。
- 自由笔使用单调三次 Hermite：端点切线为零，内部同向割线取调和平均，异号或零斜率时切线为零。使用现有 SDK 的受约束求值器防止过冲；新笔触切线在拼接前固定，避免范围外锚点影响笔触。
- 混合线性/Hermite 曲线在局部替换、复制切片时保留边界原段类型。回归曾发现右键重置把范围外部分曲线改成线性，已通过保留段类型修正。
- 编辑时间 0 的单点时，原边界 `nextafter(0)` 是次正规数，会被 Qt JSON 写成 0，使零宽断段导致合成拒绝输入。时间原点使用可稳定序列化的 machine epsilon 边界，其余时间仍保持 nextafter；新增 JSON 文本往返测试，真实窗口绘制用例也检查合成成功。
- 曲线绘制开启抗锯齿，仅作用于曲线层。细线像素测试隔离单条叠加线，并验证统一覆盖率下的声明 RGB 混色，避免将共用基线上的多条曲线混色误判为错误配色。
- 合成音高回显以前把每个采样画成水平段再垂直连接，截图中的细阶梯来自这条路径。现在对同一音符、连续覆盖的采样作受约束 Hermite 插值绘制；不同音符和时间缺口断开，最后一个采样保持其末尾覆盖。插件回传数据及 PCM 不被改写。

参考源码只用于核对数学和编辑行为：`tl_ref/TuneLab.Hosting.Foundation/Science/MathUtility.cs`、`HermiteInterpolation.cs`、`tl_ref/TuneLab/UI/MainWindow/Editor/PianoWindow/PianoScrollView/PianoScrollViewOperation.cs`。实现为独立 C++，没有新增 TuneLab/.NET、引用目录或外部运行依赖；公开 SDK ABI 不变。

截图中粉色 Power 来自示例插件声明的整数阶梯曲线。Power、Soft、Mode 以及显式 step 曲线继续按声明离散变化；不将离散参数伪装为连续值。修正应用于新画的连续浮点笔触，旧工程曲线不作静默批量转换。原有直线、平滑工具、手工切线、右键重置、只读门禁及基础值偏移继续保留。

## 验证

`freehandMonotonicInterpolation` 在修正前已复现失败，检查端点斜率、密集笔触简化、独立计算的 Hermite 中间值、一阶连续、无过冲、范围外保留、JSON 文本往返（含时间 0 单点）和离散参数。`feedbackPitchInterpolation` 检查相邻采样间确实有连续值、无过冲、不同音符和时间缺口不连接。现有合成测试核对实际插件回显与输入曲线，完整回归覆盖撤销、右键重置、持久化、SDK、播放及导出。

Release 主程序、示例插件和测试程序均通过前台 PowerShell 编译。最终 Windows 原生回归结果见计划书第 25 节及 `doc/svs/validation/SVS-smooth-regression.txt`；`SVS-smooth-repro.txt` 保留初始失败证据，构建、测试、部署日志以 `SVS-smooth-` 命名。

原生窗口截图使用配置好主题的开发 GUI 测试入口直接抓取 Windows 窗口，不使用 offscreen、Computer Use 或原安装版 LMMS。窗口由测试生命周期清理；最终操作手感及听感继续 **MANUAL/PENDING**。

范围外 follow-up：在开发安装目录直接运行集成测试时，其 GUI 子进程加载第三方 LADSPA 模块曾崩溃，缺插件用例也因安装目录自带示例插件无法隔离。该尝试的日志保留在 `SVS-smooth-regression-before-final.txt`；采用既有 build/tests 原生测试入口完成回归，不修改相邻插件系统。已有 journal ID 和测试图标诊断继续只记录。
