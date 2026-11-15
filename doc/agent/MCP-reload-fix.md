# MCP 工程重载修复与 Windows 增量交付

日期：2026-11-15。生产基线：`D:\Program Files\LMMS`，实际主程序版本为
`1.3.0-alpha.2.207+8223da3`，对应源码 `8223da3e4`。
本次开始时源码 HEAD 为 `1e4e33a11`，比生产版多一个 `svs.setPitchCurve` 命令提交。
本文件记录本次工作；验证和包信息在实际执行后补齐。

## 修复范围

- `Song::loadProject` 遍历 trackcontainer 时，无论子节点是否为 track 都推进游标，
  避免注释或非轨道节点导致主线程无限循环。
- `Song::clearProject`、`restoreProjectState` 使用有作用域的加载状态，覆盖会派发事件的
  工程清理和对象重建过程；结束后恢复原状态并正常刷新工程长度。
- `CommandBus::execute` 在参数校验及事务处理之前拒绝加载/恢复期间进入的请求，
  返回 `project_busy`，防止读取半成品模型或触发嵌套回滚。
- 工程文件加载失败提前返回时，恢复加载状态和此前的 journalling 状态。

在受控测试中临时还原修复前的两处生产源码，已经复现：非轨道子节点加载超过 8 秒仍不返回；
加载失败残留 loading 状态；事务恢复期间派发另一条未知命令后递归回滚，进程以
`0xC0000005` 退出。嵌套回滚先弹出同一事务，外层随后继续弹出，破坏事务栈。
这确认了本次修复所针对的卡死和崩溃机制；原始用户现场没有转储，未把某个具体 GUI
悬空对象当成已确认的崩溃点。

普通成功编辑直接修改当前内存模型，提交事务不会重载。`dryRun`、失败回滚和快照撤销
继续保持原有语义；本次没有重写所有工具的预演或增量逆操作。
没有改变公开类的数据布局或添加测试专用生产渲染路径。

## 前台构建和真实进度

依赖 Python `tqdm` 和 PowerShell 7。在项目根目录运行：

```powershell
python buildtools/build.py --config Release --target lmms CommandBusTest --jobs 2 --window
python buildtools/build.py --config Debug --target lmms --jobs 2 --window
python buildtools/build.py --config Release --test '^CommandBusTest$'
```

- `--config`：`Debug` 或 `Release`，默认 `Release`；生产替换包仅使用 `Release`。
- `--target`：一个或多个已配置的 Visual Studio 构建目标，默认 `lmms`。
- `--jobs`：并发数，默认 2，适应本机 16 GB 内存。
- `--window`：额外打开独立进度窗口；实际编译仍在调用会话前台执行。
- `--test REGEX`：运行匹配的 CTest，替代编译操作。

Python 从已配置的 `.slnx` / `.sln` 和 `.vcxproj` 读取所需项目编译项，结合原生头文件
依赖记录及对象文件时间戳，仅统计本轮需要重编的源码。非空 `.obj` 成功更新后才增加计数，
进度条旁显示当前 `.cpp` / `.c`。链接、复制属于编译后的独立阶段。
MSBuild logger 报告项目阶段状态；测试进度来自 CTest 实际完成记录。
完成或错误结果显示 5 秒后，脚本和窗口自动退出。完整参数见 `buildtools/README-build.md`。

`Build-Lmms.ps1` 先加载 SDK 环境，再使用原 `build` 目录。
所有编译和测试输出经 PowerShell `Tee-Object` 保存在根目录 `build.log`，
脚本立即传播退出码；失败后显示日志尾部。进度事件和状态存于忽略的 `build` 目录。
有 GUI 的测试固定使用原生 Windows Qt 后端，禁止 offscreen。

本任务开始时旧 build 和 VCPKG_ROOT 均已不存在。构建依赖从本机 vcpkg 二进制缓存恢复到
`build/vcpkg_installed/x64-windows`；音频依赖 Release DLL 已按实际生产文件哈希选择。
未更换生产运行库，也未重新部署独立 DiffSinger 引擎。

## 验证状态

- 配置：已生成原路径 `build/lmms.slnx`；日志 `build/configure.log`。
- 首轮修复版 CommandBusTest：25 passed；还原旧源码后的三个 RED 场景均已复现。
- 最终修复版 CommandBusTest、CoreCommandsTest、McpProtocolTest、McpQueueTest：4/4 passed，
  总耗时 2.61 秒；日志 `build/final-agent-tests.log`，单项 QtTest 结果见 `build/tests/*-results.txt`。
- 上述 QtTest 合计 43 passed、0 failed、1 skipped；跳过项为 CoreCommandsTest 的原生插件
  DLL 集成用例（未设置 `LMMS_AGENT_PLUGIN_TEST_PATH`），不计为通过。
- 主程序编译、插件导入符号兼容核对、增量包验证：PENDING。
- 原生 GUI / 人工操作验收：`MANUAL/PENDING`，不作为交付阻塞条件，不记为 PASS。
- 用户取消过第一轮编译；该轮退出码 `0xC000013A`，不计为验证通过。

## 增量包

在 Release 构建及兼容核对通过后，使用：

```powershell
pwsh -NoProfile -File buildtools/New-McpReloadPackage.ps1 -BaseDirectory 'D:\Program Files\LMMS'
```

生成目录为 `build/packages`。包仅替换 `lmms.exe`，复用原安装中的插件、引擎、运行库和资源。
基线清单来自实际生产二进制的 SHA256，不依赖安装目录中可能陈旧的旧 manifest。
复用已有 `Install-Replace.ps1` 的包内哈希、基线校验及路径检查。
生产目录不由本任务自动覆盖；安装前关闭 LMMS 并备份原 EXE，便于回滚。

包文件、大小、SHA256、兼容核对和安装器 VerifyOnly 结果：PENDING。
