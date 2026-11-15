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
        Path = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
        Bytes = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}

foreach ($path in @((Join-Path $runtime 'lmms.exe'), (Join-Path $base 'lmms.exe'),
        (Join-Path $base 'platforms/qwindows.dll'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required runtime file missing: $path"
    }
}

# Use the actual installation, whose older delivery manifest may be stale.
$baselinePaths = @((Join-Path $base 'lmms.exe'))
$baselinePaths += @(Get-ChildItem -LiteralPath $base -File -Filter '*.dll' |
    Select-Object -ExpandProperty FullName)
foreach ($directory in @('plugins', 'platforms')) {
    $baselinePaths += @(Get-ChildItem -LiteralPath (Join-Path $base $directory) -File -Recurse |
        Where-Object { $_.Extension -in @('.dll', '.exe') } | Select-Object -ExpandProperty FullName)
}
$baseline = @($baselinePaths | Sort-Object -Unique | ForEach-Object { Get-Record $base $_ })
$payload = @(Get-Record $runtime (Join-Path $runtime 'lmms.exe'))
$commit = & git -C $project rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
$changes = @(& git -C $project status --short --untracked-files=no --ignore-submodules)
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source worktree changes.' }
$baseExe = $baseline | Where-Object { $_.Path -eq 'lmms.exe' }
$manifest = [ordered]@{
    Format = 1
    PackageKind = 'Incremental'
    Platform = 'Windows x64'
    ProductCommit = $commit
    WorktreeStatus = $changes
    BasePackage = 'LMMS-installed-' + $baseExe.SHA256.Substring(0, 12)
    BaseVersion = (Get-Item -LiteralPath (Join-Path $base 'lmms.exe')).VersionInfo.FileVersion
    BaseFiles = $baseline
    Files = $payload
    RemoveFiles = @()
}

$null = New-Item -ItemType Directory -Path $output -Force
$name = 'lmms-mcp-reload-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-win64'
$staging = Join-Path $output $name
$null = New-Item -ItemType Directory -Path $staging
Copy-Item -LiteralPath (Join-Path $runtime 'lmms.exe') -Destination $staging
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Replace.ps1') -Destination $staging
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $staging 'replacement-manifest.json') -Encoding utf8
@"
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Replace.ps1" -TargetDirectory "$base"
pause
"@ | Set-Content -LiteralPath (Join-Path $staging 'Install-Replace.cmd') -Encoding ascii
@"
LMMS MCP 工程重载修复增量包（Windows x64）

基线安装：$base
基线版本：$($manifest.BaseVersion)
基线 EXE SHA256：$($baseExe.SHA256)
源码提交：$commit
构建工作区状态：$($changes -join '; ')

仅替换 lmms.exe，不删除或替换插件、声库、个人配置与工程。
修复非 track 节点加载死循环、工程清理/恢复期间命令重入，以及加载失败后的状态残留。
普通成功编辑继续直接操作当前工程；dryRun/回滚/快照撤销的原有语义保留。

1. 关闭 LMMS，备份原安装目录中的 lmms.exe，作为回滚文件。
2. 解压本包，以管理员身份运行 Install-Replace.cmd。
3. 安装器核对本包及实际基线运行库/插件的 SHA256，只替换清单中的文件。
4. 如需回滚，关闭 LMMS 后恢复第 1 步备份的 lmms.exe。

人工操作验收：MANUAL/PENDING。详细自动验证记录见仓库 doc/agent/MCP-reload-fix.md。
本包未自动安装到生产目录。
"@ | Set-Content -LiteralPath (Join-Path $staging 'README-覆盖安装.txt') -Encoding utf8

$archive = Join-Path $output ($name + '.zip')
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
"$hash  $([IO.Path]::GetFileName($archive))" |
    Set-Content -LiteralPath ($archive + '.sha256') -Encoding ascii
Write-Output "Package: $archive"
Write-Output "SHA256: $hash"
Write-Output "Payload: $($payload.Count); actual baseline binaries: $($baseline.Count)"
