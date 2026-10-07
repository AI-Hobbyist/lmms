param([string]$Stage = 'F0', [string]$Scale = '1', [switch]$PluginPanels, [switch]$Installed, [switch]$SpotCheck)
$ErrorActionPreference = 'Stop'
. $PSScriptRoot/Enter-LmmsEnvironment.ps1
$env:QT_QPA_PLATFORM = 'windows'
$env:LMMS_DATA_DIR = Join-Path $PWD $(if ($Installed) { 'build/Release/data' } else { 'data' })
$env:LMMS_PLUGIN_DIR = Join-Path $PWD 'build/Release/plugins'
$env:LMMS_SVS_PLUGIN_DIR = Join-Path $PWD $(if ($Installed) { 'build/Release/svs' } else { 'build/svs' })
$env:LMMS_UI_INSTALLED_EXE = if ($Installed -and -not $SpotCheck -and -not $PluginPanels) { Join-Path $PWD 'build/Release/lmms.exe' } else { $null }
$stkCacheLine = Get-Content 'build/CMakeCache.txt' | Where-Object { $_ -match '^STK_RAWWAVE_ROOT:PATH=' } | Select-Object -First 1
$env:LMMS_UI_STK_DIR = if ($stkCacheLine) { $stkCacheLine.Substring($stkCacheLine.IndexOf('=') + 1) } else { $null }
if ($Installed) { $env:LMMS_UI_STK_DIR = Join-Path $PWD 'build/Release/data/stk/rawwaves' }
$percent = [int]([double]$Scale * 100)
$env:LMMS_UI_EVIDENCE = Join-Path $PWD "doc/ui-modernization/validation/$Stage-$percent"
$env:LMMS_UI_SAMPLE = Join-Path $PWD 'doc/ui-modernization/fixtures/tone.wav'
$env:LMMS_UI_FIXTURE = Join-Path $PWD 'doc/ui-modernization/fixtures/modernization.mmp'
$env:QT_SCALE_FACTOR = $Scale
$env:LMMS_UI_PLUGIN_BASELINES = if ($PluginPanels) { '1' } else { $null }
$env:PATH = "$env:QTDIR/bin;$(Join-Path $PWD 'build/Release');$(Join-Path $PWD 'build/Release/plugins');$env:PATH"
$captureSuffix = if ($PluginPanels) { '-plugins' } else { '' }
[string[]]$captureCases = if ($PluginPanels) { @('pluginPanels') } elseif ($SpotCheck) { @('standardControls', 'crowdedWindow') } else { @('scenes', 'standardControls', 'installedLaunch') }
& ./build/tests/ui/Release/lmms.exe @captureCases -o "doc/ui-modernization/validation/$Stage-$percent$captureSuffix-results.txt,txt" -o '-,txt' 2>&1 |
    Tee-Object -FilePath 'build.log' -Encoding utf8
$buildExitCode = $LASTEXITCODE
Copy-Item 'build.log' "doc/ui-modernization/validation/$Stage-$percent$captureSuffix.log"
if ($buildExitCode -ne 0) {
    Get-Content 'build.log' -Tail 80
    Get-Content "doc/ui-modernization/validation/$Stage-$percent$captureSuffix-results.txt" -Tail 50
    exit $buildExitCode
}
