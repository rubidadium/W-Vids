param(
    [Parameter(Mandatory)][string]$Makensis,
    [Parameter(Mandatory)][string]$ProbeExecutable,
    [Parameter(Mandatory)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $root) { throw 'Use a new, empty work directory for this test.' }
$package = Join-Path $root 'payload with spaces'
$cache = Join-Path $root 'cache with spaces'
$wrapper = Join-Path $root 'W Vids.exe'
New-Item -ItemType Directory -Force -Path "$package/platforms" | Out-Null
Copy-Item -LiteralPath $ProbeExecutable -Destination "$package/w-vids.exe"
foreach ($name in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll')) {
    Set-Content -LiteralPath (Join-Path $package $name) -Value 'fixture'
}
& "$PSScriptRoot/../packaging/build-wrapper.ps1" -PackageDirectory $package -OutputFile $wrapper `
    -Makensis $Makensis -TestCacheRoot $cache

function Start-Wrapper {
    $process = Start-Process -FilePath $wrapper -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(20000)) {
        $process.Kill()
        throw 'The launcher did not exit in time.'
    }
    if ($process.ExitCode -ne 0) { throw "Launcher exited with $($process.ExitCode)." }
}
function Wait-Launches([int]$Count) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $runtime = @(Get-ChildItem -LiteralPath $cache -Directory)
        if ($runtime.Count -eq 1) {
            $log = Join-Path $runtime[0].FullName 'launches.txt'
            if ((Test-Path -LiteralPath $log) -and @(Get-Content -LiteralPath $log).Count -eq $Count) {
                return $runtime[0].FullName
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected $Count child launches."
}
Start-Wrapper
$runtime = Wait-Launches 1
if (!(Test-Path -LiteralPath "$runtime/.ready")) { throw 'Missing completion marker.' }
$stamp = (Get-Item -LiteralPath "$runtime/w-vids.exe").LastWriteTimeUtc
Start-Sleep -Milliseconds 1100
Start-Wrapper
$null = Wait-Launches 2
if ((Get-Item -LiteralPath "$runtime/w-vids.exe").LastWriteTimeUtc -ne $stamp) {
    throw 'A cached launch unnecessarily rewrote the executable.'
}
Remove-Item -LiteralPath "$runtime/platforms/qwindows.dll"
Start-Wrapper
$null = Wait-Launches 3
if (!(Test-Path -LiteralPath "$runtime/platforms/qwindows.dll")) { throw 'Missing dependency was not repaired.' }
Remove-Item -LiteralPath "$runtime/.ready"
$first = Start-Process -FilePath $wrapper -WindowStyle Hidden -PassThru
Start-Wrapper
if (!$first.WaitForExit(20000)) { $first.Kill(); throw 'Concurrent launcher timed out.' }
if ($first.ExitCode -ne 0) { throw 'Concurrent launcher failed.' }
$null = Wait-Launches 5
Set-Content -LiteralPath "$package/Qt6Core.dll" -Value 'updated fixture'
& "$PSScriptRoot/../packaging/build-wrapper.ps1" -PackageDirectory $package -OutputFile $wrapper `
    -Makensis $Makensis -TestCacheRoot $cache
Start-Wrapper
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $newRuntime = @(Get-ChildItem -LiteralPath $cache -Directory | Where-Object FullName -ne $runtime)
    if ($newRuntime.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $newRuntime[0].FullName 'launches.txt'))) {
        break
    }
    Start-Sleep -Milliseconds 50
} while ([DateTime]::UtcNow -lt $deadline)
if ($newRuntime.Count -ne 1 -or !(Test-Path -LiteralPath (Join-Path $newRuntime[0].FullName 'launches.txt')) `
    -or @(Get-Content -LiteralPath "$runtime/launches.txt").Count -ne 5) {
    throw 'An updated payload must run from a new cache and preserve the old release.'
}
Write-Host 'PASS: first extraction, cached reuse, missing dependency repair, interrupted extraction, concurrent launches, paths with spaces, release isolation'
