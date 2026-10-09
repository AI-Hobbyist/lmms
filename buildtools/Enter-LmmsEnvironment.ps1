# Refresh build variables in shells inherited from an already running application.
$buildVariableNames = @(
    'QT_ROOT', 'QTDIR', 'Qt6_DIR', 'WINDOWS_SDK_ROOT', 'WindowsSdkDir',
    'WindowsSDKVersion', 'SVSSDK_ROOT', 'SVSSDK_SOURCE', 'SVSSDK_INCLUDE_DIR',
    'VCPKG_ROOT', 'CMAKE_PREFIX_PATH', 'LMMS_CMAKE_COMMAND', 'LMMS_CTEST_COMMAND',
    'LMMS_CMAKE_GENERATOR', 'LMMS_CMAKE_PLATFORM', 'LMMS_VSDEVCMD',
    'LMMS_CMAKE_TOOLCHAIN_FILE', 'LMMS_BUILD_TOOLS_PATH', 'LMMS_ENV_SCRIPT',
    'LMMS_ONNX_ROOT', 'LMMS_ONNX_DML_ROOT', 'LMMS_DIRECTML_ROOT'
)
foreach ($buildVariableName in $buildVariableNames) {
    $buildVariableValue = [Environment]::GetEnvironmentVariable($buildVariableName, 'User')
    if ([string]::IsNullOrEmpty($buildVariableValue)) {
        $buildVariableValue = [Environment]::GetEnvironmentVariable($buildVariableName, 'Machine')
    }
    if (-not [string]::IsNullOrEmpty($buildVariableValue)) {
        [Environment]::SetEnvironmentVariable($buildVariableName, $buildVariableValue, 'Process')
    }
}
if ($env:LMMS_BUILD_TOOLS_PATH) {
    $buildToolDirectories = @($env:LMMS_BUILD_TOOLS_PATH -split ';' | Where-Object { $_ })
    $buildOtherDirectories = @($env:PATH -split ';' | Where-Object { $_ -and $_ -notin $buildToolDirectories })
    $env:PATH = ($buildToolDirectories + $buildOtherDirectories) -join ';'
}
