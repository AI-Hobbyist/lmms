param(
    [string]$Device = ''
)
$ErrorActionPreference = 'Stop'
. $PSScriptRoot/Enter-LmmsEnvironment.ps1

$probe = Join-Path $PSScriptRoot '../build/Release/svs/compute/SVSComputeProbe.exe'
$matrix = Get-Content (Join-Path $PSScriptRoot '../doc/svs/DiffSinger-six-package-matrix.json') -Raw | ConvertFrom-Json
$fixtureRoot = Join-Path $PSScriptRoot '../refs/Singers'
if (-not $Device) {
    $devices = Get-Content (Join-Path $PSScriptRoot '../doc/svs/validation/B0-device-probe.json') -Raw | ConvertFrom-Json
    $selected = $devices.devices | Where-Object available | Sort-Object dedicatedBytes -Descending | Select-Object -First 1
    if (-not $selected) {
        throw 'No successfully probed DML device; model matrix is pending'
    }
    $Device = $selected.luid
}
$reportFile = Join-Path $PSScriptRoot '../doc/svs/validation/B0-model-session-latest.json'
$voices = @()
$total = 0
foreach ($voice in $matrix.voices) {
    $models = @()
    foreach ($model in $voice.models) {
        $total++
        $path = Join-Path (Join-Path $fixtureRoot $voice.root) $model.file
        Write-Output "B0 [$total/48] $($voice.root) $($model.stage)/$($model.role)"
        & $probe $reportFile $Device $path session
        $probeExitCode = $LASTEXITCODE
        if (Test-Path -LiteralPath $reportFile) {
            $result = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
        } else {
            $result = $null
        }
        $cpuOnly = $voice.configs.vocoder.force_on_cpu -eq 'true' -and $model.stage -eq 'vocoder'
        $models += [ordered]@{
            stage = $model.stage
            role = $model.role
            file = $model.file
            cpuOnly = $cpuOnly
            constraintReason = $(if ($cpuOnly) { 'vocoder.force_on_cpu' } else { '' })
            dmlSession = $(if ($probeExitCode -eq 0) { 'PASS' } else { 'UNSUPPORTED_OR_BACKEND_FAILURE' })
            exitCode = $probeExitCode
            evidence = $result
            execution = 'PENDING_B3'
        }
        Remove-Item -LiteralPath $reportFile -ErrorAction SilentlyContinue
    }
    $voices += [ordered]@{ root = $voice.root; models = $models }
}
if ($total -ne 48 -or $voices.Count -ne 6) {
    throw "Expected six packages/48 models, got $($voices.Count)/$total"
}
[ordered]@{
    schemaVersion = 1
    runtime = 'onnxruntime-directml-1.23.0/directml-1.15.4'
    device = $Device
    purpose = 'B0 session compatibility; model node execution and numerical comparison pending B3'
    voices = $voices
} | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $PSScriptRoot '../doc/svs/DiffSinger-B0-gpu-matrix.json') -Encoding utf8
Write-Output "B0 six-package matrix complete: $total models"
exit 0
