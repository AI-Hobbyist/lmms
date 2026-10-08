# DiffSinger A4 验收

状态：PASS。范围是最终 Release 部署、独立 SDK、完整 SVS 回归，以及用户明确补充的通用音域、音素语言前缀、拼音、波形与 Song Editor 默认颜色。B0–B4 未开始。

## 交付与显示

- 主程序：`build/Release/lmms.exe`；普通插件及支持库仍在 `build/Release/plugins`。DiffSinger 成品是完整的 `build/Release/svs/SVSDiffSinger` 文件夹，包含匹配的 ORT CPU 1.23.0、冻结发音数据、manifest 和第三方许可。文件清单及 SHA256 见 `validation/A4-release-package.json`。
- SDK 使用既有 `build/svs-sdk-external` / `build/svs-sdk-install` 构建安装。无 LMMS / Qt 内部头依赖；安装内容增加可独立消费的 DiffSinger 源码示例。协议文档补充实际 CPU 使用、缓存、读音、音域元数据及输入/只读反馈参数分组 ID 规则；Conformance 相应按每个参数数组检查 ID 唯一性。
- `metadata.pitchRanges` 是所有 SVS 引擎均可提供的可选声明，不按 AI / 传统或插件 ID 分支。DiffSinger 从授权声库根的有界 `comfort.json` 读取 `available` / `comfort` / `weak`。支持标准 A–G 音名、升降号、八度、单音及范围；弱点优先于舒适音域。非标准 `7` 保留原文并提示，未猜测含义。缺少或坏的可选文件不阻止声库目录发现。
- SVS 全局 **Show voicebank pitch ranges** 默认开启，控制琴键明暗及侧栏文字；关闭或没有声明时使用默认琴键。配置保存后可重开，即时显隐不使音频失效。
- DiffSinger 全局 **Show phoneme language prefixes** 默认开启，仅隐藏已识别语言的标签前缀。实际音素、手工覆盖、歌词、音频对象及缓存身份保持不变。原来的渲染步数与声库目录仍可配置。
- 实际中文声库反馈读音 `ni / hao` 显示在音符上方，内部歌词保留 `你 / 好`。不把歌词当作拼音，不修改原歌词数据。
- Song Editor 中 SVS 默认片段为主题蓝色 `#3C6DA6`，沿用已有片段/轨道颜色、选中及静音规则，原来的自定义颜色仍可使用。

## 验证

全部必需自动验收已通过。GUI 均使用 Windows Qt 正常后端、真实可见窗口与 `QScreen::grabWindow`，没有 offscreen 路径。A3 已完成六包 CPU 全链与音频/反馈/tempo/分块/导出/工程重开验收，本阶段完整回归已复验宿主路径。

## 范围内限制与后续项

主观听感仍为 MANUAL/PENDING，六份真实 CPU SHA256 试听保存在工作区 `cache/SVS/DiffSinger`，不提交私人声库、图片或试听音频。A3 只读参考曲线原生截图仍在 `validation/A3-native-reference-*.png`。

与本阶段无关的现有 CarlaRack 加载警告及 Qt6 `--geometry` 命令行解析问题记录为后续项，不在此修改；Release 验收使用普通启动方式。未修改 DirectML 占位行为，也没有实施 B 阶段。
- 音符下方波形以 2px 线宽及更高包络绘制；全局 Show translucent background waveform 默认关闭，开启时在 Song Editor 多轨 SVS 片段内半透明垫底，钢琴窗不显示背景波形，所有引擎通用，不改音频及缓存。

### 自动结果

| 验收 | 结果与证据 |
| --- | --- |
| 最终 Release 构建 | PASS，`validation/A4-final-release-build.log`；用户新增波形项重编主程序及测试 PASS，`validation/A4-waveform-build.log` |
| 普通插件原位部署 | PASS，完整构建输出的 58 个 DLL 均在既有 `build/Release/plugins`，支持库一并更新；禁用 SID 旧 DLL 已原位保留为 `.dll.disabled`；路径、主程序 SHA256 见 `validation/A4-release-deployment.json` |
| 独立 SDK 构建/安装 | PASS，`validation/A4-independent-sdk-build.log` / `A4-independent-sdk-install.log`；Conformance 更新工具重编及安装通过 |
| 安装后的 SDK full/minimal | PASS，分别 ABI 1.3 / 1.0，有限非静音 PCM、非零 origin、结果释放及取消；`validation/A4-sdk-full-conformance.log` / `A4-sdk-minimal-conformance.log` |
| 安装后的 DiffSinger ABI | PASS，空目录 ABI 1.0–1.3、实际六包与三项设置、资源生命周期/边界；`validation/A4-sdk-empty-ABI.log` / `A4-sdk-six-ABI.log` |
| 原生目录与 comfort 可选文件 | PASS，缺失/合法/损坏 comfort fixture、递归/JSON/Unicode/ID/移动/冲突/权限/循环、六包重扫且 predictor/vocoder 无误报；`validation/A4-native-catalog.log` |
| 完整既有 SVS 回归 | PASS，68 passed / 0 failed / 2 skipped；两项新增 GUI slot 需显式嵌入窗口及 Release 路径，在下面独立运行；`validation/A4-full-SVS-QtTest.txt` |
| 新增真实窗口验收及波形回归 | PASS，5 passed / 0 failed / 0 skipped；`validation/A4-presentation-QtTest.txt`。验证拼音/原歌词、语言前缀保存重开、通用音域开关、缺失声明默认、背景波形默认关闭/开关/重开、音频对象及缓存键不变、钢琴窗不受背景开关影响、加粗波形/缩放回归 |

真实 Windows 窗口截图中中文正常、没有方框。`A4-native-ranges-pinyin-prefix-{on,off}.png` 同时显示上方拼音、内部歌词、2px 波形（音符底部下方 2px 起绘）、琴键明暗、侧栏音域和三条只读参考曲线。`A4-native-settings.png` 显示通用选项及 DiffSinger 三项设置；`A4-native-ranges-hidden.png` 与 `A4-native-no-ranges.png` 验证关闭/缺失声明的默认表现。Song Editor 的蓝色片段和半透明背景波形见本机 `A4-native-release-main.png` / `A4-native-background-waveform.png`，这两份含私人声库头像，只供用户本地验收，不提交到 Git。

完整套件必须按原有标准测试模式运行；强制整套启用 `SVS_EMBEDDED_GUI_TEST=1` 会在既有删除轨道导出用例中出现窗口队列回调崩溃。已缩小到测试运行模式，记录 `A4-full-embedded-reproduction.*` 供后续处理，本阶段未改无关轨道/窗口生命周期代码。标准完整套件通过，新增 GUI 用例以真实窗口单独通过，未使用 offscreen 规避。

Release 独立启动验证 PASS（3 passed / 0 failed，`validation/A4-release-window-QtTest.txt`）：实际开发版打开保存工程，Song Editor 蓝色 SVS 片段可见；进程模块路径核对 DiffSinger 与 ORT 均来自最终部署文件夹。随后用空声库目录配置启动同一主程序，未配置 refs/声库也正常启动并退出，截图 `validation/A4-native-empty-release.png`。测试工程明确保存可见 Song Editor 状态，不依赖跨进程快捷键焦点；所有窗口正常关闭。
