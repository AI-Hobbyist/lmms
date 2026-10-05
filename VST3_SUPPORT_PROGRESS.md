# VST3 implementation progress

Goal created 2026-10-04. User scope revised 2026-10-05: prioritize production VeSTige/VstEffect VST3 use; remove S6. Finish S0-S5, then S7-S8; commit/push each completed stage.
Windows only: x64 DAW, x86/x64 plugin helpers. Optional dependencies may be obtained from
official online sources and included in the build as authorized by the user.

| Stage | Status | Evidence / remaining work |
| --- | --- | --- |
| S0 | PASS | Contract, call graph, frozen legacy project and entry inventory recorded. Windows x86/x64 protocol, proxy, MIDI, editor, native DLL project/command/snapshot/preview/export baseline PASS. Runtime Carla baseline unavailable with DummyCarla; gap recorded for S6. |
| S1 | PASS | Production VstPlugin/RemotePlugin migrated on Windows; dual-ABI supervised native helper, bounded control and preallocated realtime path. Full Release build and 41/41 CTest PASS (51.54 s). Stage delivered in the commit containing this completion record. |
| S2 | PASS | Supervised shell full enumeration/selection, persistent unsigned shellid, production effect/instrument selection and GUI model feedback. Full Windows Release build PASS; 43/43 CTest PASS (52.14 s). Delivered in the commit containing this completion record. |
| S3 | PASS | Windows x64/Win32 native adapter and scanner integrated. Exact factory CIDs, Unicode bundles, lifecycle, float32/float64, state, ParamID, native editor, transport/events, feedback and MIDI/parameter/latency/I/O/reload restart paths verified. Full Windows Release build PASS; 49/49 CTest PASS (110.41 s), including 50 editor lifetimes per ABI. Delivered in the commit containing this completion record. |
| S4 | PASS | Multi-root migration preserves uservst; supervised bundle/PE/Unicode discovery, exact identity/version candidates, cache/quarantine/cancellation and owned asynchronous jobs/settings/selector publication implemented. Final full Windows Release build PASS; 54/54 CTest PASS (100.26 s). Delivered in the commit containing this completion record. |
| S5 | PASS | VeSTige / VstEffect 实际 VST3 加载、处理、参数、编辑器、状态、preset、clone、undo、命令、导出和旧 VST2 双架构回归通过；分类与 Sidebar / Effect Browser 仅传递身份键。清理后全量 Windows Release 编译 PASS，64/64 CTest PASS（213.21 s）。本完成记录随阶段提交推送。 |
| S6 | REMOVED_BY_USER | User explicitly removed Carla isolation from this delivery on 2026-10-05. |
| S7 | PASS_WITH_MANUAL_SKIPS | WaveShell 16.6（2 类）/16.7（725 类、14 乐器）与独立 SDK 完整类集合一致；Magma Mono/Stereo、Q10、Electric88 的双实例、主音频、MIDI、参数和 state/preset PASS。11 项自动故障测试 PASS；人工商业认证／听音／授权跳过。详见 VST3_SUPPORT_VALIDATION.md。 |
| S8 | PASS_WITH_MANUAL_SKIPS | Windows Release 全量编译、64/64 CTest（180.92 s）、182 个 PE 运行库／架构／许可审计、最终 ZIP 解压后干净 PATH 启动和两个官方入口双架构实际音频渲染 PASS。发布包与 SHA256 见验证记录；本完成记录随 S8 提交推送。 |


## 最终范围（2026-10-05 用户指示）

- VeSTige：VST2 / VST3 Instrument 官方入口。
- VstEffect：VST2 / VST3 Effect 官方入口。
- 保留多路径扫描、WaveShell 子插件选择、multi-class VST3、Windows x86/x64 helper。
- 撤回全局 Mixer / PDC、侧链及高级输入输出路由扩张，不增加宿主入口；S6 REMOVED_BY_USER。
- 保留普通 MIDI 输入、主音频处理、参数、编辑器、工程 / 预设状态、VST2 兼容。
- 人工听音、授权激活、主观 UI / DPI / 认证：SKIPPED_MANUAL。
- S7 / S8 自动验证已通过；S0～S5、S7、S8 均按阶段提交推送，S6 已删除。人工验收按指示跳过。

## 当前验证状态

清理前的完整测试已按用户指示停止，不能记为清理后 PASS。
2026-10-05：已撤回 AudioEngine / AudioBusHandle / Effect / EffectChain / Mixer / Song 的全局 Mixer、PDC、侧链和高级路由扩展，移除两个入口的高级路由界面与接线。仅保留 AudioEngine 原有句柄清理函数的 19 行缺陷修复：销毁轨道时也清理尚未进入首个音频周期的句柄，避免恢复／克隆后导出使用失效对象；没有新增路由或生命周期路径。
历史验证日志保留于 build/vst-s0、build/vst-s5。过长的原始进度及扩展范围差异已备份至 build/vst-s5/scope-rollback-backup，仅用于恢复与审计，不作为当前完成证明。

用户补充：扫描结果区分 Instrument / Effect，VST Instrument 集成现有乐器 Sidebar，Effect 集成现有 Effect Browser。仅发现／选择层传递身份键，实际实例化、宿主、路由及生命周期统一委托给 VeSTige / VstEffect；禁止第三种加载路径。S8 完成后必须在计划书记录最终保留／撤回清单和真实验收结果。

S5 验证：`build/vst-s5/final-scoped-build-10.log`、`build/vst-s5/final-scoped-tests-6.log`。64/64、213.21 s，故障标签 11 项通过。人工听音／商业授权／主观 UI 验收 SKIPPED_MANUAL。原失败日志保留，不覆盖为 PASS。

## S8 完成记录（2026-10-05）

版本 `1.3.0-alpha.2.35+8099048`；生产二进制源于 S7 提交 `8099048cc`，S8 仅新增打包脚本和交付记录。根 Release 编译 PASS；64/64 CTest PASS（180.92 s），故障标签 11 项通过（42.30 s）。最终 ZIP 中 182 个 PE 的架构、依赖闭包和 SHA256 核对通过，包含 x86/x64 VST2/VST3 四个 helpers 及许可，不包含商业插件、SDK 源码、测试工具或 Debug 运行库。

最终 ZIP 解压到独立目录后，清空 LMMS / Qt 开发环境变量，PATH 仅保留 Windows 系统目录：启动 PASS；VeSTige x86/x64 MIDI 渲染峰值 16254、非零样本各 88200；VstEffect x86/x64 峰值 6035、RMS 增益约 0.25；真实 WaveShell Element 峰值 7212、非零样本 98391。Q10 主音频渲染 PASS。Electric88 可能需要配套采样库，按用户指示排除该项验收，不列为未完成；采样库不随包分发。

全局 Mixer / PDC、侧链、高级输入输出／MIDI 目的地、路由撤销历史、额外宿主入口和 S6 已撤回。Instrument / Effect 分类及 Sidebar / Effect Browser 只传递元数据身份键，实际加载／宿主／路由／生命周期仍由 VeSTige / VstEffect 承担。计划书最后检查表已记录最终保留／删除项。

发布包：`build/vst-s8/lmms-1.3.0-alpha.2.35-vst3-win64-release.zip`（43,821,367 bytes）。SHA256：`db45f3325f427ba49cd11622f6911c4121df0d30304fa94c940f59dc34dbd970`。听音、人工商业授权／认证和主观编辑器／DPI 验收为 SKIPPED_MANUAL。其它平台不在本次范围。


## 当前交付完成：S8 后发布版实机修复（2026-10-05）

**PASS_WITH_MANUAL_SKIPS**；S0～S5、S7、S8 完成；S6 REMOVED_BY_USER。旧 S8 离线/渲染结果不等于 SDL 实时验证，本次补充并修复真实入口、编辑器和 Kontakt 的问题，详见计划 §11 与验证记录末节。

- 最终生产源 `d3508bbbad9d7e3a87f4f4168fe19350b25bed15`（含 `add227a61` / `14542d852`），Windows Release 编译 PASS。
- 65/65 CTest PASS（201.96 s），11 项故障测试 PASS（43.41 s）；8 种实际 GUI/SDL 入口组合 PASS。
- Kontakt 8.12.1 最新加载 4164 ms；安装版 Tools/Loops/Instruments、持续播放、关闭/重开编辑器 PASS，同一 helper 存活。空采样器/缺少采样库时无声不判失败；Kontakt/Electric88 可能需要配套采样库，Electric88 不列未完成。
- WaveShell Element/Q10 最新实际主音频和编辑器重开 PASS（峰值 0.207691/1.16406）。
- 包内 184 个 PE、正确架构/运行库/许可、ZIP CRC 与 SHA256 PASS；安装版已更新，全文件核对 PASS，替换前主程序/helper 关闭，个人配置保留。备份仍在 `build/vst-live-fix/installed-before-fix`。
- computer use 已结束；测试主程序/helpers 已关闭。人工听音、授权认证、主观 UI/DPI 审核 SKIPPED_MANUAL。
- 保留与撤回范围沿用计划 §10/§11：两个官方入口、多路径/WaveShell/multi-class/分类选择保留；S6、第三路径、Mixer/PDC/侧链/高级路由撤回。

**当前包**：`build/vst-live-fix/lmms-1.3.0-alpha.2.36-vst3-kontakt-ui-fix-win64.zip`。SHA256：`54abb7710b3fbb5c2ac9016c4678e71ac6440e2546682f2f321b00a28632935d`。显示版本 `1.3.0-alpha.2.36+9682079`；源提交以包内 README 为准。此包替代 alpha.2.35、realtime-fix、final-fix 等早期候选，不继续使用那些历史包。
