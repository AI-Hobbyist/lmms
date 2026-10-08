# SVS 工程转换冻结约定（M0）

基线日期：2026-10-09。来源 `refs/LibreSVIP`，提交
`e33dc2824453a3104ee2b0a9cd836dce8452ce31`。完整源码、`uv.lock` 和
`pyproject.toml` 的 SHA-256 见 `formats.json`；修改基线须重新审查矩阵。

## 能力矩阵与验证分母

`formats.json` 是逐插件源代码清单：稳定 ID、类/模块、格式版本、后缀、
读写实现行号及 mixin、直接依赖、输入/输出默认值表达式（含继承的选项）、
音频/音高/单轨选择/速度/拍号的源码证据、音高选项、各方向的待补样例。
默认值表达式保持原源码，枚举不猜测数值；M1 加载固定选项类后提供 JSON
默认值和 schema，必须和这里的源码一致。

36 个导入方向、39 个导出方向，合计 75 项；40 个插件、45 个后缀。
`ass/lrc/srt/svg` 只导出，`vshp` 只导入，其余双向。
读写能力由具体实现和 ReadOnly/WriteOnly mixin 判定，不用 hasattr。
`deprecated/experimental` 不在固定快照默认集合中。

源代码证据不等于运行验收。所有格式样例明确标记 PENDING；M1 检查全部
插件加载，M4 逐项真实读取/序列化并覆盖每个后缀。没有样例或运行失败
不得报告该方向 PASS，不因缺依赖删减矩阵。

多轨/音频/音高证据是保守预检依据，具体转换器的 parser/generator 必须
在 M1/M4 复核。统一 JSON 格式直接序列化 Project，完整保留三项；其余
没有 InstrumentalTrack 处理的目标在导出前报告省略音频。字幕/歌词/SVG
属于不可往返输出。含 `track_index` 的输出选项检查单轨选择或多文件输出，
不能默认选择第一轨并静默丢弃。源曲线读取缺失必须具名提示损失。

## 运行时与原位部署

已检查开发目录：`build/Release` 有 Qt、插件和 `svs` 声库部署，没有
Python 运行时。转换组件使用 `build/Release/svs-project/`，属于现有应用树，
不是替代构建/安装根。目录布局冻结为：

- `python/python.exe`：固定 CPython 3.13.12 x64 独立运行时；无系统 Python fallback。
- `python/Lib/site-packages/`：固定转换核心与运行依赖。
- `bridge.py`、`formats.json`、`runtime-lock.json`：入口、冻结清单及实测版本/hash。
- `licenses/`：LibreSVIP MIT、CPython 及所有随附依赖许可。

M1 从基线核心依赖加 `crypto`、`lxml` 闭包解析并记录确切 wheel 版本和
SHA-256，不安装 desktop/mobile/webui/cli。`uv.lock` 是上游完整依赖基线；
实际 Windows/Python 条件闭包另存 runtime-lock，不用用户全局包满足依赖。
M1 必须证实所有插件均能加载；若冻结依赖无法获取，明确阻塞并解决，
不能用更少的转换器冒充交付。开发部署和打包保持同一组件目录。

不导入上游默认 manager（它读取用户配置/外部目录），按固定清单模块
逐一加载类并核验 ID/版本/mixin；只接受清单内的 formatId。

## 默认选项与损失

默认取冻结版本选项类的有效默认值。输入所有 `import_pitch` 强制为 true；
SVP 的 `pitch` 使用 `full`，完整读取滑音和可解析颤音（上游默认 plain
会忽略未编辑颤音）。`use_edited_pitch` 保持 true。其他必要选项通过实际
schema 交互，不另建插件设置中心。导出保真测试把适用格式的 down_sample
设为 0；生产若选择稀疏采样须报告精度损失。

源格式/解析器没有对应曲线、读音、音素、参数、音频、轨道或时间变化
支持时，产生含 formatId、track、field、reason 的 loss 项；用户确认前不
提交。音高损失不能归入普通警告冒记完整保真。

## 时间、曲线和音频映射

- LibreSVIP 每四分音符 480 tick，LMMS 固定四分音符 48 tick，比例 10。
  音符/曲线保留小数；片段位置等整数边界一次 round（半数远离零），
  原始全局坐标转换后再裁剪，不逐段累积舍入。
- 统一 pitch 的 x 含第一小节偏移 `480*4*numerator/denominator`；减去该值
  后除 10。y 为绝对 MIDI 音高乘 100（cent）；导入为 `svs.pitch`、
  `unit=semitone`、`mode=absolute`，值除 100。`y=-100` 是中断，不是音高。
  `Point.start_point/end_point` 为哨兵；每段末点 breakAfter，保留 gaps，
  不跨中断插值。重复 x 的段界端点规范化后仍须保持左右覆盖语义。
- `projectTick=position+localTick-contentOffset`，contentOffset 为
  `-startTimeOffset`；导出仅片段有效范围，跨界音符/曲线裁剪，保持重叠。
  首小节偏移仅在统一模型边界加减一次。换声库不修改该绝对曲线。
- Song tempoModel 是整数 BPM，现有 TempoSnapshot/TempoSource 可读完整
  自动化速度；通过既有全局 AutomationClip 写 step 速度变化，不改全局
  时间系统。非整数 BPM、越界速度须具名损失确认，不能悄悄截断。
  连续速度自动化按每个整数 LMMS tick 采样，报告此采样约定；用同一份
  TempoSnapshot 换算音频偏移和片段音高。
- MeterModel 的 numerator/denominator 为 1..32 的两个可自动化 IntModel，
  原生保存分别写出 `timesig_numerator/denominator`。固定拍号保留；变拍号
  先验证既有时间系统是否正确表达。如果不能精确保留，以明确拒绝结束
  导入/导出，不只取第一项，不开展全局时间系统重构。
- 音量统一为线性 gain：LMMS 百分数除 100；声像百分数除 100 为 [-1,1]；
  保留轨道 mute/solo。片段 mute 或音量、效果等无对应项须报告损失。
- 音频完整文件可直接引用，相对路径以来源文件目录解析。裁剪/反向
  只生成该 SampleClip 的 PCM WAV，禁止混入乐器/SVS 音频。多个音频片段
  各成一个 InstrumentalTrack，并报告拆分。依赖文件缺失为具名失败/损失，
  不伪造静音文件。
- 解出的音频在导入成功前保存在受控任务目录，成功后移入当前 LMMS 用户
  workspace 的持久资源目录；工程引用持久路径。失败/取消清理本次目录。

## 协议 v1

单任务进程、UTF-8 JSON stdin/stdout，stderr 专用于日志；stdout 只含一份
响应。QProcess 用独立参数启动，禁止 shell 拼接。请求和结果最多 64 MiB；
输入文件最多 256 MiB；压缩包累计解压最多 1 GiB、最多 10000 项；禁止
绝对路径、`..`、盘符、链接及越界音频写出；超时 120 秒。取消 terminate，
必要时 kill，等待退出并回收任务目录，不保留子进程。

操作：`listFormats`、`importProject`、`exportProject`。请求 `protocol=1`、
`requestId`、`operation`，转换请求加 `formatId`、绝对 `path`、`options`。
导出加 `project`（LibreSVIP Project JSON，by_alias=false）。响应必须回显
protocol/requestId/formatId，`status` 为 success/cancelled/error，`dataVersion=1`；
`warnings` 和 `losses` 为结构化数组；错误含 code/message/detail。
协议例子见 `protocol-examples.json`。

导入返回完整 Project 和持久化前资源清单；主线程先预检再处理未保存提醒，
使用既有 DataFile saveProjectState/restoreProjectState 保存事务备份。
成功提交后清空外部路径、标记 modified；失败恢复数据、文件名及 dirty。
导出只读快照直接取 Song 子轨道的 SVS/Sample 类型，声库/缓存不可用仍可
导出笔记和用户曲线。临时输出文件组验证后提交，替换前备份已有文件，
任何失败回滚本组且清理；转换不改 Song 或撤销栈。

## 冻结误差与验收

映射内部时间误差 ≤ 1e-9 LMMS tick、绝对音高误差 ≤ 1e-6 semitone；
统一整数 tick 序列化 ≤ 0.5 LibreSVIP tick（0.05 LMMS tick），cent 整数
序列化 ≤ 0.5 cent。音频裁剪边界 ≤ 1 sample。原生保存重开保持曲线、
断点、歌词与时间。目标格式采用其源码声明的时间/音高采样单位；需在
M4 格式测试记录中写出该单位，不能事后放宽上述映射阈值。

M2 固定采样验证滑音、颤音、断点、变速与非零片段位置，并检查实际合成
input JSON。真实 Windows 窗口验证菜单/声库选择、中文、原曲线显示、
编辑与撤销，截图代表状态。严格禁止 offscreen，不使用 Computer Use。

M0 校验仅验证源合同、75 项样例占位、45 个后缀和 mixin 判定，不是构建
或真实转换/GUI 通过。M1/M4 后续填充运行证据。
