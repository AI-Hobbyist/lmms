<#
.SYNOPSIS
    Builds the incremental package for the built-in Agent chat (M5).

.DESCRIPTION
    Compares the development build with the actual installation, packages only
    new or changed files (lmms.exe, translations, theme files, the default
    system prompt and the portable tools), checks exports and dependencies,
    and writes a ZIP with manifest, README and SHA256. Nothing is installed.
#>
param(
    [string]$BaseDirectory = 'D:\Program Files\LMMS'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Enter-LmmsEnvironment.ps1')
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime = Join-Path $project 'build/Release'
$base = [IO.Path]::GetFullPath($BaseDirectory).TrimEnd('\', '/')
$output = Join-Path $project 'build/packages'

function Get-Record([string]$Root, [string]$Path) {
    $file = Get-Item -LiteralPath $Path
    return [ordered]@{
        Path   = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
        Bytes  = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}

function Get-Dumpbin {
    $root = 'C:\Program Files\Microsoft Visual Studio'
    $tool = Get-ChildItem -LiteralPath $root -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\Hostx64\\x64\\' } | Select-Object -First 1
    if (-not $tool) { throw 'dumpbin.exe (Hostx64/x64) not found.' }
    return $tool.FullName
}

function Get-Exports([string]$Dumpbin, [string]$Path) {
    $names = @{}
    foreach ($line in & $Dumpbin /nologo /exports $Path) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { $names[$Matches[1]] = $true }
    }
    return $names
}

function Get-Dependents([string]$Dumpbin, [string]$Path) {
    $inside = $false
    $result = @()
    foreach ($line in & $Dumpbin /nologo /dependents $Path) {
        if ($line -match 'Image has the following dependencies') { $inside = $true; continue }
        if ($inside -and $line -match '^\s+(\S+\.dll)\s*$') { $result += $Matches[1] }
        elseif ($inside -and $line -match 'Summary') { break }
    }
    return $result
}

# 1. Payload: the new program plus every chat resource missing or different in the installation.
$sources = [ordered]@{ 'lmms.exe' = (Join-Path $runtime 'lmms.exe') }
$sources['data/locale/zh_CN.qm'] = Join-Path $project 'data/locale/zh_CN.qm'
$sources['data/system_prompt.txt'] = Join-Path $project 'data/system_prompt.txt'
# Only chat theme files: other theme differences belong to unrelated work and are not shipped here.
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $project 'data/themes/default') -File |
        Where-Object { $_.Name -eq 'style.css' -or $_.Name -like 'agent_chat_*' }) {
    $sources['data/themes/default/' + $file.Name] = $file.FullName
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $runtime 'tools') -File -Recurse) {
    $sources['tools/' + $file.FullName.Substring((Join-Path $runtime 'tools').Length + 1).Replace('\', '/')] = $file.FullName
}
$payload = @()
foreach ($relative in $sources.Keys) {
    $source = $sources[$relative]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing payload source: $source" }
    $installed = Join-Path $base $relative
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ((Test-Path -LiteralPath $installed -PathType Leaf) -and
        (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -eq $sourceHash) {
        continue
    }
    $payload += [ordered]@{
        Path      = $relative
        Bytes     = (Get-Item -LiteralPath $source).Length
        SHA256    = $sourceHash
        Source    = $source
        Installed = Test-Path -LiteralPath $installed -PathType Leaf
    }
}

# 2. ABI: plugins import from lmms.exe by name, so no export may disappear.
$dumpbin = Get-Dumpbin
$oldExports = Get-Exports $dumpbin (Join-Path $base 'lmms.exe')
$newExports = Get-Exports $dumpbin (Join-Path $runtime 'lmms.exe')
$removedExports = @($oldExports.Keys | Where-Object { -not $newExports.ContainsKey($_) })
if ($removedExports.Count) { throw "lmms.exe no longer exports: $($removedExports -join ', ')" }

# 3. Dependencies of every packaged program resolve in the package, installation or Windows.
$missingDependencies = @()
foreach ($item in $payload | Where-Object { $_.Path -match '\.(exe|dll)$' }) {
    # Root-level programs have an empty folder, which Join-Path does not accept.
    $folder = [IO.Path]::GetDirectoryName($item.Path).Replace('\', '/')
    foreach ($name in Get-Dependents $dumpbin $item.Source) {
        $sibling = if ($folder) { "$folder/$name" } else { $name }
        $packaged = $payload | Where-Object { $_.Path -ieq $sibling }
        $candidates = @([IO.Path]::Combine($base, $sibling), [IO.Path]::Combine($base, $name),
            [IO.Path]::Combine($env:SystemRoot, 'System32', $name))
        if ($packaged -or ($candidates | Where-Object { Test-Path -LiteralPath $_ }) -or $name -match '^(api|ext)-ms-') {
            continue
        }
        $missingDependencies += "$($item.Path) -> $name"
    }
}
if ($missingDependencies.Count) { throw "Unresolved dependencies: $($missingDependencies -join '; ')" }

# 4. Baseline: the installed binaries this package was checked against.
$baselinePaths = @((Join-Path $base 'lmms.exe'))
$baselinePaths += @(Get-ChildItem -LiteralPath $base -File -Filter '*.dll' | Select-Object -ExpandProperty FullName)
foreach ($directory in @('plugins', 'platforms')) {
    $baselinePaths += @(Get-ChildItem -LiteralPath (Join-Path $base $directory) -File -Recurse |
        Where-Object { $_.Extension -in @('.dll', '.exe') } | Select-Object -ExpandProperty FullName)
}
$baseline = @($baselinePaths | Sort-Object -Unique | ForEach-Object { Get-Record $base $_ })
$commit = & git -C $project rev-parse HEAD
$changes = @(& git -C $project status --short --untracked-files=no --ignore-submodules)
$baseExe = $baseline | Where-Object { $_.Path -eq 'lmms.exe' }
$files = @($payload | ForEach-Object { [ordered]@{ Path = $_.Path; Bytes = $_.Bytes; SHA256 = $_.SHA256 } })
$manifest = [ordered]@{
    Format         = 1
    PackageKind    = 'Incremental'
    Platform       = 'Windows x64'
    ProductCommit  = $commit
    WorktreeStatus = $changes
    BasePackage    = 'LMMS-installed-' + $baseExe.SHA256.Substring(0, 12)
    BaseVersion    = (Get-Item -LiteralPath (Join-Path $base 'lmms.exe')).VersionInfo.FileVersion
    BaseFiles      = $baseline
    Files          = $files
    # Only files actually in this payload can be preserved.
    PreserveFiles  = @($files | Where-Object { $_.Path -eq 'data/system_prompt.txt' } | ForEach-Object { $_.Path })
    RemoveFiles    = @()
}

# 5. Staging folder mirrors the installation layout for manual copying.
$null = New-Item -ItemType Directory -Path $output -Force
$name = 'lmms-agent-chat-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-win64'
$staging = Join-Path $output $name
foreach ($item in $payload) {
    $destination = Join-Path $staging $item.Path
    $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force
    Copy-Item -LiteralPath $item.Source -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Replace.ps1') -Destination $staging
$json = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $staging 'replacement-manifest.json'), $json, (New-Object System.Text.UTF8Encoding $false))
$fileList = ($payload | ForEach-Object {
        '  ' + $_.Path + $(if ($_.Installed) { '（替换）' } else { '（新增）' })
    }) -join "`r`n"
@"
LMMS 内置 Agent 聊天框增量包（Windows x64）

基线安装：$base
基线版本：$($manifest.BaseVersion)
基线 EXE SHA256：$($baseExe.SHA256)
源码提交：$commit
构建工作区状态：$($changes -join '; ')

包内文件按安装目录相对路径存放，共 $($payload.Count) 个：
$fileList

手动替换（推荐）：
1. 关闭 LMMS，备份安装目录中的 lmms.exe、data\locale\zh_CN.qm 和 data\themes\default\style.css（回滚用）。
2. 把本包中的 lmms.exe、data、tools 按相同相对路径复制到安装目录，覆盖同名文件。
3. data\system_prompt.txt：若安装目录已有并且你改过，请保留你的文件；本包中的是新默认内容，可另存为 system_prompt.txt.default 后自行比对选用。
4. 不需要删除任何文件；插件、声库、个人配置、会话与工程不受影响。
5. 回滚：关闭 LMMS，恢复第 1 步的备份；新增的 tools 目录与 agent_chat_*.svg 可保留或删除。

可选：以管理员身份运行
  powershell -NoProfile -ExecutionPolicy Bypass -File Install-Replace.ps1 -TargetDirectory "$base"
它会先核对本包与基线的 SHA256，再复制；已存在的 system_prompt.txt 不会被覆盖（新默认写为 .default）。
加 -VerifyOnly 只核验、不写入。

工具：aria2 1.37.0（GPL-2.0-or-later，tools\aria2\COPYING）、7-Zip 26.06（LGPL-2.1-or-later + unRAR 限制，tools\7zip\License.txt）；
版本、来源 URL 与 SHA256 见 tools\manifest.json。下载的素材不会被执行。

真实厂商 API、联网搜索实例、公网下载与整机 GUI 操作：MANUAL/PENDING（替换后人工验收）。
自动验收记录见仓库 doc/agent/chat-implementation-progress.md。
本包未安装到生产目录。
"@ | Set-Content -LiteralPath (Join-Path $staging 'README-覆盖安装.txt') -Encoding utf8

$archive = Join-Path $output ($name + '.zip')
# Entry names use forward slashes (Compress-Archive in PowerShell 5 writes backslashes,
# which the installer's archive lookup and other unzip tools do not match).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $staging -File -Recurse) {
        $entry = $file.FullName.Substring($staging.Length + 1).Replace('\', '/')
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
    }
}
finally {
    $zip.Dispose()
}
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ascii
Write-Output "Package: $archive"
Write-Output "SHA256: $hash"
Write-Output "Payload: $($payload.Count) files; baseline binaries: $($baseline.Count)"
Write-Output "Exports: old $($oldExports.Count), new $($newExports.Count), removed 0"
Write-Output "Dependencies: all resolved"
