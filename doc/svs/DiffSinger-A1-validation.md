# DiffSinger A1：外部声库目录与资源

本阶段范围为递归扫描、配置元数据、稳定身份、目录设置/刷新和图片资源。歌词音素化与真实 duration 推理留在 A2，完整 CPU 合成留在 A3。没有以示例波形代替声库模型输出。

## 实现

- 原生 JSON/YAML 配置及逐字段角色 JSON > YAML/YML > TXT 合并，保留未知字段/来源，冲突诊断；TXT 支持 UTF-8/BOM、UTF-16 BOM及显式传统编码，保留值内等号。阶段配置和模型 ID 独立；`comfort.json` 保留并报告不支持，不能作为主配置执行。
- 声库递归扫描具有授权路径、canonical 去重、目录循环/权限/坏配置隔离和固定根优先级。包内 vocoder 优先，显式共享 Vocoders 次之；校验完整 mel 规格。predictor/vocoder 不作为 acoustic 声库。
- 明确包 ID 或宿主持久安装 ID；同路径更新保持 ID，唯一内容匹配保留移动身份，相同内容副本去重，同 ID 内容冲突诊断，歧义移动拒绝自动绑定。模型、词典、配置及外部数据内容指纹不包含图片字节。
- 每声库可选语言直接来自声库能力，保留语言映射 ID 和声明顺序；`use_lang_id=false` 不删除语言。后续字典按用户指定 `tlds_ref` 优先级实现。
- SDK ABI 1.3 可选尾部 `query_catalog`、C++包装和 conformance 空目录路径。已安装引擎与声库列表分离，空目录仍能配置。引擎设置恰好两项：渲染步数 1–100/default20、多个声库目录；添加/移除/保存重开、Apply、Rescan沿用现有设置页。
- 不可变 catalog 和资源句柄；图片 ID/MIME/大小/SHA256通过资源 API，旧打开资源跨重扫可读。只更新 SVS 浏览器/已有选择器与绑定引擎轨道，缺失声库保留工程 ID、歌词和手工/未知数据。

## 验收证据

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 原生 Unicode/嵌套/重叠根/JSON映射/YAML+TXT/移动/重复/冲突/资源/ACL权限/目录junction循环/坏配置 | PASS | `validation/A1-native-catalog.log`；fixture模型仅作解析，不作为推理证据 |
| 六个真实嵌套声库与全部阶段/四语言/两张图片；重扫身份稳定、predictor/vocoder零误报 | PASS | 同上 |
| 开发插件 ABI 1.0–1.3、六包 catalog、两项设置、资源句柄跨重扫及边界 | PASS | `validation/A1-native-abi.log` |
| 独立 SDK 原目录构建、ABI、空引擎 conformance | PASS | `A1-independent-sdk-build.log`、`A1-independent-sdk-abi.log`、`A1-empty-catalog-conformance.log` |
| 旧 full/minimal 示例音频/取消/所有权/非零原点 | PASS | `validation/A1-example-conformance.log` |
| 既有参数焦点、图片设置、词典能力、参数持久化 | PASS | `validation/A1-existing-regression-QtTest.txt`：6 passed、0 failed |
| 实窗目录添加、保存重开、移除、六包刷新、图片、四语言及工程数据保留 | PASS | `validation/A1-native-settings-QtTest.txt`：3 passed、0 failed；原生 Windows、开发主题 |

真实设置页截图为 `validation/A1-native-settings.png`。实际声库选择器/立绘截图保存在本地 `validation/A1-native-voice-images.png`，已检查中文“遐蝶”“测试”正常、无方框，图片显示正常；由于含外部声库美术资源，该截图不提交、不打包。声库模型/字典/图片均不纳入交付。

## 部署与诊断

开发程序路径保持 `D:/UserData/Desktop/Project/lmms/build/Release/lmms.exe`；新包直接写入 `build/Release/svs/SVSDiffSinger`，普通插件仍为 `build/Release/plugins`。本阶段未搬迁或禁用普通插件。部署大小/哈希和本地图片验收哈希见 `validation/A1-deployed-manifest.json`。

首次宿主链接失败为新增 Registry 的 moc 在 lmmsobjs/lmmssvs 重复生成；沿原 SVS 头文件排除规则补充 Model 修复。首次实窗启动因 Windows 系统 `onnxruntime.dll` 1.17.1 与冻结的 1.23.0 同名而失败；插件改为显式加载同包 ORT 并绑定 C++ API 表，先检查版本再创建对象。复验运行中的 GUI 进程模块为包内 1.23.0，实窗测试通过。失败日志保留在本阶段 validation 文件。

所有构建/测试采用前台 PowerShell、共享环境脚本、Tee build.log、即时退出码检查；没有使用 offscreen。实窗测试正常关闭。既有 GUI harness 对普通 instrument DLL 的加载警告保持在日志中，未将其视为本阶段普通插件验收；完整现有 SVS/Release 回归属于 A4。

完成规定自动验收后检查 git status/diff，只提交 A1 文件并推送当前分支；确认远端成功后才开始 A2。原工作区 Song/VST 修改和其它未跟踪文件不纳入提交。
