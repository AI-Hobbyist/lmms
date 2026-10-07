param([string]$ProductCommit)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ProductCommit) {
    $ProductCommit = & git -C $project rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the product source commit.' }
}
$runtime = Join-Path $project 'build/Release'
$output = Join-Path $project 'build/packages'
if (-not (Test-Path -LiteralPath $runtime -PathType Container) -or -not (Test-Path -LiteralPath $output -PathType Container)) { throw 'Reuse the existing runtime and package directories.' }
$name = 'lmms-modern-ui-full-' + $ProductCommit.Substring(0, 9) + '-win64.zip'
$zipPath = Join-Path $output $name
if (Test-Path -LiteralPath $zipPath) { throw "Package already exists: $zipPath" }
$payload = @{}
foreach ($file in Get-ChildItem -LiteralPath $runtime -File) {
    if ($file.Name -match '^(concrt140d|msvcp140(_[12])?d(_.*)?|vcruntime140(_1)?d|ucrtbased)\.dll$') { continue }
    if ($file.Extension -eq '.dll' -or $file.Name -in @('lmms.exe', 'lmms.exe.manifest', 'lmms.VisualElementsManifest.xml')) { $payload[$file.Name] = $file.FullName }
}
# Only installed data/voices enter the package, never a local workspace/config.
foreach ($path in Get-Content -LiteralPath (Join-Path $project 'build/install_manifest.txt')) {
    $full = [IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith($runtime + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Installation manifest points outside the existing runtime.' }
    $relative = $full.Substring($runtime.Length + 1).Replace('\', '/')
    if ($relative -match '^(data|svs)/') { $payload[$relative] = $full }
}
# The SVS example is built directly into the runtime; its DLL may be absent
# from the top-level install manifest when the nested target is excluded.
$svsExampleDll = Join-Path $runtime 'svs/SVSExample/SVSExample.dll'
if (-not (Test-Path -LiteralPath $svsExampleDll -PathType Leaf)) { throw 'Deployed SVS example DLL missing.' }
$payload['svs/SVSExample/SVSExample.dll'] = $svsExampleDll
foreach ($directory in @('plugins', 'assets', 'generic', 'iconengines', 'imageformats', 'networkinformation', 'platforms', 'styles', 'tls')) {
    $folder = Join-Path $runtime $directory
    if (-not (Test-Path -LiteralPath $folder)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $folder -File -Recurse) {
        if ($file.Name -match '\.(disabled|pdb|lib|exp)$' -or $file.Name -like '*.disabled.*') { continue }
        $payload[$file.FullName.Substring($runtime.Length + 1).Replace('\', '/')] = $file.FullName
    }
}
$payload['LICENSE.txt'] = Join-Path $project 'LICENSE.txt'
$targets = Get-Content -LiteralPath (Join-Path $project 'doc/ui-modernization/validation/plugin-targets.txt')
if ($targets.Count -ne 52) { throw 'Frozen plugin target count differs.' }
foreach ($target in $targets) { if (-not $payload.ContainsKey("plugins/$target.dll")) { throw "Missing enabled plugin: $target" } }
foreach ($required in @('platforms/qwindows.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'data/themes/default/style.css', 'plugins/midiimport.dll', 'plugins/midiexport.dll', 'plugins/hydrogenimport.dll', 'plugins/vstbase.dll', 'plugins/RemoteCatalogIo.exe', 'plugins/RemoteVstHost64.exe', 'plugins/32/RemoteVstHost32.exe', 'plugins/RemoteVstPlugin64.exe', 'plugins/32/RemoteVstPlugin32.exe', 'plugins/RemoteZynAddSubFx.exe')) {
    if (-not $payload.ContainsKey($required)) { throw "Incomplete runtime: $required" }
}
foreach ($required in @('svs/SVSExample/SVSExample.dll', 'svs/SVSExample/manifest.json', 'svs/SVSExample/avatar.svg', 'svs/SVSExample/portrait.svg', 'data/themes/default/svs_track.svg')) {
    if (-not $payload.ContainsKey($required)) { throw "Incomplete SVS runtime: $required" }
}
$records = @($payload.Keys | Sort-Object | ForEach-Object {
    $file = Get-Item -LiteralPath $payload[$_]
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Runtime file is a reparse point: $_" }
    [pscustomobject]@{Path = $_; Bytes = $file.Length; SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
})
$manifest = [ordered]@{Format = 1; ProductCommit = $ProductCommit; Platform = 'Windows x64'; EnabledUiPluginCount = 52; Files = $records}
$manifestText = $manifest | ConvertTo-Json -Depth 5
$instructions = @'
LMMS 现代界面全量替换包（Windows x64）

1. 关闭 LMMS。将 ZIP 解压到任意临时位置。
2. 双击 Install-Replace.cmd，输入原安装目录（包含 lmms.exe 的目录）。
   Program Files 目录通常需要以管理员身份运行该 cmd。
3. 安装器校验包内 SHA256 后覆盖程序、插件、运行库和 data/svs 资源，随后再次校验。
   包清单之外的旧 LMMS plugins DLL 会原地改名为 .dll.disabled，避免旧 ABI 闪退。
   配置、个人工程、外部 VST 路径与个人声库不被删除或覆盖。

也可把包内运行文件直接覆盖到安装目录，但必须同时停用旧 plugins 中包外的 DLL。
优先使用安装器自动处理残留。不要复制开发版 .lmmsrc.xml 或 portable_mode.txt。
若现有配置指向自定义主题目录，新默认主题可能不会自动生效；本轮未新增经典/现代切换入口。

本包为完整运行文件，并非差分包：主程序、52 个启用 UI 插件、3 个导入导出插件、
支持库、VST 32/64 位辅助程序、Zyn 辅助程序、Qt/音频运行库、预设/采样/主题与 SVS 示例。
Sid（缺 Perl）与 GigPlayer（缺 libgig）未构建。
本包包含 SVS 音符拉伸音素时长限制修复与原版风格琴键，SVS 自动回归 63 项通过。
SVS 实窗专项使用实际部署插件与主题通过；最终操作体验和听感待人工验收。
本机测试头像、立绘及开发版个人配置未打包；示例使用自带 SVG 资源。

构建来源：https://github.com/AI-Hobbyist/lmms
源代码版本：7b44c5487187d241c2dece22630a573479129c44（F6）
工作区仍含用户其他任务的 Song.h/Song.cpp 未提交修改；包来自该工作区构建，不称纯提交重建。
详细证据：仓库 doc/ui-modernization/acceptance.md 与 delivery-audit.md。
许可文本：LICENSE.txt；第三方资源随其原有许可。请保留自己的原安装文件作为回滚来源。
'@
$instructions = $instructions.Replace('7b44c5487187d241c2dece22630a573479129c44（F6）', $ProductCommit)
$cmd = "@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0Install-Replace.ps1`"`r`npause`r`n"
$expected = @{}
$archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
function Add-TextEntry([string]$Name, [string]$Value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
    $entry = $archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $stream = $entry.Open()
    try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $expected[$Name] = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') } finally { $sha.Dispose() }
}
try {
    foreach ($file in $records) {
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $payload[$file.Path], $file.Path, [IO.Compression.CompressionLevel]::Optimal)
        $expected[$file.Path] = $file.SHA256
    }
    Add-TextEntry 'replacement-manifest.json' $manifestText
    Add-TextEntry 'README-覆盖安装.txt' $instructions
    Add-TextEntry 'Install-Replace.cmd' $cmd
    Add-TextEntry 'Install-Replace.ps1' (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Install-LmmsReplacement.ps1') -Raw)
} finally { $archive.Dispose() }
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -ne $expected.Count) { throw 'Archive entry count differs.' }
    foreach ($entry in $archive.Entries) {
        if (-not $expected.ContainsKey($entry.FullName)) { throw "Unexpected archive entry: $($entry.FullName)" }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') } finally { $stream.Dispose(); $sha.Dispose() }
        if ($hash -ne $expected[$entry.FullName]) { throw "Archive SHA256 mismatch: $($entry.FullName)" }
    }
} finally { $archive.Dispose() }
$manifestText | Set-Content -LiteralPath ($zipPath + '.manifest.json') -Encoding UTF8
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
"$hash  $name" | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding ASCII
Write-Output "Created and verified $($records.Count) runtime files + 4 package entries: $zipPath"
Write-Output "Bytes: $((Get-Item -LiteralPath $zipPath).Length); SHA256: $hash"
