[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$OutputFile,
    [string]$Makensis = 'makensis.exe',
    [string]$TestCacheRoot
)

$ErrorActionPreference = 'Stop'
$payload = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\')
$output = [IO.Path]::GetFullPath($OutputFile)
if ($output.StartsWith($payload + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The wrapper output must be outside the payload directory.'
}
foreach ($required in @('w-vids.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms\qwindows.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $payload $required) -PathType Leaf)) {
        throw "The deployed package is missing $required. Run windeployqt before packaging."
    }
}

# Hash every deployed file, including plugins: updates never overwrite a running release.
$files = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName)
$manifest = foreach ($file in $files) {
    $relative = $file.FullName.Substring($payload.Length + 1)
    if ($relative -match '[\r\n]' -or $relative -eq '.ready') { throw "Invalid payload path: $relative" }
    '{0} {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash, $relative
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $id = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($manifest -join "`n")) |
        ForEach-Object { $_.ToString('x2') })
} finally { $sha.Dispose() }

function ConvertTo-NsisLiteral([string]$Value) {
    if ($Value -match '[\r\n]') { throw 'NSIS paths cannot contain newlines.' }
    $Value.Replace('$', '$$').Replace('"', '$\"')
}

New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($output)) | Out-Null
$generated = Join-Path ([IO.Path]::GetDirectoryName($output)) ('.wrapper-' + [guid]::NewGuid().ToString('N') + '.nsi')
$lines = @(
    '!define PAYLOAD_ID "' + $id + '"'
    '!define PAYLOAD_DIR "' + (ConvertTo-NsisLiteral $payload) + '"'
    '!define OUTPUT_FILE "' + (ConvertTo-NsisLiteral $output) + '"'
    '!define APP_ICON "' + (ConvertTo-NsisLiteral (Join-Path $PSScriptRoot '..\asset\icon.ico')) + '"'
)
if ($TestCacheRoot) {
    $lines += '!define TEST_CACHE_ROOT "' + (ConvertTo-NsisLiteral ([IO.Path]::GetFullPath($TestCacheRoot))) + '"'
}
$lines += '!macro CheckPayload'
foreach ($file in $files) {
    $relative = ConvertTo-NsisLiteral ($file.FullName.Substring($payload.Length + 1))
    $lines += 'IfFileExists "$INSTDIR\' + $relative + '" 0 unpack'
}
$lines += '!macroend'
$lines += '!include "' + (ConvertTo-NsisLiteral (Join-Path $PSScriptRoot 'launcher.nsi')) + '"'
try {
    [IO.File]::WriteAllLines($generated, $lines, [Text.UTF8Encoding]::new($true))
    & $Makensis /V2 $generated
    if ($LASTEXITCODE -ne 0) { throw "NSIS failed with exit code $LASTEXITCODE." }
} finally {
    Remove-Item -LiteralPath $generated -Force -ErrorAction SilentlyContinue
}
Write-Host "Built $output (runtime $id)"
