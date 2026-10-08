param([string]$Destination = (Join-Path $PSScriptRoot '../build/_deps/onnxruntime-win-x64-1.23.0'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Enter-LmmsEnvironment.ps1')
$sdkDestination = [IO.Path]::GetFullPath($Destination)
$sdkArchive = "$sdkDestination.zip"
$expectedHash = '72c23470310ec79a7d42d27fe9d257e6c98540c73fa5a1db1f67f538c6c16f2f'
if (-not (Test-Path -LiteralPath $sdkArchive)) {
    New-Item -ItemType Directory -Path (Split-Path $sdkArchive) -Force | Out-Null
    Write-Output 'Downloading official ONNX Runtime CPU 1.23.0 Windows x64 SDK'
    Invoke-WebRequest 'https://github.com/microsoft/onnxruntime/releases/download/v1.23.0/onnxruntime-win-x64-1.23.0.zip' -OutFile $sdkArchive
}
if ((Get-FileHash -LiteralPath $sdkArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'ONNX Runtime SDK archive hash mismatch; refusing extraction'
}
if (-not (Test-Path -LiteralPath (Join-Path $sdkDestination 'VERSION_NUMBER'))) {
    Expand-Archive -LiteralPath $sdkArchive -DestinationPath (Split-Path $sdkDestination) -Force
}
if ((Get-Content -LiteralPath (Join-Path $sdkDestination 'VERSION_NUMBER') -Raw).Trim() -ne '1.23.0') {
    throw 'Unexpected ONNX Runtime SDK version'
}
[Environment]::SetEnvironmentVariable('LMMS_ONNX_ROOT', $sdkDestination, 'User')
$env:LMMS_ONNX_ROOT = $sdkDestination
Write-Output "PASS native CPU SDK 1.23.0 SHA256=$expectedHash LMMS_ONNX_ROOT=$sdkDestination"
