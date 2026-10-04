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

## S8：Windows Release 与最终 ZIP

2026-10-05 完成，状态 PASS_WITH_MANUAL_SKIPS。版本 `1.3.0-alpha.2.35+8099048`，生产源提交 `8099048cc`。S8 的改动为打包脚本及记录，没有新增生产加载路径。

| 检查 | 结果与证据 |
| --- | --- |
| 根 Windows Release | PASS：`build/vst-s8/configure-release-2.log`、`release-build-1.log`。x64 DAW + x86/x64 helpers；Qt 6.10.3，MSVC 14.51，Windows SDK 10.0.26100。 |
| 全量回归 | PASS：64/64，180.92 s；faults 11 项、42.30 s；`release-tests-1.log`。 |
| 安装与依赖 | PASS：`install-1.log`、`package-release-final.log`；182 个 PE、Release DLL 闭包与正确架构，x86/x64 VST2/VST3 四个 helpers 齐备。 |
| 最终 ZIP 审计 | PASS：CRC、182 个二进制 SHA256、helper、许可检查；`zip-extraction-audit.log`。无商业插件、SDK 源码、fixture、测试工具、PDB／LIB／Debug CRT。 |
| 解压后启动 | PASS：仅 Windows 系统 PATH，清空 Qt / LMMS 开发环境变量；`extracted-clean-path-version.log`。无 SDK／开发依赖搜索路径。 |
| VeSTige x64/x86 | PASS：真实 MIDI note 渲染各 176384 帧、44100 Hz、16-bit stereo；峰值 16254、非零样本 88200。 |
| VstEffect x64/x86 | PASS：相同输入，峰值 6035、RMS 增益约 0.25；确定性 fixture 同时验证实际效果处理和两种 helper。 |
| WaveShell 16.7 Element | PASS：VeSTige 实际渲染峰值 7212、98391 个非零样本；最终解压包验证。 |
| WaveShell 16.7 Q10 | PASS：VstEffect 主音频渲染为非零 stereo；`clean-path-effect-render-q10.log`。不将这一项等同于全部 EQ 参数／商业认证通过。 |

解压包的音频内容断言：`build/vst-s8/extracted-render-energy.log`、`extracted-render-energy.json`，对应 `extracted-*.log`。自动检查比较音频样本，不能由渲染命令退出码代替。

Electric88 可能需要配套采样库；按照用户指示排除其验收，不列为未完成项。商业插件及配套资源由用户本机安装，包内不分发采样库。所有 725 类已验证目录集合，实际音频验证只针对所列类和可控 fixture；没有声称全部商业类认证通过。人工听音、商业激活／认证、主观 UI／DPI／焦点审核为 SKIPPED_MANUAL。

本次打包修正：使用匹配 Release 的 FFTW／FluidSynth／Lilv 及其依赖，给两个架构的 helpers 部署对应 CRT，补齐原有 DummyCarla DLL。Qt 使用 Windows 提供的 ICU shim；排除 CMake 错误拾取的 SDK ICU DLL，否则启动缺入口。早期失败日志保留，最终解压包已通过实际启动验证。DummyCarla 是基线可选后端，不表示实施已删除的 S6；SUIL、SID／SWH 所需 Perl、GIG 所需 libgig 未安装是已有可选功能限制，不影响这两个 VST 入口。

打包脚本：`buildtools/package_windows_vst.py`。可复现调用参数：stage=`build/vst-s8/lmms-windows-vst3-8099048`，vcpkg=`build/vcpkg_installed/x64-windows`，qt=`C:/Qt/6.10.3/msvc2022_64`，plugins=`build/plugins/Release`，crt=`D:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC/14.51.36231`，sdk-license=`vst3sdk/LICENSE.txt`，version=`1.3.0-alpha.2.35+8099048`，archive 使用新的 ZIP 路径。所有构建／测试／打包前台输出并写根 build.log，随即检查退出码并归档阶段日志。

最终发布 ZIP：`build/vst-s8/lmms-1.3.0-alpha.2.35-vst3-win64-release.zip`。
SHA256：`db45f3325f427ba49cd11622f6911c4121df0d30304fa94c940f59dc34dbd970`。
旁置校验文件：同名 `.zip.sha256`。ZIP 包含 `WINDOWS-VST3-RELEASE.md`、`windows-runtime-manifest.json`、LMMS 资源及依赖许可。只验证 Windows；其它平台和 x86 DAW 未纳入交付。
