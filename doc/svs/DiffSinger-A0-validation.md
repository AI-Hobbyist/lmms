# A0 原生骨架与冻结验收

日期：2026-10-08。实现基线：`05cd61c5f`。本记录只覆盖 A0；没有声库合成能力或 DirectML 实现声明。

新增 `plugins/SVSDiffSinger`，稳定 ID `org.lmms.svs.diffsinger`、名称 DiffSinger、类型 ai。只依赖独立 SVS SDK 和 native CPU ORT；空 catalog，不注册假声库，未知 voice/session 拒绝。冻结 ORT 1.23.0、JSON 3.12.0、yaml-cpp 0.8.0 的 revision/archive SHA256；原生中文拼音数据及许可选择见 [依赖调查](DiffSinger-native-dependencies.md)。用户要求已加入计划：字典解析参考 tlds_ref，可选语言依据声库能力。

| 验收项 | 结果 | 证据 |
| --- | --- | --- |
| 现有独立 SDK 构建树构建 native 插件/工具/旧示例 | PASS | `validation/A0-sdk-configure.log`、`A0-sdk-build.log` |
| SDK-only ABI 1.0/1.1/1.2 协商、错误 major/短表拒绝、零声库与 AI 声明 | PASS | `validation/A0-sdk-abi.log` |
| 实际开发安装的旧 SVSExample full/minimal 可合成、有音频、取消、所有权、时间原点 | PASS | `validation/A0-existing-example.log` |
| 独立 C 最小例子 ABI 1.0 可用 | PASS | `validation/A0-minimal-example.log` |
| 真正缺 native ORT configure 明确失败 | PASS | `validation/A0-missing-onnx.log`；负向预期失败 |
| 官方 CPU SDK 下载哈希校验 | PASS | `validation/A0-onnx-sdk.log`、`plugins/SVSDiffSinger/dependencies.lock.json` |
| 原开发树直接构建/部署及 ABI 测试 | PASS | `validation/A0-development-configure.log`、`A0-development-build.log`、`A0-deployed-abi.log` |
| 六个嵌套 acoustic 包，48 个模型 CPU session/实际 tensor signature | PASS | `validation/A0-model-probe.log`、[完整矩阵](DiffSinger-six-package-matrix.json)；0 错误 |

开发安装仍为 `D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe`。新增引擎 DLL 直接写入其 `svs/SVSDiffSinger/SVSDiffSinger.dll`，ORT 同包；没有建立替代安装树。已有普通插件目录 `build/Release/plugins` 未搬迁，本阶段未重编或禁用普通插件。部署文件/大小/哈希：`validation/A0-deployed-manifest.json`。

六包：芙宁娜、那维莱特、纳西妲、温迪、希格雯、遐蝶；均为 acoustic + duration linguistic/dur + pitch linguistic/pitch + variance linguistic/variance + bundled vocoder，每包 8 个实际模型。所有根配置为 44100Hz、hop512、128 mel、FFT/window2048、e/slaney、40–16000Hz。阶段配置及实际语言 ID、音素数量保留于矩阵；预测器/vocoder 未当作额外 acoustic 包。

实际接口共同结构：acoustic 接 tokens/durations/f0、breathiness/voicing/tension/gender/velocity、scalar depth(float32)/steps(int64)，输出 mel；duration linguistic 接 word_div/word_dur，dur 输出 ph_dur_pred；pitch/variance linguistic 接 ph_dur，predictor 分别输出 pitch_pred 和三条 variance。完整 name/dtype/rank/static/symbolic dimensions 以机器矩阵为准（ONNX dtype 1=float32，7=int64，9=bool）。不从文件夹存在或参考文件名推断接口。ORT session 一次一个、CPU-only、线程有界，未执行这些模型，因此不宣称非静音 PCM 或可编辑反馈通过。

构建失败已读取日志并作必要局部修正：yaml-cpp 0.8.0 的旧 policy 在 CMake 4 不再兼容，在新增引擎目录设置最低 policy；其 uninstall 与 LMMS 同名，新增引擎挂接移到既有 uninstall 创建之后，不改宿主卸载目标。失败诊断保留于 `validation/A0-sdk-configure-failed-yaml-policy.log`、`A0-development-configure-failed-yaml-target.log`。

本阶段没有 GUI 布局改动或 GUI 验收需求；未使用 offscreen，也未启动原安装版。所有构建/验证在前台 PowerShell，先刷新共享环境，使用 build.log Tee 管线并检查退出码。六包仅本地只读 fixture，模型/图片/字典不进入提交。原工作区的 Song/VST 修改和既有未跟踪文件未纳入本阶段。

自动验收通过后更新计划、检查 git status/diff，仅提交 A0 文件并推送当前分支；确认远端后才开始 A1。
