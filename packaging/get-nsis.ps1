[CmdletBinding()]
param([Parameter(Mandatory)][string]$DestinationDirectory)

$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($DestinationDirectory)
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$archive = Join-Path $destination 'nsis-3.12.zip'
$expectedHash = '56581F90DB321581C5381193D796FFFCF2D24B2F8FED2160A6C6A3BAA67F2C4F'
$urls = @(
    'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.12/nsis-3.12.zip'
    'https://prdownloads.sourceforge.net/nsis/nsis-3.12.zip'
    'https://phoenixnap.dl.sourceforge.net/project/nsis/NSIS%203/3.12/nsis-3.12.zip'
)

foreach ($url in $urls) {
    Write-Host "Downloading NSIS 3.12 from $url"
    # Invoke-WebRequest can receive SourceForge's HTML download page with HTTP 200.
    # Use the same curl client used by the local packaging build, and verify every mirror.
    & curl.exe --disable --fail --location --silent --show-error --retry 2 `
        --connect-timeout 20 --max-time 120 --proto '=https' --proto-redir '=https' `
        --output $archive --url $url
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "NSIS download failed (curl exit $LASTEXITCODE); trying the next URL."
        continue
    }
    $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash) {
        $size = (Get-Item -LiteralPath $archive).Length
        Write-Warning "NSIS response failed verification ($size bytes; SHA256 $actualHash; expected $expectedHash). It may be an HTML download page. Trying the next URL."
        continue
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    $compiler = Join-Path $destination 'nsis-3.12/makensis.exe'
    if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw 'The verified NSIS archive did not contain makensis.exe.'
    }
    $global:LASTEXITCODE = 0
    Write-Output $compiler
    return
}
throw 'All NSIS download URLs failed. See the download errors and checksums above; no unverified compiler was extracted.'
