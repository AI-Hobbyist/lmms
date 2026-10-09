# B0：共享计算协议、依赖和设备冻结

日期：2026-10-09。范围：B0；未声明 B1～B4 完成。

独立 `svs_compute.h` ABI 1.0 冻结固定宽度状态/句柄、描述符 size、表 version/features、
模型/会话/可取消 run/result、借用输入与模块所有权输出。独立于旧 `svs.h`，该旧表没有修改。
冻结全局 computePolicy、引擎自愿声明、CPU-only 阶段、一次整阶段 CPU 回退、epoch/超时/
缓冲预算和缓存实际路由规则，详见 `sdk/svs/docs/compute.md`。B1 再交付执行实现和 RAII。

依赖：官方 native Microsoft.ML.OnnxRuntime.DirectML 1.23.0，发布 revision
`0b2c4ac474a32ed700fd75435fb180ddfbbb4af6`；nuspec 声明 Microsoft.AI.DirectML 1.15.4。
仅提取 native x64 include/lib/DLL，不执行 managed/.NET。归档和三个 DLL SHA256 在
`src/core/svs/compute/dependencies.lock.json`。ORT MIT；DirectML DLL 使用其
Microsoft redistributable 条款，不能将代码样例的 MIT 误称为 DLL 许可。部署随附两种许可。
新 SDK 环境变量通过 Enter-LmmsEnvironment.ps1 刷新；A 的 CPU SDK 保持原位。

| 自动验收 | 证据与结果 |
| --- | --- |
| 原 build 原生 probe 构建 | `validation/B0-probe-build.log`，PASS |
| AMD Radeon 780M | `dxgi:00000000:000148ca`；D3D12/DML/ORT Add PASS，1 个实际 DML 节点、0 CPU 节点 |
| NVIDIA RTX 5060 Laptop | `dxgi:00000000:00016a3c`；同上 PASS |
| 六包 DML session 兼容矩阵 | `DiffSinger-B0-gpu-matrix.json`；48/48 session PASS，使用 NVIDIA 实际 LUID，各模型独立原生进程，未回退 CPU |
| 旧 ABI 兼容设计 | `svs.h` 与旧接口前缀未修改；compute 是独立可选协议，不接入者继续 CPU，缺可选 DLL 不阻断 LMMS 启动 |
| 纯 C 公开头文件检查 | `validation/B0-abi-build.log`、`B0-abi-test.log` |
| 不存在的设备 LUID | `validation/B0-missing-device.json`、`B0-missing-device.log`；明确失败，无固定设备 0 替代 |

五包 vocoder 配置 force_on_cpu=false，遐蝶该字段未声明（缺省 false）；六包当前没有配置
强制 CPU 阶段，也没有本轮 session construction 被拒模型。模型中 ORT CPU 节点分区
与整阶段回退区别已经冻结。未知/更高 opset 不凭 ONNX 扩展名推定可用；实际 session/run
结果为依据。错误数据不尝试 CPU 掩盖，取消不回退。force_on_cpu=true 的正向约束 fixture
及 unsupported/OOM 等一次回退执行验收保留在 B1/B3。

矩阵只证明 session 创建。报告内 `available=false` 是模型模式未执行设备 Add 的缺省值，
不覆盖独立设备报告的 available=true；execution=PENDING_B3。真实声学 PCM、模型 DML
节点及 CPU/DML 同 seed 数值对比必须在 B3 完成。容差已在 compute.md 事前冻结，不能
看结果后放宽。两块 GPU 均存在，后续多设备验证可实际运行。

Probe 位于原 `build/Release/svs/compute/SVSComputeProbe.exe`，三个配套 native DLL 同目录；
开发 LMMS 保持 `build/Release/lmms.exe`，普通插件仍为 `build/Release/plugins`。
此阶段不改变生产 UI，不需要 GUI 验收，未使用 offscreen。六包仅只读本机 fixture，
没有任何模型、字典、图片、个人配置进入提交。

本阶段自动验收通过后更新计划、检查 diff、提交并推送 master，确认远端才进入 B1。
