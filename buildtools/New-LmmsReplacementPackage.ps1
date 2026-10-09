param([string]$ProductCommit, [string]$BaseManifest)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ProductCommit) {
    $ProductCommit = & git -C $project rev-parse HEAD
    if ($LASTEXITCODE -ne 0) {
        throw 'Cannot identify the product source commit.'
    }
}
$runtime = Join-Path $project 'build/Release'
$output = Join-Path $project 'build/packages'
if (-not (Test-Path -LiteralPath $runtime -PathType Container) -or -not (Test-Path -LiteralPath $output -PathType Container)) {
    throw 'Reuse the existing runtime and package directories.'
}
$name = 'lmms-enhanced-full-' + $ProductCommit.Substring(0, 9) + '-win64.zip'
if ($BaseManifest) {
    $name = 'lmms-enhanced-incremental-' + $ProductCommit.Substring(0, 9) + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-win64.zip'
}
$zipPath = Join-Path $output $name
if (Test-Path -LiteralPath $zipPath) {
    throw "Package already exists: $zipPath"
}
$payload = @{}
foreach ($file in Get-ChildItem -LiteralPath $runtime -File) {
    if ($file.Name -match '^(concrt140d|msvcp140(_[12])?d(_.*)?|vcruntime140(_1)?d|ucrtbased)\.dll$') {
        continue
    }
    if ($file.Extension -eq '.dll' -or $file.Name -in @('lmms.exe', 'lmms.exe.manifest', 'lmms.VisualElementsManifest.xml')) {
        $payload[$file.Name] = $file.FullName
    }
}
# Only installed data/voices enter the package, never a local workspace/config.
foreach ($path in Get-Content -LiteralPath (Join-Path $project 'build/install_manifest.txt')) {
    $full = [IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith($runtime + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Installation manifest points outside the existing runtime.'
    }
    $relative = $full.Substring($runtime.Length + 1).Replace('\', '/')
    if ($relative -match '^(data|svs)/') {
        $payload[$relative] = $full
    }
}
# The SVS example is built directly into the runtime; its DLL may be absent
# from the top-level install manifest when the nested target is excluded.
$svsExampleDll = Join-Path $runtime 'svs/SVSExample/SVSExample.dll'
if (-not (Test-Path -LiteralPath $svsExampleDll -PathType Leaf)) {
    throw 'Deployed SVS example DLL missing.'
}
$payload['svs/SVSExample/SVSExample.dll'] = $svsExampleDll
$sharedVocoderReadme = Join-Path $runtime 'svs/vocoders/README.md'
if (-not (Test-Path -LiteralPath $sharedVocoderReadme -PathType Leaf)) {
    throw 'Default shared vocoder directory instructions missing.'
}
$payload['svs/vocoders/README.md'] = $sharedVocoderReadme
$diffSingerPackage = Join-Path $runtime 'svs/SVSDiffSinger'
if (-not (Test-Path -LiteralPath $diffSingerPackage -PathType Container)) {
    throw 'Deployed DiffSinger package missing.'
}
foreach ($file in Get-ChildItem -LiteralPath $diffSingerPackage -File -Recurse) {
    if ($file.Name -match '\.(pdb|lib|exp|disabled)$') {
        continue
    }
    $payload[$file.FullName.Substring($runtime.Length + 1).Replace('\', '/')] = $file.FullName
}
$computeClient = Join-Path $runtime 'plugins/SVSCompute.dll'
if (-not (Test-Path -LiteralPath $computeClient -PathType Leaf)) {
    throw 'Deployed shared compute client missing.'
}
$computePackage = Join-Path $runtime 'svs/compute'
foreach ($file in Get-ChildItem -LiteralPath $computePackage -File -Recurse) {
    if ($file.Name -match '\.(pdb|lib|exp|disabled)$') {
        continue
    }
    $payload[$file.FullName.Substring($runtime.Length + 1).Replace('\', '/')] = $file.FullName
}
foreach ($required in @('SVSComputeWorker.exe', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'DirectML.dll', 'dependencies.lock.json', 'licenses/directml/LICENSE.txt', 'licenses/onnxruntime/LICENSE')) {
    if (-not $payload.ContainsKey("svs/compute/$required")) {
        throw "Incomplete shared compute runtime: $required"
    }
}
foreach ($directory in @('plugins', 'assets', 'generic', 'iconengines', 'imageformats', 'networkinformation', 'platforms', 'styles', 'tls')) {
    $folder = Join-Path $runtime $directory
    if (-not (Test-Path -LiteralPath $folder)) {
        continue
    }
    foreach ($file in Get-ChildItem -LiteralPath $folder -File -Recurse) {
        if ($file.Name -match '\.(disabled|pdb|lib|exp)$' -or $file.Name -like '*.disabled.*') {
            continue
        }
        $payload[$file.FullName.Substring($runtime.Length + 1).Replace('\', '/')] = $file.FullName
    }
}
# Project conversion runs in this isolated deployed runtime, not system Python.
$projectRuntime = Join-Path $runtime 'svs-project'
if (-not (Test-Path -LiteralPath $projectRuntime -PathType Container)) {
    throw 'Deployed SVS project runtime missing.'
}
foreach ($file in Get-ChildItem -LiteralPath $projectRuntime -File -Recurse) {
    if ($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -in @('.pyc', '.pdb', '.lib', '.exp')) {
        continue
    }
    $payload[$file.FullName.Substring($runtime.Length + 1).Replace('\', '/')] = $file.FullName
}
foreach ($required in @('bridge.py', 'formats.json', 'export-policy.json', 'runtime-lock.json', 'python/python.exe', 'python/python313.dll', 'python/python313.zip', 'licenses/LibreSVIP-LICENSE', 'licenses/CPython-LICENSE.txt')) {
    if (-not $payload.ContainsKey("svs-project/$required")) {
        throw "Incomplete SVS project runtime: $required"
    }
}
foreach ($name in @('formats.json', 'export-policy.json', 'runtime-lock.json')) {
    if ((Get-FileHash -LiteralPath $payload["svs-project/$name"]).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project "doc/svs/project/$name")).Hash) {
        throw "Stale deployed SVS project manifest: $name"
    }
}
if ((Get-FileHash -LiteralPath $payload['svs-project/bridge.py']).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'tools/svs-project/bridge.py')).Hash) {
    throw 'Stale deployed SVS project bridge.'
}
$payload['svs-project/usage.md'] = Join-Path $project 'doc/svs/project/usage.md'
$payload['LICENSE.txt'] = Join-Path $project 'LICENSE.txt'
$payload['README.md'] = Join-Path $project 'README.md'
$targets = Get-Content -LiteralPath (Join-Path $project 'doc/ui-modernization/validation/plugin-targets.txt')
if ($targets.Count -ne 52) {
    throw 'Frozen plugin target count differs.'
}
foreach ($target in $targets) {
    if (-not $payload.ContainsKey("plugins/$target.dll")) {
        throw "Missing enabled plugin: $target"
    }
}
foreach ($required in @('platforms/qwindows.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'data/themes/default/style.css', 'plugins/midiimport.dll', 'plugins/midiexport.dll', 'plugins/hydrogenimport.dll', 'plugins/vstbase.dll', 'plugins/RemoteCatalogIo.exe', 'plugins/RemoteVstHost64.exe', 'plugins/32/RemoteVstHost32.exe', 'plugins/RemoteVstPlugin64.exe', 'plugins/32/RemoteVstPlugin32.exe', 'plugins/RemoteZynAddSubFx.exe')) {
    if (-not $payload.ContainsKey($required)) {
        throw "Incomplete runtime: $required"
    }
}
foreach ($required in @('svs/SVSExample/SVSExample.dll', 'svs/SVSExample/manifest.json', 'svs/SVSExample/avatar.svg', 'svs/SVSExample/portrait.svg', 'data/themes/default/svs_track.svg')) {
    if (-not $payload.ContainsKey($required)) {
        throw "Incomplete SVS runtime: $required"
    }
}
foreach ($required in @('svs/SVSDiffSinger/SVSDiffSinger.dll', 'svs/SVSDiffSinger/onnxruntime.dll', 'svs/SVSDiffSinger/manifest.json', 'data/projects/templates/default.mpt')) {
    if (-not $payload.ContainsKey($required)) {
        throw "Incomplete DiffSinger/default template runtime: $required"
    }
}
if ((Get-FileHash -LiteralPath $payload['data/projects/templates/default.mpt']).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'data/projects/templates/default.mpt')).Hash) {
    throw 'Runtime default template differs from the official factory template.'
}
$records = @($payload.Keys | Sort-Object | ForEach-Object {
        $file = Get-Item -LiteralPath $payload[$_]
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Runtime file is a reparse point: $_"
        }
        [pscustomobject]@{Path = $_; Bytes = $file.Length; SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    })
$manifest = [ordered]@{Format = 1; ProductCommit = $ProductCommit; Platform = 'Windows x64'; EnabledUiPluginCount = 52; Files = $records }
if ($BaseManifest) {
    $base = Get-Content -LiteralPath $BaseManifest -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($base.Format -ne 1 -or $base.PackageKind -eq 'Incremental' -or -not $base.Files.Count) {
        throw 'An incremental package requires a full replacement package manifest.'
    }
    $baseline = @{}
    foreach ($entry in $base.Files) {
        if ($baseline.ContainsKey($entry.Path)) { throw "Duplicate baseline path: $($entry.Path)" }
        $baseline[$entry.Path] = $entry
        if (-not $payload.ContainsKey($entry.Path)) { throw "File removal requires a full package: $($entry.Path)" }
    }
    $records = @($records | Where-Object { -not $baseline.ContainsKey($_.Path) -or $_.SHA256 -ne $baseline[$_.Path].SHA256 })
    if (-not $records.Count) { throw 'No runtime changes relative to the full package.' }
    $manifest.Files = $records
    $manifest.PackageKind = 'Incremental'
    $manifest.BasePackage = [IO.Path]::GetFileName($BaseManifest).Replace('.manifest.json', '')
    $manifest.BaseProductCommit = $base.ProductCommit
    $manifest.BaseManifestSHA256 = (Get-FileHash -LiteralPath $BaseManifest -Algorithm SHA256).Hash
    $manifest.BaseFiles = @($base.Files)
}
$manifestText = $manifest | ConvertTo-Json -Depth 5
$instructions = @'
LMMS 增强分支全量替换包（Windows x64）

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
包含原生 DiffSinger CPU/DirectML 完整依赖文件夹、共享计算 worker 与许可；SVS 全局选择实际设备，按阶段显示执行后端。模型内存管理默认空闲 60 秒释放，也可立即释放或常驻；CPU/GPU 通用。SDK 保持旧 ABI 兼容。
包含官方默认工程模板：TripleOscillator、Sample track、Pattern 0、Automation track；Pattern Editor 包含 Kicker。
包含 SVS 工程导入导出菜单、隔离 CPython/LibreSVIP 运行时和第三方许可文本；有损格式会在写入前具名提示。
用户 templates/default.mpt 优先于官方模板。如果此前自行设置了空白模板，请先备份并停用该覆盖文件，再新建工程。
全部分支增强由 AI 辅助开发；本分支独立维护并同步上游更新。英文功能对比及原版 README 见 README.md。
Sid（缺 Perl）与 GigPlayer（缺 libgig）未构建。
本包包含 SVS 自动音素拉伸误写 null、音素时长限制修复与原版风格琴键；回归和真实窗口证据见当前分段渲染验收记录。
SVS 实窗专项使用实际部署插件与主题通过；最终操作体验和听感待人工验收。
本机测试头像、立绘及开发版个人配置未打包；示例使用自带 SVG 资源。

构建来源：https://github.com/AI-Hobbyist/lmms
源代码版本：7b44c5487187d241c2dece22630a573479129c44（F6）
工作区变更：__WORKTREE_STATUS__
包来自当前工作区构建；存在上述修改时，不称纯提交重建。
SVS 工程转换使用说明：svs-project/usage.md；完整验收记录：仓库 doc/svs/project/M4-validation.md。
jyutping 与 wanakana-python 上游许可声明复核仍为非阻塞 MANUAL/PENDING，已保留原始声明和来源。
其他证据：仓库 doc/svs/SVS-segment-rendering-validation.md、doc/ui-modernization/acceptance.md 与 delivery-audit.md。
许可文本：LICENSE.txt；第三方资源随其原有许可。请保留自己的原安装文件作为回滚来源。
'@
$instructions = $instructions.Replace('7b44c5487187d241c2dece22630a573479129c44（F6）', $ProductCommit)
if ($BaseManifest) {
    $instructions = @"
LMMS 增量覆盖包（Windows x64）

所需基包：$($manifest.BasePackage)
基包源码版本：$($manifest.BaseProductCommit)
当前源码版本：$ProductCommit
变更文件数：$($records.Count)

1. 先安装上述全量基包，然后关闭 LMMS。
2. 解压本包，双击 Install-Replace.cmd，选择包含 lmms.exe 的原安装目录。
   Program Files 目录需要以管理员身份运行。
3. 安装器校验基包未变更文件及增量文件 SHA256，仅覆盖本包列出的文件。
   原有插件、运行库、个人配置、工程和声库保持原位。

包含 SVS 导入/导出文件对话框的“所有支持格式”默认筛选及按扩展名识别。
此前已安装此增量时也可再次运行；校验失败时应重新安装全量基包。
工作区变更：__WORKTREE_STATUS__
包来自当前工作区构建；存在上述修改时，不称纯提交重建。
构建来源：https://github.com/AI-Hobbyist/lmms
"@
}
$worktreeStatus = @(& git -C $project status --short --untracked-files=no)
if ($LASTEXITCODE -ne 0) {
    throw 'Cannot identify worktree changes.'
}
$instructions = $instructions.Replace('__WORKTREE_STATUS__', $(if ($worktreeStatus.Count) {
            $worktreeStatus -join '; '
        }
        else {
            '无已跟踪文件修改'
        }))
$cmd = "@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0Install-Replace.ps1`"`r`npause`r`n"
$expected = @{}
$archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
function Add-TextEntry([string]$Name, [string]$Value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
    $entry = $archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $stream = $entry.Open()
    try {
        $stream.Write($bytes, 0, $bytes.Length)
    }
    finally {
        $stream.Dispose()
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $expected[$Name] = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
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
}
finally {
    $archive.Dispose()
}
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -ne $expected.Count) {
        throw 'Archive entry count differs.'
    }
    foreach ($entry in $archive.Entries) {
        if (-not $expected.ContainsKey($entry.FullName)) {
            throw "Unexpected archive entry: $($entry.FullName)"
        }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
        }
        finally {
            $stream.Dispose(); $sha.Dispose()
        }
        if ($hash -ne $expected[$entry.FullName]) {
            throw "Archive SHA256 mismatch: $($entry.FullName)"
        }
    }
}
finally {
    $archive.Dispose()
}
$manifestText | Set-Content -LiteralPath ($zipPath + '.manifest.json') -Encoding UTF8
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
"$hash  $([IO.Path]::GetFileName($zipPath))" | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding ASCII
Write-Output "Created and verified $($records.Count) runtime files + 4 package entries: $zipPath"
Write-Output "Bytes: $((Get-Item -LiteralPath $zipPath).Length); SHA256: $hash"
