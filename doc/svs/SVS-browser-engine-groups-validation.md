# SVS 插件浏览器引擎分类

状态：PASS。

按用户要求，左侧 Singing Voice Synthesis 采用“引擎名称 → 声库”子分类；没有声库的引擎不显示。设置 → SVS 中的引擎配置和扫描入口保持可用。扫描完成后原列表自动刷新，声库拖放和右键创建 SVS 轨道仍使用原来的插件/声库标识。

搜索支持引擎名称和声库名称，匹配声库时显示其父分类，匹配引擎时显示该引擎声库。修改仅涉及 PluginBrowser 和对应验证。

验证：既有 build/Release/lmms.exe 原位编译；SVSIntegrationTest svsBrowserEngineGroups 标准模式及原生 Windows GUI 模式均为 3 passed / 0 failed / 0 skipped。测试检查空 DiffSinger 分类隐藏、扫描后的六个声库归属、引擎/声库搜索和无匹配过滤。原生 GUI 使用已有 Release 主题；真实窗口截图 SVS-browser-engine-groups.png 已人工检查，中文正常显示。该截图含私人声库头像，仅本地提供，不提交仓库。

证据：validation/SVS-browser-engine-groups-QtTest.txt、validation/SVS-browser-engine-groups-native-QtTest.txt。

用户已确认最初的“DiffSinger 不出现”在扫描声库后解决，本次只实现明确要求的分类，不改变加载器或依赖部署。

更新覆盖包：build/packages/lmms-enhanced-full-3f637c6f4-win64.zip，产品提交 3f637c6f4ead1df96266fcecddfa8c5e1a94fc85，版本 1.3.0-alpha.2.88+3f637c6。包含 3,484 个运行文件和 4 个安装/说明条目，共 66,026,243 bytes；全条目 SHA256、360 个 PE 位数、官方默认模板和私人数据排除检查通过。ZIP SHA256：8C4EBFEDD01780F73A84706CDC0ED54F774BBD4C14DE8C5C23A65018E0B4E2C7。证据 validation/SVS-browser-engine-groups-package.json。
