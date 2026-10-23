param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,
    [string]$OutputDir = 'build/packages',
    [string]$StageRoot = 'build/package-stage',
    [string]$Version = '',
    [string]$Commit = '',
    [string]$PythonExe = '',
    [switch]$SkipWindeploy,
    [switch]$KeepStage
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = (Resolve-Path $SourceDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $source 'lmms.exe') -PathType Leaf)) {
    throw "Source directory does not contain lmms.exe: $source"
}

function Get-RepoVersion {
    foreach ($candidate in @(
            (Join-Path (Split-Path $source -Parent) 'lmmsversion.h'),
            (Join-Path $source 'lmmsversion.h'))) {
        if (Test-Path -LiteralPath $candidate) {
            $match = Select-String -LiteralPath $candidate -Pattern 'LMMS_VERSION\s+"([^"]+)"'
            if ($match) { return $match.Matches[0].Groups[1].Value }
        }
    }
    return (& git -C $repo describe --tags --always --dirty).Trim()
}

if (-not $Commit) { $Commit = (& git -C $repo rev-parse HEAD).Trim() }
if (-not $Version) { $Version = Get-RepoVersion }
$shortCommit = $Commit.Substring(0, [Math]::Min(9, $Commit.Length))
$name = "lmms-enhanced-full-$shortCommit-win64"
$stage = Join-Path (Join-Path $repo $StageRoot) $name
$output = Join-Path $repo $OutputDir
New-Item -ItemType Directory -Path $output -Force | Out-Null

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

$excludedNames = @(
    '.lmmsrc.xml', 'portable_mode.txt', 'qt.conf', 'install_manifest.txt',
    'concrt140d.dll', 'msvcp140d.dll', 'msvcp140_1d.dll', 'msvcp140_2d.dll',
    'msvcp140d_atomic_wait.dll', 'msvcp140d_codecvt_ids.dll', 'vcruntime140_1d.dll',
    'vcruntime140d.dll', 'ucrtbased.dll'
)
$excludedPatterns = @('^Qt6.*d\.dll$', '\.pdb$', '\.lib$', '\.exp$', '\.disabled($|\.)')
function Test-Excluded([string]$fileName) {
    if ($excludedNames -contains $fileName) { return $true }
    foreach ($pattern in $excludedPatterns) {
        if ($fileName -match $pattern) { return $true }
    }
    return $false
}

function Copy-FilteredTree([string]$sourceRoot, [string]$targetRoot) {
    Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | ForEach-Object {
        if (Test-Excluded $_.Name) { return }
        $relative = $_.FullName.Substring($sourceRoot.Length + 1)
        $destination = Join-Path $targetRoot $relative
        $parent = Split-Path $destination -Parent
        if (-not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
    }
}

# Runtime directories from the Windows deployment. Keep only data/runtime content.
foreach ($directory in @('assets', 'data', 'generic', 'iconengines', 'imageformats',
        'networkinformation', 'platforms', 'plugins', 'styles', 'svs', 'svs-project', 'tls')) {
    $path = Join-Path $source $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        Copy-FilteredTree $path (Join-Path $stage $directory)
    }
}

# Runtime files beside lmms.exe.
Get-ChildItem -LiteralPath $source -File | ForEach-Object {
    if (Test-Excluded $_.Name) { return }
    if ($_.Extension -notin @('.dll', '.exe', '.manifest', '.xml', '.md', '.txt')) { return }
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stage $_.Name) -Force
}

# Qt runtime and plugins. windeployqt is available in the Qt bin directory on CI.
if (-not $SkipWindeploy) {
    $windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if (-not $windeployqt -and $env:QTDIR) {
        $candidate = Join-Path (Join-Path $env:QTDIR 'bin') 'windeployqt.exe'
        if (Test-Path -LiteralPath $candidate) { $windeployqt = Get-Item -LiteralPath $candidate }
    }
    if (-not $windeployqt) {
        throw 'windeployqt.exe was not found. Set QTDIR or pass -SkipWindeploy.'
    }
    & $windeployqt.Source --release --no-translations --no-compiler-runtime --dir $stage (Join-Path $stage 'lmms.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }
    Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { Test-Excluded $_.Name } |
        Remove-Item -Force
}

function Copy-VCRuntime([string]$architecture, [string]$destination) {
    $candidates = @()
    if ($env:VCToolsRedistDir) {
        $candidates += Get-ChildItem -LiteralPath $env:VCToolsRedistDir -Recurse -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\$architecture\\" }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = (& $vswhere -latest -products * -property installationPath).Trim()
        if ($installation) {
            $redist = Join-Path $installation 'VC\Redist\MSVC'
            if (Test-Path -LiteralPath $redist) {
                $candidates += Get-ChildItem -LiteralPath $redist -Recurse -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match "\\$architecture\\" }
            }
        }
    }
    if ($candidates.Count -eq 0) {
        $fallback = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC'
        if (Test-Path -LiteralPath $fallback) {
            $candidates += Get-ChildItem -LiteralPath $fallback -Recurse -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "\\$architecture\\" }
        }
    }
    if ($candidates.Count -eq 0) { return }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $crt = $candidates | Sort-Object FullName -Descending | Select-Object -First 1
    Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File | Where-Object { -not (Test-Excluded $_.Name) } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $destination -Force }
}

function Copy-UCRT([string]$architecture, [string]$destination) {
    $roots = @(
        'C:\Program Files (x86)\Windows Kits\10\Redist',
        'D:\Windows Kits\10\Redist'
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $version = Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name -Descending | Select-Object -First 1
        $dlls = Join-Path (Join-Path $version.FullName 'ucrt\DLLs') $architecture
        if (Test-Path -LiteralPath $dlls) {
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
            Get-ChildItem -LiteralPath $dlls -Filter '*.dll' -File |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $destination -Force }
            return
        }
    }
}

Copy-VCRuntime 'x64' $stage
Copy-VCRuntime 'x86' (Join-Path $stage 'plugins\32')
Copy-UCRT 'x64' $stage
Copy-UCRT 'x86' (Join-Path $stage 'plugins\32')

# Carla's native helper is looked up beside the main executable.
$carla = Join-Path $source 'plugins\libcarla_native-plugin.dll'
if (Test-Path -LiteralPath $carla) { Copy-Item -LiteralPath $carla -Destination $stage -Force }

# MCP documentation and the generic replacement installer.
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $docs -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'data\agent\MCP.md') -Destination (Join-Path $docs 'MCP.md') -Force
Copy-Item -LiteralPath (Join-Path $repo 'data\agent\mcp_client.py') -Destination (Join-Path $docs 'mcp_client.py') -Force
$installerPs1 = Join-Path $PSScriptRoot 'Install-Replace.ps1'
$installerCmd = Join-Path $PSScriptRoot 'Install-Replace.cmd'
$readmeLines = @(
    'LMMS Windows x64 完整替换包',
    '',
    ('版本：' + $Version),
    ('源码提交：' + $Commit),
    '构建：MSVC / Qt 6 / Release',
    '包内文件：见 replacement-manifest.json（安装器会逐项校验 SHA256）',
    '',
    '关闭 LMMS 后解压本包，运行 Install-Replace.cmd 并选择现有 LMMS 安装目录（例如 D:\Program Files\LMMS）。',
    '安装器会校验包内每个文件的 SHA256，覆盖运行文件，并把旧版本中已不存在的插件 DLL 原地改名为 .disabled。',
    'Program Files 目录通常需要管理员权限。',
    '个人配置、工程、声库、缓存不在包内；不会复制 .lmmsrc.xml、portable_mode.txt 或 lmms-workspace。',
    '',
    '本机 HTTP MCP 已编入，运行时默认关闭；连接说明与 Python 示例见 docs/MCP.md。',
    'SVS/SVC 工具的使用说明见解压后的 SVC-使用说明.md。'
)
$readmeText = ($readmeLines -join "`n") + "`n"

$stageFiles = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName
$manifestFiles = @()
foreach ($file in $stageFiles) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
    $manifestFiles += [ordered]@{
        Path   = $relative
        Bytes  = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$manifest = [ordered]@{
    Format                = 1
    ProductCommit         = $Commit
    Platform              = 'Windows x64'
    EnabledUiPluginCount  = @(Get-ChildItem -LiteralPath (Join-Path $stage 'plugins') -Filter '*.dll' -File -ErrorAction SilentlyContinue).Count
    EnabledSvcPluginCount = @(Get-ChildItem -LiteralPath (Join-Path $stage 'svs') -Directory -ErrorAction SilentlyContinue).Count
    Files                 = $manifestFiles
    RemoveFiles           = @()
}
$manifestText = $manifest | ConvertTo-Json -Depth 5
$manifestText | Set-Content -LiteralPath (Join-Path $stage 'replacement-manifest.json') -Encoding UTF8
$readmeText | Set-Content -LiteralPath (Join-Path $stage 'README-覆盖安装.txt') -Encoding UTF8
Copy-Item -LiteralPath $installerPs1 -Destination (Join-Path $stage 'Install-Replace.ps1') -Force
Copy-Item -LiteralPath $installerCmd -Destination (Join-Path $stage 'Install-Replace.cmd') -Force

if ($PythonExe) {
    & $PythonExe (Join-Path $PSScriptRoot 'audit-package.py') $stage
    if ($LASTEXITCODE -ne 0) { throw "Package dependency audit failed with exit code $LASTEXITCODE." }
}

$zipPath = Join-Path $output ($name + '.zip')
if (Test-Path -LiteralPath $zipPath) { throw "Refusing to replace existing package: $zipPath" }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
        $null = [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $file.FullName, $relative, [System.IO.Compression.CompressionLevel]::Optimal)
    }
}
finally {
    $zip.Dispose()
}

$zipRead = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $expected = (Get-ChildItem -LiteralPath $stage -Recurse -File).Count
    if ($zipRead.Entries.Count -ne $expected) {
        throw "Archive entry count mismatch: $($zipRead.Entries.Count) != $expected"
    }
    foreach ($file in $manifestFiles) {
        $entry = $zipRead.GetEntry($file.Path)
        if (-not $entry -or $entry.Length -ne $file.Bytes) {
            throw "Missing or incorrect archive entry: $($file.Path)"
        }
        $stream = $entry.Open()
        try {
            $sha = [Security.Cryptography.SHA256]::Create()
            try {
                $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
            }
            finally { $sha.Dispose() }
        }
        finally { $stream.Dispose() }
        if ($hash -ne $file.SHA256) { throw "Archive SHA256 mismatch: $($file.Path)" }
    }
}
finally { $zipRead.Dispose() }

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
($zipHash + '  ' + [IO.Path]::GetFileName($zipPath)) | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding ASCII

if (-not $KeepStage) { Remove-Item -LiteralPath $stage -Recurse -Force }
Write-Output "Created $zipPath"
Write-Output "Files: $($manifestFiles.Count); bytes: $((Get-Item -LiteralPath $zipPath).Length); SHA256: $zipHash"
