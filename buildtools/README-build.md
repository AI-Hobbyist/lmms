# Windows 前台编译与进度

依赖 PowerShell 7、Python 和 `tqdm`（`python -m pip install tqdm`）。
先按项目 SDK 约定配置原 `build` 目录，再从项目根目录运行：

```powershell
python buildtools/build.py --config Release --target lmms --jobs 2 --window
python buildtools/build.py --config Debug --target lmms CommandBusTest --jobs 2 --window
python buildtools/build.py --config Release --test '^CommandBusTest$' --window
```

参数：

- `--config`：`Debug` / `Release`，默认 `Release`。
- `--target`：一个或多个目标，默认 `lmms`。
- `--jobs`：1～16，默认 2，适应本机 16 GB 内存。
- `--window`：额外显示进度窗口；编译进程始终由当前会话前台等待。
- `--test REGEX`：执行匹配的 CTest，替代编译。

脚本通过已配置的 Visual Studio `.slnx` / `.sln` 和 `.vcxproj` 读取编译项，
按所选配置排除不参与构建的源码，并结合原生 MSBuild 的头文件依赖记录及对象文件
时间戳识别本轮需要重编的文件。tqdm 的最大值是本轮源码编译单元数，
每个非空 `.obj` 成功更新后增加计数，预编译且未变化的文件不计入最大值。
同一源文件为不同目标生成不同对象文件时，各算一个编译单元。
生成代码或编译命令在构建中引入额外工作时，计数会按实际对象更新补齐。
CMake 重写 `.vcxproj` 本身不计为源码重编条件；以源码、实际头文件依赖及对象更新为准。

进度条旁显示当前 `.cpp` / `.c` 文件名。编译计数完成后的链接、复制等步骤单独显示为
`Linking / finishing build`；编译 100% 不表示链接已成功。
测试进度来自 CTest 实际完成记录。

所有输出经前台 PowerShell `Tee-Object` 写入根目录 `build.log`。
当前会话同时回传完整 stdout/stderr；`--window` 的独立窗口仅展示简洁进度条与当前源码。
成功或失败均显示退出码，失败时显示日志尾部；结果显示 5 秒后，脚本及进度窗口自动退出。
GUI 测试使用原生 Windows Qt 后端，禁止 offscreen。
本脚本当前支持 Windows x64 Visual Studio 生成器；不会创建其他构建或部署目录。

组成：

- `build.py`：命令行入口、前台进程等待、tqdm 和独立进度窗口。
- `build_progress.py`：编译项、原生依赖与对象文件计数。
- `Build-Lmms.ps1`：SDK 环境、原生构建/测试及完整日志。
- `BuildProgressLogger.cs`：MSBuild 项目事件，用于构建阶段状态。

2026-11-15 验证：真实增量编译 `CommandBusTest` 为 `0/1 → 1/1`，退出码 0；
其后原项目的增量计划为 0 个待编译源文件。完成与错误状态的进度窗口均约 5.5 秒自动退出。
以上验证不等同于 MCP 功能回归通过；功能验证单独记录。
