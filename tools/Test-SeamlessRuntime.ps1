$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\launcher\Initialize-SeamlessRuntime.ps1')
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\seamless-runtime-tests' ([Guid]::NewGuid().ToString('N'))
$optional = Join-Path $temporary 'SeamlessCoop_here'
$package = Join-Path $optional 'SeamlessCoop'
$runtime = Join-Path $temporary '.local\game'
New-Item -ItemType Directory -Path $package -Force | Out-Null
$settings = Join-Path $package 'ds1sc_settings.ini'
[IO.File]::WriteAllText($settings, "[SAVE]`r`nsave_file_extension = co2`r`n")
Initialize-SeamlessRuntime -OptionalRoot $optional -RuntimeRoot $runtime
foreach ($extension in @('sl2', 'co2', 'mySave9')) {
    [IO.File]::WriteAllText($settings, "[SAVE]`r`nsave_file_extension = $extension`r`n")
    Initialize-SeamlessRuntime -OptionalRoot $optional -RuntimeRoot $runtime
    $actual = [IO.File]::ReadAllText((Join-Path $runtime 'SeamlessCoop\ds1sc_settings.ini'))
    if ($actual -cne [IO.File]::ReadAllText($settings)) { throw "Runtime settings differ for $extension" }
}
$legacy = Join-Path $temporary 'legacy'
$crashes = Join-Path $legacy 'SeamlessCoop\crashdumps'
New-Item -ItemType Directory -Path $crashes -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $crashes 'fixture.dmp'), 'preserve crash dump')
Initialize-SeamlessRuntime -OptionalRoot $optional -RuntimeRoot $legacy
Initialize-SeamlessRuntime -OptionalRoot $optional -RuntimeRoot $legacy
$backups = @(Get-ChildItem -LiteralPath $legacy -Directory -Filter 'SeamlessCoop.backup-*')
if ($backups.Count -ne 1 -or [IO.File]::ReadAllText((Join-Path $backups[0].FullName 'crashdumps\fixture.dmp')) -ne 'preserve crash dump') {
    throw 'Legacy crash dumps were not preserved exactly once.'
}
if ([IO.File]::ReadAllText((Join-Path $legacy 'SeamlessCoop\ds1sc_settings.ini')) -cne [IO.File]::ReadAllText($settings)) {
    throw 'Migrated runtime does not read selected settings.'
}
$blocked = Join-Path $temporary 'blocked'
$wrong = Join-Path $temporary 'wrong-package'
New-Item -ItemType Directory -Path $blocked, $wrong -Force | Out-Null
New-Item -ItemType Junction -Path (Join-Path $blocked 'SeamlessCoop') -Target $wrong | Out-Null
$rejected = $false
try { Initialize-SeamlessRuntime -OptionalRoot $optional -RuntimeRoot $blocked } catch { $rejected = $true }
if (-not $rejected) { throw 'Unexpected runtime junction was accepted.' }
Write-Output 'PASS: live sl2/co2/custom settings; legacy runtime folder migration preserves crash dumps; repeated setup succeeds; wrong junction rejected. Fixtures only; no game launched.'
