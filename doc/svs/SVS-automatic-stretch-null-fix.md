# 自动音素拉伸 Failed (2) 实机复现与修复

2026-10-07，使用 computer-use 技能操作实际开发版窗口。编译目录仍为 `build`，程序为 `build/Release/lmms.exe`，实际加载 `build/Release/svs/SVSExample/SVSExample.dll`；普通插件仍为 `build/Release/plugins`。没有创建替代构建或插件部署目录。

## 实机复现与根因

新建默认歌词 la 的自动音素音符，初次合成为 Ready，显示 l/a 两块。铅笔拖动尾端，从 12 tick 延长到 60 tick 后立即 Failed (2)，音素块消失。保存的本机复现工程确认 `phonemes={"segments":null}`。

共享伸缩操作在可变 QJsonObject 上读取不存在的 `segments` 使用了 operator[]，读取本身插入 null；插件将存在的 segments 视为手工覆盖，严格检查要求非空数组，因此返回 SVS_INVALID_INPUT (2)。第 29 节最小时长修复覆盖另一种真实失败，但未覆盖这个默认自动音素路径。

修复限定于 SVSStretchOperations：改为非插入式 value() 读取；伸缩时删除旧版误写的 null。正常手工数组保留，时长限制继续生效。旧故障工程加载后不会自动清理；再次伸缩时恢复自动音素并正常合成。

## 验证

- 新回归首先在旧代码稳定失败：`validation/SVS-automatic-stretch-red.txt`，2 passed / 1 failed。
- 新测试以真实鼠标创建默认音符，覆盖画布和音素条上半区首尾伸缩，每次检查成功音频、自动音素 JSON 未被写入和反馈恢复；另覆盖旧 null 工程恢复。
- Release 主程序及测试在原目录前台编译通过。原生 Windows Qt 完整回归：64 passed / 0 failed / 0 skipped，CTest 1/1 通过。证据：`validation/SVS-automatic-stretch-green.txt`、`validation/SVS-automatic-stretch-regression.txt`、`validation/SVS-automatic-stretch-ctest.log`。
- Computer Use 实机：旧故障音符首尾延长、首尾缩短均 Ready，两块音素恢复；新建自动音素音符首尾延长均 Ready；音素条上半区首尾延长正常联动音符；下半区 l/a 交界单独拖动成功且保持 Ready。三条有值只读曲线正常显示。
- 保存后的恢复音符为 `phonemes={}`；单独编辑过交界的新音符为有效 segments 数组（l 90 tick、a 66 tick）。本机红/绿工程仅保留于现有 build，不进入工程或发布依赖。
- 实机测试窗口已保存并关闭，Computer Use 已结束。没有使用 offscreen。

实测二进制 SHA256：lmms.exe `36A1722C828924AA140E00DD7F5AF0D19342C28E0804438B5A5EAA3602F53D91`；SVSExample.dll `E41473E3319234F1B5E3D4CDC9EA82ECCA7C968DA91C3B17DDF307A167924A41`。
