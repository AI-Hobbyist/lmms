# F6 交付审计

交付状态：**IMPLEMENTED / MANUAL-PENDING**。人工验收按用户要求跳过，未计为 PASS；需要修改 PNG 的部分为用户批准的 `SKIPPED / PNG`。经典/现代主题切换仅登记未来计划，本轮没有新增切换入口。

## 第 12.1 节逐条核对

| 要求 | 结果与证据 |
|---|---|
| 默认 CSS 与引用资源可安装 | PASS。原目录 `build/Release/data/themes/default`；`F6-installed-resources.json` 记录 CSS 和 17 个 SVG 的源码/安装 SHA256，全部相同。实窗解析资源见 F6 各缩放目录的 standard-resources.json。 |
| A/B/C/D 实施、E 边界明确 | IMPLEMENTED，参见 inventory、F1–F5 检查点和 plugin-coverage。D04 的私有 artwork/嵌入标签为 SKIPPED / PNG；E 原生第三方内容不计现代化。 |
| 覆盖清单与引擎/插件例外 | inventory.md、plugin-coverage.md、F5-plugin-coverage.json：冻结 52 个目标，47 个有效面板双 DPI 实窗，5 个外部运行环境 MANUAL/PENDING。Sid/GigPlayer 当前未启用，不计编译成功。 |
| 视觉规格 | visual-spec.md 保留冻结颜色、字体、密度与状态；紧凑宿主框的实际半径另有实现映射，不将保留 artwork 描述为重绘。 |
| 构建、测试与实窗证据 | F6-build.log：主程序、测试与 52 个插件；F6-tests.log：核心 3/3 PASS，完整 SVS 59 PASS，ThemeWidgetTest 12 PASS；F6-theme-150-results.txt：安装资源下控件 12 PASS。 |
| 场景、行为、人工项、提交 | acceptance.md、下方截图索引及阶段提交表；模拟 DPI 与人工范围分别注明。 |
| 音频数据、参数与格式保持 | 本轮已提交差异未修改 src/core、sdk/svs 或插件 DSP/参数/预设实现；Carla 仅改输出目录。F6 双 DPI fixture 全新进程重开验证 MIDI/Sample/Automation/SVS/Mixer/效果/控制器；F5 45 个 instrument/effect 的显示前后及 restore/save 比较通过，Tool 预设 N/A。听感和未自动覆盖的完整输入体验仍 MANUAL/PENDING。 |
| 独立开发安装与资源依赖 | PASS。`cmake --install build --config Release --prefix build/Release` 完成；3250 条安装记录均在原开发前缀。实际程序使用独立临时 `--config`，不改个人配置；清除 LMMS_DATA_DIR/PLUGIN_DIR/SVS_PLUGIN_DIR 和 Qt 插件覆盖变量，PATH 只含该安装目录/plugins 与 Windows 系统目录，仍正常启动/关闭：F6-isolated-launch-results.txt 3 PASS。原 Program Files 安装未被操作。 |
| 阶段独立提交及推送 | F0–F5 与部署补修均已逐次推送 master，见 acceptance。F6 为本检查点提交；推送成功、远端 SHA 核对后才宣布交付。三个其他任务的未提交文件保留，不纳入本阶段。 |

## 安装目录与清单

- 程序：`build/Release/lmms.exe`，SHA256 见 validation/F6-delivery-manifest.json。
- 插件及支持库：`build/Release/plugins`，52 个目标的当前路径、时间、大小及 SHA256 见 F6-plugin-artifacts.json。与 F5 DLL 哈希一致，未另起部署目录。
- 默认主题和 STK 数据：`build/Release/data`；SVS 示例：`build/Release/svs/SVSExample`。头像/立绘使用安装的仓库 SVG，不依赖个人声库图片。
- Sid 残留保持原地 `sid.dll.disabled`，没有可加载的 sid.dll；GigPlayer DLL 缺席。完整安装日志、清单和路径核对分别为 F6-install.log、F6-install-manifest.txt、F6-install-audit.log。
- 工程 bundle 的 sample 仅引用 `resources/tone.wav`；临时配置由测试清理。所有测试窗口关闭，未采用离屏或 Computer Use 验收。

## 最终实窗矩阵与索引

| 范围 | 证据 | 结论 |
|---|---|---|
| 100% / 150%，S01 主窗 | F6-100 / F6-150：S01-main.png、S01-installed-executable.png | 产品控件场景与实际开发程序均显示；100% 独立 PATH 冒烟另有最终结果。 |
| S02 设置/导出 | 同目录：S02-general/audio/paths/vst/svs/export.png | 安装数据下显示正常，滑块轨道可见。 |
| S03 Song/Pattern | S03-song.png、S03-pattern.png | 多类型轨道、片段与步进显示；中文轨道名无方块字。 |
| S04 Piano/Automation | S04-piano.png、S04-automation.png | 网格、音符和节点；实际编辑数据契约见 ThemeWidgetTest。 |
| S05 SVS/设置 | S05-svs.png、S05-plugin.png | 音符坐标反变换断言；安装的示例引擎与 SVG，参数/只读分界保持。 |
| S06 Mixer/机架 | S06-mixer/controller/effect.png；150% F5-mixer-rack、F5-effect-controls | 通道、推子、卡片与控制面板；选通道、LED 和 Controls 交互有模型断言。 |
| S07 MDI/生命周期 | S07-mdi.png；ThemeWidgetTest 的 groupBoxAndDetach、legacyAndRepolish | 原生分离/回嵌/最大化/恢复/关闭及旧主题回退；人工拖动边缘缩放不计自动通过。 |
| S08 公共页/自有面板 | S08-instrument.png、S08-common-1..5.png；F5-100 / F5-150 的 47 个有效插件面板 | 公共长标签边界断言；完整逐项插件覆盖复用 F5，F6 产品及 DLL 未改动。 |
| 125% / 200% 有限抽查 | F6-125 / F6-200：standard-states-focus/hover/pressed、standard-menu、S02-scale-audio、S05-scale-spot-check/toolbar-end/parameters-end.png | 标准页和最拥挤 SVS 窗口；200% 工具栏横向滚动到 Properties，参数侧栏纵向滚动到 Mode/Gain。未展开全状态矩阵。 |

100%/150% 场景各 5 PASS；125%/200% 抽查各 4 PASS。各目录 environment.json 记录 Qt 6.10.3、原生 windows QPA、实际字体/窗口尺寸及 QT_SCALE_FACTOR。这些是**模拟缩放**；完整中文翻译界面/真实中文 IME、跨屏真实 DPI、听感、外部插件编辑器和个人观感按要求 MANUAL/PENDING。当前 UI 为英文，加入真实中文 fixture/标签；系统 locale 不等于已测试完整中文翻译。

## 遗留与回滚

PNG 范围例外、五个外部环境项及第三方原生内容详见 plugin-coverage；F0 同进程工程重新加载问题、间歇性既有 SVS 导出测试超时、Eq View 绑定时既有归一化详见 acceptance 的 follow-up。这些没有在 F6 扩大为音频/加载流程重构。本轮最终必须回归均通过，不能用历史失败轮充当 PASS。

回滚按各阶段 commit 执行 `git revert <阶段提交>`，先保留当前其他任务修改，再重编译主程序及启用插件、原地覆盖并复核相同部署目录。主题与对应 View 属性一起回退，避免 CSS 悬挂或再次混用插件 ABI；不删除用户工程、配置或使用强制清理。回滚未实际执行。
