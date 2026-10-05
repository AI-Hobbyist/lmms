# 共享 Windows 构建环境（2026-10-05）

应用户要求，将当前已验证的 Qt 6.10.3 MSVC x64、Windows SDK 10.0.26100.0、公开 SVSSDK 安装位置、vcpkg 和 VS CMake 工具位置写入 Windows 当前用户的持久环境变量，并扩展 User PATH。当前进程非管理员，Machine 环境未修改；所有使用同一 Windows 账户的 agent 共用这些变量。

变量以 Windows 环境存储为准：QTDIR/QT_ROOT/Qt6_DIR、WindowsSdkDir/WindowsSDKVersion/WINDOWS_SDK_ROOT、SVSSDK_ROOT/SVSSDK_SOURCE/SVSSDK_INCLUDE_DIR、VCPKG_ROOT、CMAKE_PREFIX_PATH，以及 LMMS_CMAKE_COMMAND/LMMS_CTEST_COMMAND/LMMS_CMAKE_GENERATOR/LMMS_CMAKE_PLATFORM/LMMS_CMAKE_TOOLCHAIN_FILE/LMMS_VSDEVCMD。VS CMake 生成器自行初始化编译器；直接运行 cl 时仍通过 LMMS_VSDEVCMD 初始化对应 MSVC 环境。

用户原有 PATH 和 CMAKE_PREFIX_PATH 保留。原值备份在本机 build/lmms-environment-before.json；本机设置脚本不纳入源码。SVSSDK_ROOT 指向当前工作区 build/svs-sdk-final-install；删除该安装目录后需重新安装 SDK 并更新变量。

buildtools/Enter-LmmsEnvironment.ps1 只刷新已保存的构建变量和当前进程 PATH，不重写持久设置。用户 PowerShell all-hosts profile 已加入读取 LMMS_ENV_SCRIPT 的入口；新 PowerShell（包括后续 agent 工具会话）已实际自动读取新变量。已经运行的会话或 -NoProfile 会话可在项目根目录执行：

```powershell
. ./buildtools/Enter-LmmsEnvironment.ps1
& cmake --build build --config Release --parallel 4 2>&1 | Tee-Object -FilePath build.log -Encoding utf8
$buildExitCode=$LASTEXITCODE
if($buildExitCode -ne 0){Get-Content build.log -Tail 70;exit $buildExitCode}
```

工作区 AGENTS.md 已补编译前刷新入口和变量使用说明，保留现有前台日志/退出码规则。配置新构建目录时从环境取 LMMS_CMAKE_GENERATOR、LMMS_CMAKE_PLATFORM、LMMS_CMAKE_TOOLCHAIN_FILE；其它 LMMS 功能开关仍按项目要求选择，环境配置不替代构建验收。

自动证据：新工具会话中 Get-Command cmake 成功且 Process/User SDK 值相符；qmake -query QT_VERSION 输出6.10.3；仅从 SVSSDK_ROOT/LMMS_CMAKE_GENERATOR/LMMS_CMAKE_PLATFORM 和环境 CMAKE_PREFIX_PATH 配置并编译独立 C11 示例成功，CMake 自动选择 Windows SDK10.0.26100.0/MSVC19.51。日志 validation/Shared-build-environment-{setup,Qt,configure,build}.log。没有设置全局 INCLUDE/LIB 或全局 CMAKE_TOOLCHAIN_FILE，避免把其它项目强制绑定本仓库的工具链。
