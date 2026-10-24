$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\installer-tests' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[void][Reflection.Assembly]::LoadFrom((Join-Path $repository '_launch-Q-Souls-1.exe'))
function Assert($condition, $message) { if (-not $condition) { throw $message } }
$ini = "[PASSWORD]`r`ncooppassword = default`r`n[SAVE]`r`nsave_file_extension = co2`r`n"
function New-Package($path, $prefix = '', $extra = '', $missing = $false, $locale = '{}') {
    $stream = [IO.File]::Open($path, [IO.FileMode]::Create)
    $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $entries = @{
            ($prefix + 'SeamlessCoop/ds1sc_settings.ini') = $ini
            ($prefix + 'SeamlessCoop/locale/english.json') = $locale
            ($prefix + 'ds1sc_launcher.exe') = 'fixture launcher, never executed'
        }
        if (-not $missing) { $entries[$prefix + 'SeamlessCoop/ds1sc.dll'] = 'fixture DLL, never loaded' }
        if ($extra) { $entries[$extra] = 'unsafe' }
        foreach ($name in $entries.Keys) {
            $entry = $zip.CreateEntry($name)
            $writer = New-Object IO.StreamWriter($entry.Open())
            try { $writer.Write($entries[$name]) } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose(); $stream.Dispose() }
}
function Rejected($root, $archive, $message) {
    $rejected = $false
    try { [void][SeamlessInstaller]::Install($root, $archive) } catch { $rejected = $true }
    Assert $rejected $message
}
$root = Join-Path $temporary 'repo'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$zip = Join-Path $temporary 'valid.zip'
New-Package $zip
[void][SeamlessInstaller]::Install($root, $zip)
$installed = Join-Path $root 'SeamlessCoop_here\SeamlessCoop'
Assert (Test-Path -LiteralPath (Join-Path $installed 'ds1sc.dll')) 'Fresh install missing DLL.'
Assert ([IO.File]::ReadAllText((Join-Path $installed 'ds1sc_settings.ini')) -ceq $ini) 'Fresh install INI changed.'
$custom = $ini.Replace('default', 'my existing password').Replace('co2', 'custom')
[IO.File]::WriteAllText((Join-Path $installed 'ds1sc_settings.ini'), $custom)
[IO.File]::WriteAllText((Join-Path $installed 'user-note.txt'), 'preserve me')
[IO.File]::WriteAllText((Join-Path $installed 'ds1sc.dll'), 'previous DLL')
$wrapped = Join-Path $temporary 'wrapped.zip'
New-Package $wrapped 'download-v0.0.4/'
$result = [SeamlessInstaller]::Install($root, $wrapped)
Assert ($result.Contains('Previous installation:')) 'Backup location not reported.'
Assert ([IO.File]::ReadAllText((Join-Path $installed 'ds1sc_settings.ini')) -ceq $custom) 'Update overwrote user settings.'
Assert ([IO.File]::ReadAllText((Join-Path $installed 'user-note.txt')) -ceq 'preserve me') 'Update removed user file.'
$backups = @(Get-ChildItem -LiteralPath (Join-Path $root '.local\seamless-backups') -Directory)
Assert ($backups.Count -eq 1) 'Missing installation backup.'
Assert ([IO.File]::ReadAllText((Join-Path $backups[0].FullName 'SeamlessCoop\ds1sc.dll')) -eq 'previous DLL') 'Backup did not preserve old DLL.'
$before = (Get-FileHash -LiteralPath (Join-Path $installed 'ds1sc.dll')).Hash
foreach ($unsafe in @('../escape.txt', '/absolute.txt', 'SeamlessCoop/CON.txt', 'SeamlessCoop/bad:stream', 'SeamlessCoop/../escape.txt')) {
    $bad = Join-Path $temporary 'unsafe.zip'
    New-Package $bad '' $unsafe
    Rejected $root $bad "Unsafe path accepted: $unsafe"
}
$bad = Join-Path $temporary 'missing.zip'; New-Package $bad '' '' $true
Rejected $root $bad 'Incomplete package accepted.'
$bad = Join-Path $temporary 'wrong-locale.zip'; New-Package $bad '' '' $false 'not JSON'
Rejected $root $bad 'Invalid locale accepted.'
$bad = Join-Path $temporary 'duplicate.zip'; New-Package $bad
$archive = [IO.Compression.ZipFile]::Open($bad, [IO.Compression.ZipArchiveMode]::Update)
try { [void]$archive.CreateEntry('SeamlessCoop/DS1SC.dll') } finally { $archive.Dispose() }
Rejected $root $bad 'Case-insensitive duplicate accepted.'
$bad = Join-Path $temporary 'symlink.zip'; New-Package $bad
$archive = [IO.Compression.ZipFile]::Open($bad, [IO.Compression.ZipArchiveMode]::Update)
try { $entry = $archive.CreateEntry('SeamlessCoop/link'); $entry.ExternalAttributes = -1577058304 } finally { $archive.Dispose() }
Rejected $root $bad 'Archive link accepted.'
Assert ((Get-FileHash -LiteralPath (Join-Path $installed 'ds1sc.dll')).Hash -eq $before) 'Rejected package changed installed files.'
Assert ([IO.File]::ReadAllText((Join-Path $installed 'ds1sc_settings.ini')) -ceq $custom) 'Rejected package changed user settings.'
Assert (@(Get-ChildItem -LiteralPath (Join-Path $root '.local') -Directory -Filter 'seamless-install-*').Count -eq 0) 'Staging files left behind.'
Assert (-not (Test-Path -LiteralPath (Join-Path $root 'escape.txt'))) 'ZIP escaped destination.'
$fakeGame = Join-Path $temporary 'game'
New-Item -ItemType Directory -Path $fakeGame | Out-Null
[IO.File]::WriteAllText((Join-Path $fakeGame 'DarkSoulsRemastered.exe'), 'fixture, never run')
Rejected $fakeGame $zip 'Installer allowed writing inside a game directory.'
Assert (-not (Test-Path -LiteralPath (Join-Path $fakeGame 'SeamlessCoop_here'))) 'Game directory was changed.'
Write-Output 'PASS: fresh and wrapped installs, settings preservation, backups, unsafe paths, duplicate entries, links, incomplete packages, invalid JSON, failed-install preservation, staging cleanup, game-directory guard. Fixture files only; no game launched.'
