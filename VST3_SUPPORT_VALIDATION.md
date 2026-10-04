# Windows VST3 验证记录

## S7：WaveShell 与故障矩阵

2026-10-05，S5 提交 `8941803a3`。测试工具仅在 BUILD_TESTING 构建，不安装到 LMMS；其扫描和实例探测复用现有 HostSession / Vst3HostProxy。生产 Sidebar / Effect Browser 仍只交给 VeSTige / VstEffect 加载。

| 模块 | 全部类 | Instrument | Effect | SDK 类集合比较 | 模块 SHA256 |
| --- | ---: | ---: | ---: | --- | --- |
| WaveShell 16.6 x64 | 2 | 0 | 2 | PASS：原始 CID、名称、分类、版本、子分类完全一致 | `038a7e114dffc96d48adc0e6b9dc2355073d74f4054aa2c2f8aa70fa2e9734a5` |
| WaveShell 16.7 x64 | 725 | 14 | 711 | PASS：原始 CID、名称、分类、版本、子分类完全一致 | `50f29e25afac67b46b2c2b24f815caaa62e6cdefd0b197d79668dfcb8202f5b6` |

16.6 的安装返回 2 类，而非按文件名推测更大集合；16.7 返回 725 类。两个安装同时保留独立模块路径／版本／指纹；不移动 Waves 安装，不测试 ARA。

| 实际类探测 | 结果 |
| --- | --- |
| 16.6 Magma StressBox Mono | PASS：parameters=136, inputs=1, outputs=1, distinct_helpers=55228,53220, state_bytes=900, parameter_write=1, midi=1 |
| 16.6 Magma StressBox Stereo | PASS：parameters=136, inputs=2, outputs=2, distinct_helpers=50960,45604, state_bytes=900, parameter_write=1, midi=1 |
| 16.7 Q10 Stereo | PASS：parameters=189, inputs=2, outputs=2, distinct_helpers=50484,29776, state_bytes=1278, parameter_write=1, midi=1 |
| 16.7 Electric88 Stereo | PASS：parameters=165, inputs=0, outputs=2, distinct_helpers=30344,54956, state_bytes=3197, parameter_write=1, midi=1 |

每类创建两个不同 PID 的 helper，执行 state 和 VST3 preset 保存／跨实例恢复、可写参数控制、普通 MIDI note-on/off、8 个 512 帧离线块，检查有限输出及正常关闭。这里只验证基本主音频；没有额外 Mixer、侧链或高级路由。不是完整商业音色或所有 725 类的认证。

自动故障矩阵：S5 清理后全套 64/64 PASS（213.21 s），其中 faults 标签 11 项（37.43 s），覆盖 factory 崩溃／挂起／取消／恢复、协议／跨位数会话、VST2 DSP 和状态故障。x86/x64 VST3 处理、50 次编辑器生命周期、状态／ParamID 和双实例隔离由可控 fixture 自动验证。S7 未修改生产宿主，复用此结果。

人工听音、商业许可证激活／人工审核、DPI／焦点／真实编辑器体验、所有商业类的人工兼容认证：SKIPPED_MANUAL。其余真实类只完成目录验证，不能写成全部实例／DSP PASS。SDK 检查出现 Waves 调用 WMIC 不存在的提示，但目录集合完整一致；不安装或修改系统组件。

日志：`build/vst-s7/waves-*-scan.tsv`、`waves-*-sdk.tsv`、`waves-*-probe.log`；完整故障日志 `build/vst-s5/final-scoped-tests-6.log`。生成的本机日志和商业插件不进入 Git 或 ZIP。
