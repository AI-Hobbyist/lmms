<#
.SYNOPSIS
    Fetches the portable aria2 and 7-Zip tools used by the built-in Agent chat
    and writes tools/manifest.json beside lmms.exe.

.DESCRIPTION
    Downloads only official release files, verifies their SHA256 values and
    copies the required files into <Destination>/aria2 and <Destination>/7zip.
    The 7-Zip installer is never run: it is a 7z self-extracting archive and is
    unpacked with the official standalone 7zr.exe.
#>
param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\build\Release\tools")
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$aria2 = @{
    Version = "1.37.0"
    Url     = "https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0-win-64bit-build1.zip"
    # The release publishes no digest; this value was recorded from the first verified download.
    Sha256  = "67d015301eef0b612191212d564c5bb0a14b5b9c4796b76454276a4d28d9b288"
    License = "GPL-2.0-or-later (COPYING)"
}
$sevenZip = @{
    Version = "26.06"
    Url     = "https://github.com/ip7z/7zip/releases/download/26.06/7z2606-x64.exe"
    Sha256  = "9b697f65ecc2d9b0152f5ee96d72dde522a7827690349d9307935dacf40f42ec"
    License = "GNU LGPL-2.1-or-later with the unRAR restriction and BSD parts (License.txt)"
}
$sevenZipReduced = @{
    Url    = "https://github.com/ip7z/7zip/releases/download/26.06/7zr.exe"
    Sha256 = "e80db2dad715a6c5adabe292d31ed54e11f4e9b99ca349827b50573b175891bc"
}

function Get-VerifiedFile {
    param([hashtable]$Source, [string]$Directory)

    $path = Join-Path $Directory ([IO.Path]::GetFileName($Source.Url))
    Write-Host "Downloading $($Source.Url)"
    Invoke-WebRequest -Uri $Source.Url -OutFile $path -UseBasicParsing
    $actual = (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLowerInvariant()
    if ($Source.Sha256 -and $actual -ne $Source.Sha256) {
        throw "SHA256 mismatch for $path`n expected $($Source.Sha256)`n actual   $actual"
    }
    if (-not $Source.Sha256) {
        Write-Warning "No pinned SHA256 for $($Source.Url); recorded actual value $actual"
    }
    Write-Host "  sha256 $actual"
    return @{ Path = $path; Sha256 = $actual }
}

function Get-FileEntry {
    param([string]$Root, [string]$RelativePath)

    $file = Join-Path $Root $RelativePath
    return [ordered]@{
        path   = $RelativePath.Replace('\', '/')
        size   = (Get-Item $file).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -Path $file).Hash.ToLowerInvariant()
    }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("lmms-agent-tools-" + [guid]::NewGuid().ToString("N"))
$downloads = Join-Path $work "downloads"
New-Item -ItemType Directory -Path $downloads | Out-Null

try {
    $aria2Archive = Get-VerifiedFile -Source $aria2 -Directory $downloads
    $sevenZipArchive = Get-VerifiedFile -Source $sevenZip -Directory $downloads
    $reducedTool = Get-VerifiedFile -Source $sevenZipReduced -Directory $downloads

    # aria2: the zip contains one versioned folder with aria2c.exe and its licence.
    $aria2Unpacked = Join-Path $work "aria2"
    Expand-Archive -Path $aria2Archive.Path -DestinationPath $aria2Unpacked
    $aria2Folder = (Get-ChildItem -Path $aria2Unpacked -Directory | Select-Object -First 1).FullName
    $aria2Target = Join-Path $Destination "aria2"
    New-Item -ItemType Directory -Force -Path $aria2Target | Out-Null
    foreach ($name in "aria2c.exe", "COPYING") {
        Copy-Item -Force -Path (Join-Path $aria2Folder $name) -Destination $aria2Target
    }

    # 7-Zip: unpack only the needed files from the installer archive with 7zr.exe.
    $sevenZipUnpacked = Join-Path $work "7zip"
    & $reducedTool.Path x -y "-o$sevenZipUnpacked" $sevenZipArchive.Path "7z.exe" "7z.dll" "License.txt" | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "7zr.exe failed to unpack $($sevenZipArchive.Path) (exit $LASTEXITCODE)"
    }
    $sevenZipTarget = Join-Path $Destination "7zip"
    New-Item -ItemType Directory -Force -Path $sevenZipTarget | Out-Null
    foreach ($name in "7z.exe", "7z.dll", "License.txt") {
        Copy-Item -Force -Path (Join-Path $sevenZipUnpacked $name) -Destination $sevenZipTarget
    }

    $root = (Resolve-Path $Destination).Path
    $manifest = [ordered]@{
        generated = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        tools     = @(
            [ordered]@{
                name    = "aria2"
                version = $aria2.Version
                url     = $aria2.Url
                sha256  = $aria2Archive.Sha256
                license = $aria2.License
                files   = @(
                    (Get-FileEntry -Root $root -RelativePath "aria2\aria2c.exe"),
                    (Get-FileEntry -Root $root -RelativePath "aria2\COPYING")
                )
            },
            [ordered]@{
                name          = "7zip"
                version       = $sevenZip.Version
                url           = $sevenZip.Url
                sha256        = $sevenZipArchive.Sha256
                license       = $sevenZip.License
                unpackedWith  = [ordered]@{ url = $sevenZipReduced.Url; sha256 = $reducedTool.Sha256 }
                files         = @(
                    (Get-FileEntry -Root $root -RelativePath "7zip\7z.exe"),
                    (Get-FileEntry -Root $root -RelativePath "7zip\7z.dll"),
                    (Get-FileEntry -Root $root -RelativePath "7zip\License.txt")
                )
            }
        )
    }
    $manifestPath = Join-Path $root "manifest.json"
    # UTF-8 without BOM so Qt can parse the manifest directly.
    $json = $manifest | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText($manifestPath, $json, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "Wrote $manifestPath"
}
finally {
    Remove-Item -Recurse -Force -Path $work -ErrorAction SilentlyContinue
}
