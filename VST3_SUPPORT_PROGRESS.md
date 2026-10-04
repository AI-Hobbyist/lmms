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
| S7 | NOT_RUN | WaveShell and automated fault matrix. |
| S8 | NOT_RUN | Release, dependency verification and ZIP. |


## 最终范围（2026-10-05 用户指示）

- VeSTige：VST2 / VST3 Instrument 官方入口。
- VstEffect：VST2 / VST3 Effect 官方入口。
- 保留多路径扫描、WaveShell 子插件选择、multi-class VST3、Windows x86/x64 helper。
- 撤回全局 Mixer / PDC、侧链及高级输入输出路由扩张，不增加宿主入口；S6 REMOVED_BY_USER。
- 保留普通 MIDI 输入、主音频处理、参数、编辑器、工程 / 预设状态、VST2 兼容。
- 人工听音、授权激活、主观 UI / DPI / 认证：SKIPPED_MANUAL。
- S7、S8 待运行；各阶段完成后提交推送。

## 当前验证状态

清理前的完整测试已按用户指示停止，不能记为清理后 PASS。
2026-10-05：已撤回 AudioEngine / AudioBusHandle / Effect / EffectChain / Mixer / Song 的全局 Mixer、PDC、侧链和高级路由扩展，移除两个入口的高级路由界面与接线。仅保留 AudioEngine 原有句柄清理函数的 19 行缺陷修复：销毁轨道时也清理尚未进入首个音频周期的句柄，避免恢复／克隆后导出使用失效对象；没有新增路由或生命周期路径。
历史验证日志保留于 build/vst-s0、build/vst-s5。过长的原始进度及扩展范围差异已备份至 build/vst-s5/scope-rollback-backup，仅用于恢复与审计，不作为当前完成证明。

用户补充：扫描结果区分 Instrument / Effect，VST Instrument 集成现有乐器 Sidebar，Effect 集成现有 Effect Browser。仅发现／选择层传递身份键，实际实例化、宿主、路由及生命周期统一委托给 VeSTige / VstEffect；禁止第三种加载路径。S8 完成后必须在计划书记录最终保留／撤回清单和真实验收结果。

S5 验证：`build/vst-s5/final-scoped-build-10.log`、`build/vst-s5/final-scoped-tests-6.log`。64/64、213.21 s，故障标签 11 项通过。人工听音／商业授权／主观 UI 验收 SKIPPED_MANUAL。原失败日志保留，不覆盖为 PASS。
