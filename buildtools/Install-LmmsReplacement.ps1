param([string]$TargetDirectory, [string]$ArchivePath, [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-ContainedPath([string]$Root, [string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[/\\])\.\.([/\\]|$)') {
        throw "Invalid package path: $Relative"
    }
    $full = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $full.StartsWith($Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside installation: $Relative"
    }
    if (Test-Path -LiteralPath $full) {
        if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Reparse point in installation: $full"
        }
    }
    $parent = [IO.Path]::GetDirectoryName($full)
    while ($parent.Length -ge $Root.Length) {
        if (Test-Path -LiteralPath $parent) {
            if ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in installation: $parent"
            }
        }
        if ($parent -eq $Root) {
            break
        }
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
    return $full
}
function Get-StreamHash($Stream) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

$archive = $null
try {
    if ($ArchivePath) {
        $archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).Path)
        $entry = $archive.GetEntry('replacement-manifest.json')
        if (-not $entry) {
            throw 'Replacement manifest missing.'
        }
        $reader = [IO.StreamReader]::new($entry.Open(), [Text.Encoding]::UTF8)
        try {
            $manifest = $reader.ReadToEnd() | ConvertFrom-Json
        }
        finally {
            $reader.Dispose()
        }
    }
    else {
        $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'replacement-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'This package requires 64-bit Windows.'
    }
    if ($manifest.Format -ne 1 -or $manifest.Files.Count -lt 1) {
        throw 'Unsupported or empty replacement manifest.'
    }
    $names = @{}
    $incremental = $manifest.PackageKind -eq 'Incremental'
    if ($incremental -and (-not $manifest.BaseFiles.Count -or -not $manifest.BasePackage)) {
        throw 'Incremental package baseline missing.'
    }
    foreach ($file in $manifest.Files) {
        $name = [string]$file.Path
        if ($names.ContainsKey($name)) {
            throw "Duplicate file: $name"
        }
        $names[$name] = $true
        if ($name -match '(^|/)(portable_mode\.txt|\.lmmsrc.*|lmms-workspace)(/|$)') {
            throw 'Personal configuration must not be packaged.'
        }
        if ($archive) {
            $entry = $archive.GetEntry($name)
            if (-not $entry -or $entry.Length -ne $file.Bytes) {
                throw "Missing or invalid archive file: $name"
            }
            $stream = $entry.Open()
        }
        else {
            $source = Get-ContainedPath $PSScriptRoot $name
            if ((Get-Item -LiteralPath $source).Length -ne $file.Bytes) {
                throw "Invalid file size: $name"
            }
            $stream = [IO.File]::OpenRead($source)
        }
        try {
            if ((Get-StreamHash $stream) -ne $file.SHA256) {
                throw "Package SHA256 mismatch: $name"
            }
        }
        finally {
            $stream.Dispose()
        }
    }
    if (-not $incremental -and (-not $names.ContainsKey('lmms.exe') -or -not $names.ContainsKey('platforms/qwindows.dll'))) {
        throw 'Incomplete Windows runtime.'
    }
    if (-not $TargetDirectory) {
        $suggested = Join-Path $env:ProgramFiles 'lmms'
        $TargetDirectory = Read-Host "LMMS installation directory (Enter for $suggested)"
        if (-not $TargetDirectory) {
            $TargetDirectory = $suggested
        }
    }
    $target = [IO.Path]::GetFullPath($TargetDirectory.Trim('"')).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath (Join-Path $target 'lmms.exe') -PathType Leaf)) {
        throw 'Choose the existing directory containing lmms.exe.'
    }
    foreach ($file in $manifest.Files) {
        $null = Get-ContainedPath $target $file.Path
    }
    if ($incremental) {
        $baselineNames = @{}
        foreach ($file in $manifest.BaseFiles) {
            if ($baselineNames.ContainsKey($file.Path)) { throw "Duplicate baseline file: $($file.Path)" }
            $baselineNames[$file.Path] = $true
            $path = Get-ContainedPath $target $file.Path
            if ($names.ContainsKey($file.Path)) { continue }
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-Item -LiteralPath $path).Length -ne $file.Bytes -or
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.SHA256) {
                throw "Incremental baseline mismatch: $($file.Path). Install $($manifest.BasePackage) first."
            }
        }
        if (-not $baselineNames.ContainsKey('lmms.exe') -or -not $baselineNames.ContainsKey('platforms/qwindows.dll')) {
            throw 'Incomplete incremental baseline.'
        }
    }
    if ($VerifyOnly) {
        Write-Output "Verified $($manifest.Files.Count) payload files and installation paths. No changes made."; exit 0
    }
    foreach ($process in @(Get-Process -Name lmms -ErrorAction SilentlyContinue)) {
        if (-not $process.Path -or $process.Path.StartsWith($target + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Close LMMS before replacing files.'
        }
    }
    # Native LMMS plugins from another build can have an incompatible widget ABI.
    $pluginRoot = Join-Path $target 'plugins'
    if (-not $incremental -and (Test-Path -LiteralPath $pluginRoot)) {
        foreach ($old in Get-ChildItem -LiteralPath $pluginRoot -Filter '*.dll' -File -Recurse) {
            $relative = $old.FullName.Substring($target.Length + 1).Replace('\', '/')
            if (-not $names.ContainsKey($relative)) {
                $null = Get-ContainedPath $target $relative
                $retired = $old.FullName + '.disabled'
                if (Test-Path -LiteralPath $retired) {
                    $retired += '.' + [Guid]::NewGuid().ToString('N')
                }
                Move-Item -LiteralPath $old.FullName -Destination $retired
                Write-Output "Retired in place: $relative"
            }
        }
    }
    foreach ($file in $manifest.Files) {
        $destination = Get-ContainedPath $target $file.Path
        $parent = [IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $parent)) {
            $null = New-Item -ItemType Directory -Path $parent
        }
        if ($archive) {
            $inputStream = $archive.GetEntry($file.Path).Open()
            try {
                $outputStream = [IO.File]::Create($destination)
                try {
                    $inputStream.CopyTo($outputStream)
                }
                finally {
                    $outputStream.Dispose()
                }
            }
            finally {
                $inputStream.Dispose()
            }
        }
        else {
            $source = Get-ContainedPath $PSScriptRoot $file.Path
            if ($source -ne $destination) {
                Copy-Item -LiteralPath $source -Destination $destination -Force
            }
        }
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $file.SHA256) {
            throw "Installed SHA256 mismatch: $($file.Path)"
        }
    }
    Write-Output "Replaced and verified $($manifest.Files.Count) files in $target. Personal configuration and projects were preserved."
}
finally {
    if ($archive) {
        $archive.Dispose()
    }
}
