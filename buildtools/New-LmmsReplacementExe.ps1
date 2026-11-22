param(
    [Parameter(Mandatory)][string]$ArchivePath,
    [switch]$Production
)
$ErrorActionPreference = 'Stop'
$zip = (Resolve-Path -LiteralPath $ArchivePath).Path
$exe = [IO.Path]::ChangeExtension($zip, '.exe')
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$launcher = Join-Path (Split-Path -Parent $zip) ("lmms-launcher-" + [Guid]::NewGuid().ToString('N') + '.exe')
$compilerArguments = @(
    '/nologo', '/target:exe', '/optimize+', '/platform:x64', "/out:$launcher"
    '/reference:System.IO.Compression.dll', '/reference:System.IO.Compression.FileSystem.dll'
    '/reference:System.Windows.Forms.dll', (Join-Path $PSScriptRoot 'LmmsReplacementLauncher.cs')
)
if ($Production) {
    $compilerArguments += '/define:LMMS_PRODUCTION_REPLACEMENT'
}
try {
    & $compiler @compilerArguments
    if ($LASTEXITCODE -ne 0) { throw 'Replacement launcher compilation failed.' }
    & python (Join-Path $PSScriptRoot 'package_replacement_exe.py') --launcher $launcher --zip $zip --output $exe
    if ($LASTEXITCODE -ne 0) { throw 'Self-extracting replacement packaging failed.' }
}
finally {
    if (Test-Path -LiteralPath $launcher) {
        Remove-Item -LiteralPath $launcher
    }
}
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
"$hash  $([IO.Path]::GetFileName($exe))" | Set-Content -LiteralPath "$exe.sha256" -Encoding ASCII
Write-Output "Self-extracting replacement SHA256: $hash"
