# B3 DiffSinger 阶段路由、回退与缓存验收

状态：PASS。Windows x64，2026-10-09。B4 尚未开始。

DiffSinger 的 duration、linguistic、pitch、variance、acoustic 和 vocoder 均消费冻结的共享计算策略。只有实际声库配置的 `force_on_cpu=true` 限制对应阶段；没有 CPU 约束的声码器可运行 DirectML。能力声明、每阶段执行反馈和缓存身份均反映实际路由。GPU 后端失败最多重试 CPU 一次；取消、非法张量和资源预算错误不触发该重试。下一请求可以重新尝试所选 GPU。

宿主在结果写入缓存前根据实际阶段路由重算键及保留段签名，避免把 CPU 回退结果作为 DML 成功结果复用。阶段排序和诊断文字不改变身份。PCM 内存/磁盘缓存与保留段明确标记命中，移除历史 provider、计时、资源和 worker epoch；不能把读取缓存作为新 GPU 执行证据。

## 六包真实 GPU 比较

NVIDIA GeForce RTX 5060 Laptop GPU，LUID `dxgi:00000000:00016a3c`。六包各 8 个模型，共 48 次首次推理：使用相同输入张量与 seed=1234，逐阶段重新运行 CPU 对照，检查形状、类型、有限值及 B0 冻结容差。整数/bool 输出严格一致；每个 GPU 模型均有实际 DML 节点。声学模型包含 1505–1507 DML 节点及 73 CPU 分区节点，因此不宣称全节点 GPU 执行。

| 声库 | 全链相关性 | 归一化 RMS | 电平差 dB | DML 首次 duration+render ms | CPU render ms | 缓存重放 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 芙宁娜 | 1 | 0.000769001 | 0.0000218548 | 12039.7 | 2935.04 | 18.3962 |
| 那维莱特 | 0.999999 | 0.00133765 | 0.0000294811 | 12188.5 | 2982.04 | 15.3151 |
| 纳西妲 | 1 | 0.000784125 | 0.0000150461 | 11610 | 3294.67 | 19.4723 |
| 温迪 | 0.999999 | 0.00109728 | 0.0000126778 | 12761.8 | 2867.18 | 19.5746 |
| 希格雯 | 1 | 0.000618301 | 0.0000179945 | 12358.7 | 3015.82 | 23.8337 |
| 遐蝶 | 1 | 0.000808219 | 0.00000469466 | 14800.2 | 3242.92 | 16.6797 |

以上计时范围不同：DML 首次包括模型初始化和 duration，扣除测试观察器的 CPU 重放；CPU 数字仅为随后合成 render，CPU worker 已被对照预热。缓存重放也不等于 GPU 推理速度，不能据此算加速比。逐阶段运行时间、Windows 工作集/峰值工作集、DXGI 本地显存使用/预算及采样峰值保留在日志中。GPU 采样峰值约 2.59 GB；不是持续监测的驱动高水位。

没有对齐、平移或重采样波形。原始浮点 origin 最大相差 1.5501e-7 秒，实际 48 kHz 输出样本起点完全相同，日志记录原始秒差。这是宿主离散 PCM 定位的检查；不把浮点秒数宣称完全相等。首次严格浮点比较的失败记录为测试单位问题，数值容差没有放宽。音素身份/布局、完整 pitch/variance 反馈也按原定限制检查。

首个真实声库另以内存配置 `force_on_cpu=true` 验证训练声码器留 CPU，acoustic 仍有 DML 节点；实际声库文件未改动。六包原始配置没有强制 CPU vocoder，因此不人为施加统一限制。

## 故障与资源边界

真实缺失 LUID 初始化选择 CPU，并保留原因。OOM、unsupported、GPU initialization 三类使用内部测试观察器在真实 GPU 输出之后注入受控后端错误，验证一次 CPU 重试、正确输出、原因以及下一请求 DML 恢复；不是物理显存耗尽或真实驱动故障证据。CPU 再失败时结束，不循环重试。错张量、预取消及取消同时出现后端错误均不回退。

同设备运行 10 批、每批 4 个并发 session。共享上下文沿用 1 GiB 缓冲预算与每 run 512 MiB 上限，超预算明确拒绝；每批完成 1–3 个请求，其余只允许预期预算错误，每批结束后串行推理成功证明预算回收。没有为测试放大生产限制。

## 证据

- `validation/B3-six-directml-final.log`：六包 48 模型逐阶段、全链及反馈对照、实际路由、CPU-only vocoder 正向测试、缓存/seed/曲线/控制/手工音素/取消/分块/tempo。
- `validation/B3-routing-faults.log`：受控错误、真实缺设备、取消/非法数据、并发预算与释放。
- `validation/B3-host-QtTest.txt` / `B3-host-test.log`：5 passed / 0 failed，内存与磁盘身份/PCM恢复、全局策略、第二 AI 实际 DML clip/导出与重启；第二 AI 模型为合成 fixture。
- `validation/B3-final-cache-build.log`：实际 `build/Release/lmms.exe` 及宿主测试重编通过。
- `validation/B3-performance-build.log` / `B3-acceptance-build.log`：共享 worker 和 DiffSinger 模型测试原路径构建通过。

六包 CPU 回归已通过（`validation/B3-six-cpu-regression.log`）；AMD 780M（`dxgi:00000000:000148ca`）首包 8 模型逐阶段与全链对照通过，相关性 1、归一化 RMS 0.000781365、电平差 0.0000246145 dB；同一用例的分块/tempo 尾项也已通过。证据 `validation/B3-amd-directml.log`。人工听感 MANUAL/PENDING。私人模型和试听音频不提交、不分发。
