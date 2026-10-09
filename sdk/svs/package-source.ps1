param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../../build/svs-sdk-source'))
$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$stageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $stageRoot) {
    throw "Use an empty output path: $stageRoot"
}
New-Item -ItemType Directory -Path $stageRoot | Out-Null
foreach ($entry in @('include', 'docs', 'cmake', 'examples', 'tools', 'CMakeLists.txt', 'README.md', 'LICENSE', 'package-source.ps1')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $entry) -Destination $stageRoot -Recurse
}
$fullSource = Join-Path $sourceRoot 'examples/full'
if (-not (Test-Path -LiteralPath $fullSource)) {
    $fullSource = Join-Path $sourceRoot '../../plugins/SVSExample'
}
$fullTarget = Join-Path $stageRoot 'examples/full'
if (-not (Test-Path -LiteralPath $fullTarget)) {
    New-Item -ItemType Directory -Path $fullTarget | Out-Null
    foreach ($entry in @('CMakeLists.txt', 'SVSExample.cpp', 'Json.h', 'Schemas.h.in', 'manifest.json.in', 'manifest.json', 'full.json', 'minimal.json', 'zh.json', 'ja.json', 'en.json', 'avatar.svg', 'portrait.svg', 'avatar-lite.svg', 'portrait-lite.svg', 'SVSExample-demo.mmp')) {
        Copy-Item -LiteralPath (Join-Path $fullSource $entry) -Destination $fullTarget
    }
}
Copy-Item -LiteralPath (Join-Path $fullSource 'Json.h') -Destination (Join-Path $stageRoot 'tools/Json.h')
foreach ($component in @(
        @{ Name = 'diffsinger'; Repository = '../../plugins/SVSDiffSinger' },
        @{ Name = 'compute'; Repository = '../../src/core/svs/compute' }
    )) {
    $componentTarget = Join-Path $stageRoot ('examples/' + $component.Name)
    if (Test-Path -LiteralPath $componentTarget) {
        continue
    }
    $componentSource = Join-Path $sourceRoot $component.Repository
    foreach ($file in Get-ChildItem -LiteralPath $componentSource -Recurse -File) {
        if ($file.Name -ne 'CMakeLists.txt' -and $file.Extension -notin @('.cpp', '.c', '.h', '.json', '.in', '.txt', '.md')) {
            continue
        }
        $relative = $file.FullName.Substring(([IO.Path]::GetFullPath($componentSource)).Length + 1)
        $destination = Join-Path $componentTarget $relative
        $parent = Split-Path -Parent $destination
        if (-not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
}
$archive = "$stageRoot.zip"
if (Test-Path -LiteralPath $archive) {
    throw "Archive already exists: $archive"
}
Compress-Archive -LiteralPath $stageRoot -DestinationPath $archive
Write-Output "SDK source archive: $archive"
Write-Output 'Contents: public headers, CMake, docs/license, legacy examples, compute consumer, native DiffSinger/shared compute sources and SDK-only validation tools.'
