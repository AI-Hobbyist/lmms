param([string]$Destination = "$PSScriptRoot/../build/_deps/diffsinger-pronunciation")
$ErrorActionPreference = 'Stop'
try {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $sources = @(
        @{ Name='pinyin.txt'; Repo='pinyin-data'; Revision='9193766130af24d2ac54230be979b2e98ac66223'; File='pinyin.txt'; Hash='621f8ca9eff8519f47e2b17b564fd318161e13bca07eea8c8e04993cd5d3b52e' },
        @{ Name='phrases.txt'; Repo='phrase-pinyin-data'; Revision='cee0ed6e6e4898580cafd2bd5e3723e20b214aa0'; File='pinyin.txt'; Hash='dcc769607c220b312fea3e71cb63421298b4b891b1f7356a95ab58f2c96fff81' },
        @{ Name='LICENSE-pinyin-data'; Repo='pinyin-data'; Revision='9193766130af24d2ac54230be979b2e98ac66223'; File='LICENSE'; Hash='9c048697be2502a16e8bcb282d5d465a07295b2def0ffb05a269c5d39dbe1586' },
        @{ Name='LICENSE-phrase-pinyin-data'; Repo='phrase-pinyin-data'; Revision='cee0ed6e6e4898580cafd2bd5e3723e20b214aa0'; File='LICENSE'; Hash='89ac55df747e4776088c3e77531ef61b973a1a59dd8e6a4548a58996da9a4f70' }
    )
    foreach ($source in $sources) {
        $path = Join-Path $Destination $source.Name
        if (!(Test-Path -LiteralPath $path)) {
            Invoke-WebRequest "https://raw.githubusercontent.com/mozillazg/$($source.Repo)/$($source.Revision)/$($source.File)" -OutFile $path
        }
        $actualHash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $source.Hash) { throw "Frozen pronunciation data hash mismatch: $($source.Name)" }
        Write-Output "$($source.Name) SHA256=$actualHash bytes=$((Get-Item -LiteralPath $path).Length)"
    }
    exit 0
} catch { Write-Error $_; exit 1 }
