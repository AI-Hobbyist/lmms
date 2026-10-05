param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../../build/svs-sdk-source'))
$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$stageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $stageRoot) { throw "Use an empty output path: $stageRoot" }
New-Item -ItemType Directory -Path $stageRoot | Out-Null
foreach ($entry in @('include','docs','cmake','examples','tools','CMakeLists.txt','README.md','LICENSE','package-source.ps1')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $entry) -Destination $stageRoot -Recurse
}
$fullSource = Join-Path $sourceRoot 'examples/full'
if (-not (Test-Path -LiteralPath $fullSource)) { $fullSource = Join-Path $sourceRoot '../../plugins/SVSExample' }
$fullTarget = Join-Path $stageRoot 'examples/full'
if (-not (Test-Path -LiteralPath $fullTarget)) {
    New-Item -ItemType Directory -Path $fullTarget | Out-Null
    foreach ($entry in @('CMakeLists.txt','SVSExample.cpp','Json.h','Schemas.h.in','manifest.json.in','manifest.json','full.json','minimal.json','zh.json','ja.json','en.json','avatar.svg','portrait.svg','avatar-lite.svg','portrait-lite.svg','SVSExample-demo.mmp')) {
        Copy-Item -LiteralPath (Join-Path $fullSource $entry) -Destination $fullTarget
    }
}
Copy-Item -LiteralPath (Join-Path $fullSource 'Json.h') -Destination (Join-Path $stageRoot 'tools/Json.h')
$archive = "$stageRoot.zip"
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
Compress-Archive -LiteralPath $stageRoot -DestinationPath $archive
Write-Output "SDK source archive: $archive"
Write-Output 'Contents: public headers, CMake, docs/license, minimal C example, full C++ example, SDK-only validation tool.'
