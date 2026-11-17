# 内置 Agent 聊天框交付说明（M5）

更新：2026-11-17。计划书：`内置Agent聊天框实施计划书.md`；逐步实施记录：`doc/agent/chat-implementation-progress.md`。

## 增量包

| 项目 | 值 |
| --- | --- |
| 路径 | `D:\UserData\Desktop\Project\lmms\build\packages\lmms-agent-chat-20261117-213718-win64.zip` |
| 大小 | 6,733,415 字节 |
| ZIP SHA256 | `C62B896C4DA10E72E236B8F912D976C0472B4F40BA232D1938F8333C9F7E7A33` |
| 生成脚本 | `buildtools/New-AgentChatPackage.ps1`（日志 `build/agent-chat-m5-package.log`） |
| 基线 | 实际安装 `D:\Program Files\LMMS` 的 367 个二进制 SHA256 |

包内文件按安装目录相对路径存放，共 41 个：`lmms.exe`（替换）、`data/locale/zh_CN.qm`（替换）、`data/themes/default/style.css`（替换，仅在末尾新增聊天样式段）、31 个 `data/themes/default/agent_chat_*.svg`（新增）、`data/system_prompt.txt`（新增，保留用户文件）、`tools/` 下 aria2 与 7-Zip 共 6 个文件（新增）。不删除任何文件；与本功能无关的主题差异（`ui-*.svg`）不打包。

## 安装（手动替换，会话未写入生产目录）

1. 关闭 LMMS，备份安装目录中的 `lmms.exe`、`data\locale\zh_CN.qm`、`data\themes\default\style.css`。
2. 把包内 `lmms.exe`、`data`、`tools` 按相同相对路径复制到安装目录。
3. `data\system_prompt.txt` 若已存在且改过，保留你的文件；包内为新默认内容，可另存为 `.default` 后比对选用。
4. 可选：管理员运行包内 `Install-Replace.ps1 -TargetDirectory <安装目录>`，先核对 SHA256 再复制；已有 `system_prompt.txt` 不覆盖（新默认写为 `.default`）；加 `-VerifyOnly` 只核验。
5. 回滚：恢复第 1 步备份；新增的 `tools`、`agent_chat_*.svg`、`system_prompt.txt` 可删除或保留。

## 配置要点

- 设置 → API 与模型：选择预设并填写 Key（存于 Windows 凭据管理器）。
- 批准模式默认“请求批准”。
- 全局设置：联网搜索默认关闭；来源默认 SearXNG（填写自部署实例 URL，可为局域网；实例需在 `settings.yml` 的 `search.formats` 启用 `json`），可选 Brave（API Key）或模型自带搜索（需在连接高级设置 `capabilities` 中声明 `"hostedWebSearch": true`，仅 OpenAI Responses / Anthropic Messages / Gemini）。
- 素材下载：工具默认 `tools/aria2/aria2c.exe`、`tools/7zip/7z.exe`（相对 lmms.exe），“检查工具”按 `tools/manifest.json` 复核 SHA256。

## 工具来源与许可

| 工具 | 版本 | 来源 | SHA256 | 许可 |
| --- | --- | --- | --- | --- |
| aria2 | 1.37.0 | github.com/aria2/aria2 release `aria2-1.37.0-win-64bit-build1.zip` | 归档 `67d01530…b288`，aria2c.exe `be2099c2…70c2` | GPL-2.0-or-later（COPYING） |
| 7-Zip | 26.06 | github.com/ip7z/7zip `7z2606-x64.exe`（由官方 7zr.exe 解包，未运行安装程序） | 安装包 `9b697f65…42ec`，7z.exe `dc049342…ef76`，7z.dll `500db9eb…364e` | LGPL-2.1-or-later + unRAR 限制（License.txt） |

完整值见包内及 `build/Release/tools/manifest.json`；获取脚本 `buildtools/Get-AgentChatTools.ps1`。

## ABI 与依赖

- `lmms.exe` 导出：基线 3120，新 3138，删除 0（插件按名称导入，均仍满足）。
- 新 `lmms.exe` 与包内工具的全部依赖在包内、安装目录或 Windows 系统目录中可解析（Qt6Network 等已在安装目录）。
- 未改变插件 ABI，不替换或重建插件 DLL。

## 验收矩阵

| 范围 | 自动验证 | 真实环境 | 人工 |
| --- | --- | --- | --- |
| M1 界面与本地数据 | PASS（ChatDockTest 等，真实窗口） | — | MANUAL/PENDING |
| M2 协议与附件 | PASS（8 协议夹具、本机 HTTP） | 逐厂商实测 MANUAL/PENDING | — |
| M3 内部工具与批准 | PASS（CTest 19/19，含 MCP 回归） | 真实厂商工具调用 MANUAL/PENDING | MANUAL/PENDING |
| M4 搜索/读取/托管搜索 | PASS（本机 HTTP 夹具） | 真实 SearXNG/Brave/厂商托管搜索 MANUAL/PENDING | — |
| M4 下载/解压/工具清单 | PASS（真实 aria2c 1.37.0、7-Zip 26.06，本机 HTTP） | 公网素材下载 MANUAL/PENDING | — |
| M5 包与安装 | PASS（导出/依赖核验、安装器 VerifyOnly 41 文件） | 生产替换由用户手动执行 | 替换后整机 GUI 验收 MANUAL/PENDING |

最终回归：CTest 19/19（11 Chat、5 MCP、CommandBus/CoreCommands/SvsCommands），日志 `build/agent-chat-m4-regression-{build,tests}.log`；VerifyOnly 日志 `build/agent-chat-m5-verifyonly.log`。按用户要求，本阶段未启动应用做整机 GUI 测试。

## 更新：侧栏默认收起（2026-11-17，提交 5fa76001a）

首次启动时 Agent 侧栏默认收起为 48 px 标签，用户展开或收起后按保存的状态恢复。此时生产目录已由用户手动替换为上一版包（`lmms-agent-chat-20261117-212914-win64.zip`），因此新包按实际安装比对后只含 `lmms.exe` 1 个文件：`build/packages/lmms-agent-chat-20261117-213718-win64.zip`，SHA256 `C62B896C4DA10E72E236B8F912D976C0472B4F40BA232D1938F8333C9F7E7A33`；导出 3138→3138 删除 0，依赖可解析，VerifyOnly 通过且无写入。替换方法：关闭 LMMS，备份并覆盖安装目录中的 `lmms.exe`。
