param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Targets = 'lmms',
    [ValidateRange(1, 16)]
    [int]$Jobs = 2,
    [string]$TestPattern
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
Set-Location -LiteralPath (Join-Path $PSScriptRoot '..')
. ./buildtools/Enter-LmmsEnvironment.ps1

if (-not (Test-Path -LiteralPath build/CMakeCache.txt)) {
    throw 'Configure the existing build directory before building.'
}

$runtime = Join-Path $PWD "build/$Configuration"
$dependencyBin = Join-Path $PWD 'build/vcpkg_installed/x64-windows/bin'
if ($Configuration -eq 'Debug') {
    $dependencyBin = Join-Path $PWD 'build/vcpkg_installed/x64-windows/debug/bin'
}
$env:PATH = "$runtime;$dependencyBin;$env:QTDIR/bin;$env:PATH"

if ($TestPattern) {
    # Any GUI tests must create native Windows Qt windows.
    $env:QT_QPA_PLATFORM = 'windows'
    $env:LMMS_PLUGIN_DIR = Join-Path $runtime 'plugins'
    & ctest --test-dir build -C $Configuration --output-on-failure --timeout 120 -R $TestPattern 2>&1 |
        Tee-Object -FilePath build.log -Encoding utf8
} else {
    $cmakeCache = Get-Content -LiteralPath build/CMakeCache.txt
    $instanceEntry = $cmakeCache | Where-Object { $_ -match '^CMAKE_GENERATOR_INSTANCE:INTERNAL=' }
    if (-not $instanceEntry) {
        throw 'The progress logger requires the configured Visual Studio MSBuild generator.'
    }
    $instance = $instanceEntry -replace '^[^=]*=', ''
    $framework = Join-Path $instance 'MSBuild/Current/Bin/Microsoft.Build.Framework.dll'
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    $logger = Join-Path $PWD 'build/BuildProgressLogger.dll'
    $source = Join-Path $PSScriptRoot 'BuildProgressLogger.cs'
    if (-not (Test-Path -LiteralPath $logger) -or
        (Get-Item -LiteralPath $source).LastWriteTimeUtc -gt (Get-Item -LiteralPath $logger).LastWriteTimeUtc) {
        & $compiler /nologo /target:library "/reference:$framework" "/out:$logger" $source 2>&1 |
            Tee-Object -FilePath build.log -Encoding utf8
        $buildExitCode = $LASTEXITCODE
        if ($buildExitCode -ne 0) { exit $buildExitCode }
    }
    $buildTargets = $Targets -split ','
    $events = Join-Path $PWD 'build/build-events.tsv'
    Set-Content -LiteralPath $events -Value '' -Encoding utf8
    & cmake --build build --config $Configuration --parallel $Jobs --target @buildTargets -- "/logger:BuildProgressLogger,$logger;$events" 2>&1 |
        Tee-Object -FilePath build.log -Encoding utf8
}
$buildExitCode = $LASTEXITCODE
exit $buildExitCode
