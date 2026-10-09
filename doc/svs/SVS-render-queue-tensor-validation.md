# SVS 推理张量回收与全局渲染并发

2026-10-09。用户追加范围：推理完成及时释放临时张量；全局同时渲染线程默认 1，其余轨道依次排队。所有引擎共用宿主调度上限，CPU/GPU 均适用。

## 行为

- SVS 全局设置新增同时渲染线程，范围 1～16，默认 1；保存后立即更新调度器，并写入既有 `svs/concurrency` 配置。取消编辑不发布设置。显式保存的旧并发配置保留。
- 默认最多一个轨道片段执行整次渲染，包括其各个分段；同优先级任务按提交顺序进入。保留已有优先级与引擎实例串行保护。取消排队任务不占运行槽；降低并发时，已在运行的任务完成后再按新上限取任务，不提前释放运行槽。
- worker 完成输出复制后，立即销毁 ORT 输入/输出张量和临时副本，再执行 profiling/回复。关闭 CPU arena 和 memory pattern，避免临时张量缓冲随驻留 session 保留。DiffSinger 复制结果后先释放共享计算结果，再调用观察者；acoustic 输入和 mel/vocoder 输入在后续 PCM 处理前回收。
- 模型仍按立即释放／空闲释放（默认 60 秒）／常驻策略管理。供下一阶段使用的必要输出保持到消费完成；最终音频与磁盘缓存保留。公开 C ABI 的结果由调用方拥有，仍必须等调用方释放，不能使已返回的张量指针失效。
- `memory_status` 新增 `tensorAllocatedBytes` 诊断字段，统计上下文中在途共享缓冲和调用方拥有的结果，未改变 ABI 或音频缓存身份。不将模型、编译资源或进程总内存当作临时张量占用。

## 验收

- `RenderQueue-red`：修改前默认并发测试失败，实际 2、期望 1。
- `RenderQueue-queue-final`：4 passed / 0 failed。四个独立引擎模拟不同轨道，验证默认峰值 1、FIFO、取消后继续、运行中上调/下调及范围约束。
- `RenderQueue-regression-first`：9 passed / 1 failed；失败为新增队列 fixture 漏填能力声明，补齐后以上复验通过。其余既有取消/预算/关闭、全局策略、分段依赖、真实 DiffSinger 分段和原生设置用例已通过。
- `RenderQueue-tensors-conformance.log`：CPU 32 次连续推理，结束后共享缓冲已释放，结果释放后统计回到零；持有结果时只剩 16 bytes 的实际输出而非 512 MB 的传输缓冲。CPU、AMD 780M、NVIDIA RTX 5060 的模型驻留/租约/释放/重载、取消和异常检查通过。
- `RenderQueue-DiffSinger-DML.log`：真实声库 duration、pitch、variance、acoustic、vocoder DirectML 节点与 fresh CPU 数值对照、PCM 对照、缓存和取消通过。
- `RenderQueue-native-compute-settings.png` 已查看真实 Windows 窗口：新控件、主题、布局正常，编辑/保存/重新打开的并发值均检查通过。没有使用 offscreen。
- `RenderQueue-release-final`：清理测试生成的示例 DLL/manifest 后，原部署 `build/Release/lmms.exe` 的实际 Windows Qt 窗口及退出验收通过，3 passed / 0 failed / 0 skipped。截图已查看；未更改生产 GUI 验证路径。
- `RenderQueue-package.log` / `RenderQueue-package-verify.log`：最终 ZIP 的 3425 个运行文件及四个安装入口逐 entry 哈希通过；使用从 ZIP 提取的安装器 VerifyOnly 验证通过，包括六条示例清理路径。

## 交付包

`build/packages/lmms-enhanced-incremental-4473466fc-20261009-152458-win64.zip`，55,881,891 bytes，SHA256 `25FF74407DDB8C763790647B7F89E3EE5E4EDF5920F7C0869AF8214AAE17F17E`。基包仍为 `lmms-enhanced-full-236c5f3fe-win64.zip`；已安装上一增量的用户也可再次覆盖。清单及 ZIP 哈希分别见 `RenderQueue-package-manifest.json`、`RenderQueue-package.sha256`。

包名提交 `4473466fc` 为本增量的父提交，包内记录工作区功能变更，不声称纯父提交重建。运行包取自本次已验收的构建；打包后工作区另有并行的未选声库预览/导入修改和构建，本检查点仅提交本任务代码，保留其他改动，不将那些后续构建作为本 ZIP 的验证依据。开发测试重建的示例 DLL/manifest 已在本次实窗验收前移出部署目录；包装白名单继续排除示例。

全部构建、测试、打包沿用现有目录，前台 PowerShell PTY，载入 SDK 环境，`Tee-Object build.log` 完整记录并检查退出码。首次编译发现新增测试重复声明 `reopened`，修正 fixture 后构建通过；生产代码不因此改变。

截图中的原工程未提供，未宣称复现或彻底排除原工程的系统提交内存不足；本轮验收针对上述张量生命周期和全局调度行为。共享单次传输上界仍为 512 MB，上下文预算仍为 1 GB，没有扩张已有安全边界。
