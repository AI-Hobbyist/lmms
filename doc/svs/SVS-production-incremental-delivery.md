# SVS 生产部署清理与增量交付

2026-10-09。B0～B4 已依次提交推送，B4 为 `027ea2e8e462785c7febf9912246e2966c3e7431`。本交付仅清理用户要求的示例引擎和生成增量替换包，不扩展引擎/UI 范围。

## 部署范围

DAW 的 `build/Release/svs` 不再包含 SVSExample、SVSComputeExample 的示例 DLL 或扫描 manifest；SVSMinimal 原本未部署。原文件移至现有构建参考目录，仓库及独立 SDK 示例代码保留。正式部署保留 DiffSinger、共享计算客户端/worker、ORT/DirectML 和必要许可。主程序为 `build/Release/lmms.exe`，52 个启用 UI 插件及支持库保持 `build/Release/plugins`。

安装器的清理白名单仅包含这三个示例的 DLL 和 manifest 共六条路径，旧文件改名为 `.disabled` 备份，不再被加载。生产引擎不在白名单内。开发验收仍可构建示例 fixture；打包器始终排除上述六条路径。

## 增量替换包

| 项目 | 值 |
| --- | --- |
| 文件 | `build/packages/lmms-enhanced-incremental-027ea2e8e-20261009-144747-win64.zip` |
| 必需全量基包 | `lmms-enhanced-full-236c5f3fe-win64.zip` |
| 基包提交 | `236c5f3fee98e677e375cb1aa1a7db431b904f2c` |
| 运行文件 / 安装入口 | 3425 / 4 |
| 大小 | 55,880,975 bytes |
| SHA256 | `D0F9BDD25A80D2C8EBD8222BDBEE41954872149039FD9A82F02EAE4937F27837` |

增量涵盖基包之后的 B0～B4 运行变更，包括全局 CPU/DirectML 加速、模型内存管理和共享运行库。默认空闲 60 秒释放，时间可自定义，也可立即释放或常驻；CPU 回收模型内存，GPU 回收模型显存。修改驻留策略不改变音频/缓存身份。

运行文件来自 B4 构建，包装时安装脚本存在本交付工作区变更；包内说明保留该事实，不声称由无修改的 B4 提交直接重建。增量不能用于任意旧版本：安装器先核对基包未改动文件、增量文件 SHA256 与安装路径，再执行原位覆盖。个人配置、工程和声库不在载荷中。

## 验收记录

- `validation/B4-incremental-package.log`：全部 ZIP entry 与源文件哈希校验通过；清单及 ZIP 哈希分别见 `B4-incremental-manifest.json`、`B4-incremental-package.sha256`。
- `validation/B4-incremental-verify.log`：使用从最终 ZIP 提取的安装器，3425 载荷及六条清理路径验证通过。
- `validation/B4-installer-legacy-verify.log`：旧全量基包没有 RemoveFiles 字段，3484 载荷仍通过验证。
- `validation/B4-incremental-removal-rejection.log`：请求清理正式 DiffSinger DLL 被拒绝，正式 DLL 哈希未变。
- `validation/B4-incremental-install.log`：使用最终 ZIP 内安装器原位覆盖既有 `build/Release`，3425 文件安装后哈希全部通过；在预先放回旧示例的条件下，四个旧文件实际退出加载路径，全部六条清理路径不存在。停用备份随后移至 DAW 之外的现有构建参考目录。
- `validation/B4-incremental-post-install-verify.log`：安装后再次 VerifyOnly 通过；`B4-production-deployment-audit.log` 证明 DAW 内没有示例二进制，可用引擎和参考源码仍存在。附加源码存在性检查最初误写 C++ 路径，修正为实际 `sdk/svs/examples/minimal/minimal.c` 后复验通过，不涉及源码修改。
- `validation/B4-production-only-release.log` / `B4-production-only-release-QtTest.txt`：示例移出后，原部署的实际 Windows Qt Release 窗口、DiffSinger 推理和空声库启动通过，3 passed / 0 failed / 0 skipped，正常退出。未使用 offscreen。
- `validation/B4-incremental-installed-release.log` / `B4-incremental-installed-release-QtTest.txt`：实际安装增量后再次运行同项 Windows 实窗验收，3 passed / 0 failed / 0 skipped，正常退出。最终空声库真实窗口截图 `B4-native-empty-release.png` 已查看，主题/布局正常；没有残留 LMMS 测试进程。

首个候选包在 VerifyOnly 检出 PowerShell 换行语法错误；修复后重新打包并验证以上最终包。失败候选已改为 `.invalid`，不作为交付。

完整 B 阶段验收见 `DiffSinger-B4-validation.md`。主观听感/操作体验保持非阻塞 `MANUAL/PENDING`。
