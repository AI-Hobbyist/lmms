# 完整替换包

2026-10-10，产品源码提交 `1ab671a64cc0a436b7c9d6c3cab9fceb9f0f4124`。

- 包：`build/packages/lmms-enhanced-full-1ab671a64-win64.zip`。
- 大小：113,959,211 字节；6847 个运行文件及 4 个安装包条目。
- SHA256：`5B96E1A2CA7F87C6E1D10EE6CECD15E1F725AAD4B7FEC2EB92F2AA1DC4A01A53`。
- 使用既有 `build/Release` 成品，未新增构建目录。包含 DiffSinger 文件夹及依赖、共享计算运行时、SVC 插件、工程转换运行时、主题和语言文件。缓存、用户配置及外部声库不打包。
- 生成脚本验证文件完整性、52 个常规插件、SVC 插件、默认模板及部署资源；没有同工程其他运行任务。
- 安装脚本 `-VerifyOnly` 校验通过：6847 个文件、6 个示例退役路径及安装路径；没有修改任何安装文件。

关闭 LMMS 后解压，运行包内 `Install-Replace.cmd`，选择 `D:\Program Files\LMMS`。该目录通常需要以管理员身份运行。脚本会校验、备份并替换对应文件。
