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


## S8 后实机修复：当前 Windows 交付验收（2026-10-05）

状态 **PASS_WITH_MANUAL_SKIPS**；本节优先于前述历史 S8 交付。

2026-10-05：S8 后 Windows 发布版实机问题修复。交付范围继续以计划 §10 为准：VeSTige 是 VST2/VST3 Instrument 唯一官方入口，VstEffect 是 VST2/VST3 Effect 唯一官方入口；保留多路径扫描、WaveShell/multi-class、Instrument/Effect 分类及原 Sidebar/Effect Browser 元数据选择。S6、第三种加载/宿主/生命周期、全局 Mixer/PDC、侧链及高级路由仍为 REMOVED_BY_USER，没有恢复这些扩张。

实机反馈暴露了原 S8 离线验收的覆盖缺口：离线渲染通过并不证明 SDL 连续音频和原生编辑器稳定。修复回调连续到达时结果尚未就绪、已准入回调内嵌套工作被控制屏障拒绝、编辑器打开/隐藏缺少现有 Pause/Resume 屏障。生产结果等待上限 100 ms；默认独立实时会话仍为 0 ms，真正挂起的 DSP 仍故障退出。修复 Kontakt 原生多总线布局与元数据容量、attach 前不提供尺寸的编辑器兼容：共享音频总通道容量 128（单总线仍最多 32），IPC ProtocolVersion=3；实际两个入口仍只映射既有主 stereo，未新增端口/路由界面。主程序与四个 helpers 必须作为同一套更新。

Kontakt 8.12.1 初次加载计时 4645/5191 ms，空采样器峰值 0 不判失败。随后安装版实机点击 Tools 标签连续两次令 helper 退出，日志为 error 12 / stage 4 / native 0；确认 helper 主线程窗口消息处理阻塞 DSP。现在同一个受监督 VST3 helper 内由专用线程处理 DSP，控制修改仍用互斥及原有 Pause/Resume 排空屏障，GUI 消息保留主线程。实机 Tools、Loops、Instruments 切换和持续播放/编辑器重开均通过，同一 helper PID 11524 存活，无音频故障日志。没有采样库的静音不表示失败；Kontakt 和 Electric88 可能需要配套采样库，不列作未完成。

新增可控原生 GUI handler 阻塞 350 ms 的回归：旧 helper 在连续实时音频请求失败（gui-slow-handler-regression-red.log）；修复版两种 ABI 在 GUI 仍忙时完成连续音频，输出为 0.625，不等待 GUI 返回。该测试属于现有原生编辑器生命周期套件，没有新增生产生命周期。

人工听音、商业授权/认证、主观 UI/DPI/焦点审核按指示 SKIPPED_MANUAL；本机可自动观察的加载、UI 操作、存活、音频数据与清理已测试，不声称所有商业类全面认证。此前直接发 NoteOn 的 Element 停止 transport 场景失败日志保留；最终商业乐器验收采用实际 SongPlayback，不把先前失败解释成已经证实的“仅错过瞬时峰值”。

| 检查 | 当前结果与证据（均在 build/vst-live-fix） |
| --- | --- |
| 全量 Release | PASS，`gui-thread-release-build.log`；生产源 d3508bbbad9d7e3a87f4f4168fe19350b25bed15。 |
| 全量回归 | 65/65 PASS，201.96 s；faults 11 项、43.41 s；`gui-thread-final-tests-2.log`。 |
| 原生编辑器/慢 GUI 回归 | x64/cross-ABI 各 50 生命周期 PASS（6.84/6.79 s）；350 ms GUI 忙时连续实时音频仍为 0.625，旧 helper 的相同测试在第 58 行失败；`gui-slow-handler-regression-red.log`。 |
| 官方入口实机音频矩阵 | 8 种 VST2/VST3 × Instrument/Effect × x86/x64 PASS（15.22 s），实际 SDL 256 帧、可见编辑器、重开、同 PID、非零音频与清理；`gui-thread-realtime-results.txt`。 |
| WaveShell Element / Q10 | 最终二进制 4/4 PASS（11.267 s），实际 SongPlayback/效果处理峰值 0.207691/1.16406；同 PID、编辑器重开 PASS；`waves-gui-thread-final.txt`。此前三次 SongPlayback PASS 日志 waves-song-1/2/3 仍保留。 |
| Kontakt 8.12.1 | 最终二进制 3/3 PASS（10.429 s），加载 4164 ms，峰值 0，显式 EmptySampler 接受；同 PID 与重开 PASS；`kontakt-gui-thread-final.txt`。此前 4645/5191 ms 记录为历史测量。 |
| 安装版 Kontakt GUI | Tools 标签原来连续两次令 helper 退出：error 12/stage 4/native 0；新 helper Tools/Loops/Instruments 操作、持续播放/重开通过，PID 11524 保持存活；`kontakt-installed-ui-diagnostic.log` / `kontakt-installed-ui-thread-green-1.log`。测试工程手工空 fingerprint/state 属性曾触发合法拒绝，已修正本机测试文件，不是插件静音失败。 |
| ZIP / 依赖 | `gui-thread-final-install.log` / `gui-thread-final-package.log` / `gui-thread-final-zip-audit-2.log`：184 个 PE、x86/x64 四 helpers、依赖/许可/CRC/逐文件 SHA256 PASS。增加 plugins 与 plugins/32 各自的 msvcp140_atomic_wait.dll。首次 ZIP 校验使用大小写敏感路径导致 KeyError；按 Windows 路径规则重验通过，旧日志保留。 |
| 已安装文件 | `installed-gui-thread-final-update.json` DEPLOYED，本轮最终替换 5 文件，核对全部 184 PE；此前整体替换 12 文件见 `installed-final-update.json`。均在主程序/helpers 关闭时更新，配置替换期间保持原样，原始备份不覆盖。 |
| 干净 PATH | 安装版只用 Windows 系统 PATH 的 --version PASS；`installed-gui-thread-clean-path-version.log`。 |
| computer use | 测试主程序与 helpers 关闭，computer-use REPL 已重置结束；未卸载技能/插件。 |

曾错误地对所有 CTest 设置 LMMS_VST_LIVE_GUI，导致离线 VstEntryPoints 的 nativeBundleLocator 在不支持的 GUI 模式崩溃；该 64/65 失败日志 `gui-thread-final-tests.log` 保留。移除全局开关，让专属 VstEntryPointsRealtime 使用其既定 GUI 配置，最终全套 65/65 PASS；未以跳过测试掩盖失败。此前 `final-sampler-tests.log` 的 65/65（249.56 s）属于 attach/capacity 修复候选，不能证明后续 Tools 标签稳定性。

当前 ZIP：`build/vst-live-fix/lmms-1.3.0-alpha.2.36-vst3-kontakt-ui-fix-win64.zip`；SHA256：`54abb7710b3fbb5c2ac9016c4678e71ac6440e2546682f2f321b00a28632935d`。显示 `1.3.0-alpha.2.36+9682079`，配置时间早于修复提交；生产源 d3508bbbad9d7e3a87f4f4168fe19350b25bed15，包含 add227a61 / 14542d852。旧 alpha.2.35、20 ms realtime-fix、attach-only final-fix 包均已被本包取代。包内不含商业插件、采样库、SDK 源码或测试工具。
