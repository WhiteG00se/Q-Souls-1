$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\update-tests' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[void][Reflection.Assembly]::LoadFrom((Join-Path $repository '_launch-Q-Souls-1.exe'))
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Write-Fixture($path, $text) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
}
function Rejected($action, $message) {
    $rejected = $false
    try { & $action } catch { $rejected = $true }
    Assert $rejected $message
}
Assert ([RepositoryUpdates]::Newer('0.9', '0.10')) 'Version comparison is lexical.'
Assert (-not [RepositoryUpdates]::Newer('0.7', '0.4')) 'Version comparison allows downgrade.'
Assert (-not [RepositoryUpdates]::Newer('0.7', '0.7.0')) 'Equivalent versions differ.'
Assert ([RepositoryUpdates]::NormalizeCoopVersion("0.03 0`n>0.04 1`n") -eq '0.0.4') 'Seamless version code decoded incorrectly.'
$hashPath = Join-Path $temporary 'hash.txt'; Write-Fixture $hashPath 'abc'
Assert ([RepositoryUpdates]::BlobHash($hashPath) -eq 'f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f') 'Git blob hash is incorrect.'
foreach ($path in @('../escape', '/absolute', '.git/config', '.local/launcher.xml', 'SeamlessCoop_here/SeamlessCoop/ds1sc_settings.ini', 'bad:stream', 'CON.txt', 'folder/../escape', 'folder\escape')) {
    Rejected { [void][RepositoryUpdates]::Target($temporary, $path) } "Unsafe path accepted: $path"
}
function New-UpdateFixture($name) {
    $root = Join-Path $temporary $name
    $job = Join-Path $root ('.local\updates\' + [Guid]::NewGuid().ToString('N'))
    $stage = Join-Path $job 'files'
    $files = [Collections.Generic.List[RepoFile]]::new()
    $contents = [ordered]@{
        'data/new.txt' = 'new file'
        'data/old.txt' = 'new content'
        'launcher/Start-Q-Souls-1.ps1' = '# fixture, never executed'
        'runtime/QSoulsRuntime.dll' = 'fixture, never loaded'
        '_launch-Q-Souls-1.exe.config' = '<fixture />'
        '_launch-Q-Souls-1.exe' = 'fixture, never executed'
        'SeamlessCoop_here/.gitkeep' = ''
        'VERSION.txt' = '0.2'
    }
    foreach ($relative in $contents.Keys) {
        $path = Join-Path $stage $relative
        Write-Fixture $path $contents[$relative]
        $file = New-Object RepoFile
        $file.Path = $relative; $file.Sha = [RepositoryUpdates]::BlobHash($path); $file.Size = (Get-Item -LiteralPath $path).Length
        $files.Add($file)
    }
    Write-Fixture (Join-Path $root 'VERSION.txt') '0.1'
    Write-Fixture (Join-Path $root 'data/old.txt') 'original content'
    Write-Fixture (Join-Path $root 'launcher/Start-Q-Souls-1.ps1') '# original launcher'
    Write-Fixture (Join-Path $root '.local/launcher.xml') '<user-settings />'
    Write-Fixture (Join-Path $root 'SeamlessCoop_here/SeamlessCoop/ds1sc_settings.ini') 'user password and settings'
    Write-Fixture (Join-Path $root '.git/config') 'git metadata'
    Write-Fixture (Join-Path $root 'personal-notes.txt') 'user notes'
    $release = New-Object PublishedVersion
    $release.Version = '0.2'; $release.Commit = 'a' * 40; $release.Tree = 'b' * 40; $release.Files = $files
    $plan = New-Object UpdatePlan
    $plan.Repository = $root; $plan.Release = $release
    Write-Fixture (Join-Path $job 'plan.json') ([RepositoryUpdates]::Json().Serialize($plan))
    return @{ Root=$root; Job=$job; Stage=$stage; Release=$release }
}
$fixture = New-UpdateFixture 'success'
$result = [RepositoryUpdates]::Apply($fixture.Job)
Assert ($result.Contains('v0.2')) 'Update did not finish.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'data/old.txt')) -eq 'new content') 'Tracked file not updated.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root '.local/launcher.xml')) -eq '<user-settings />') 'Local settings overwritten.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'SeamlessCoop_here/SeamlessCoop/ds1sc_settings.ini')) -eq 'user password and settings') 'Optional mod overwritten.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root '.git/config')) -eq 'git metadata') 'Git metadata overwritten.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'personal-notes.txt')) -eq 'user notes') 'Untracked file overwritten.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Job 'backup/data/old.txt')) -eq 'original content') 'Backup missing original content.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Root '.local/installed-release.json')) 'Installed snapshot not recorded.'
Assert ([RepositoryUpdates]::VerifySnapshot($fixture.Root, $fixture.Release).Contains('All files match.')) 'Clean verification failed.'
Write-Fixture (Join-Path $fixture.Root 'data/old.txt') 'tampered'
$verification = [RepositoryUpdates]::VerifySnapshot($fixture.Root, $fixture.Release)
Assert ($verification.Contains('Changed: data/old.txt')) 'Verification missed changed file.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'data/old.txt')) -eq 'tampered') 'Verification wrote to disk.'
Rejected { [void][RepositoryUpdates]::Apply($fixture.Job) } 'Updater allowed same version again.'
$fixture = New-UpdateFixture 'rollback'
$lock = [IO.File]::Open((Join-Path $fixture.Root 'launcher/Start-Q-Souls-1.ps1'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { Rejected { [void][RepositoryUpdates]::Apply($fixture.Job) } 'Locked-file update unexpectedly succeeded.' } finally { $lock.Dispose() }
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'data/old.txt')) -eq 'original content') 'Rollback did not restore changed file.'
Assert (-not (Test-Path -LiteralPath (Join-Path $fixture.Root 'data/new.txt'))) 'Rollback did not remove new file.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'VERSION.txt')) -eq '0.1') 'Failed update changed version.'
$fixture = New-UpdateFixture 'obsolete'
$oldRelease = New-Object PublishedVersion
$oldRelease.Version = '0.1'; $oldRelease.Files = New-Object 'Collections.Generic.List[RepoFile]'
foreach ($name in @('old-unchanged.txt', 'old-customized.txt')) {
    $oldFile = Join-Path $fixture.Root $name; Write-Fixture $oldFile 'published old content'
    $record = New-Object RepoFile; $record.Path = $name; $record.Sha = [RepositoryUpdates]::BlobHash($oldFile)
    $oldRelease.Files.Add($record)
}
Write-Fixture (Join-Path $fixture.Root 'old-customized.txt') 'local user changes'
Write-Fixture (Join-Path $fixture.Root '.local/installed-release.json') ([RepositoryUpdates]::Json().Serialize($oldRelease))
[void][RepositoryUpdates]::Apply($fixture.Job)
Assert (-not (Test-Path -LiteralPath (Join-Path $fixture.Root 'old-unchanged.txt'))) 'Obsolete unchanged tracked file retained.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Job 'backup/old-unchanged.txt')) -eq 'published old content') 'Obsolete file not backed up.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'old-customized.txt')) -eq 'local user changes') 'Locally changed obsolete file removed.'
$fixture = New-UpdateFixture 'bad-hash'
Write-Fixture (Join-Path $fixture.Stage 'data/old.txt') 'tampered download'
Rejected { [void][RepositoryUpdates]::Apply($fixture.Job) } 'Staged checksum mismatch accepted.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'data/old.txt')) -eq 'original content') 'Invalid staged data changed install.'
$fixture = New-UpdateFixture 'zip'
$zipPath = Join-Path $temporary 'repository.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($fixture.Stage, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $true)
$extracted = Join-Path $temporary 'extracted'
[RepositoryUpdates]::Extract($zipPath, $extracted, $fixture.Release)
Assert ([RepositoryUpdates]::VerifySnapshot($extracted, $fixture.Release).Contains('All files match.')) 'Pinned archive extraction failed.'
$badZip = Join-Path $temporary 'bad.zip'
[IO.File]::Copy($zipPath, $badZip)
$archive = [IO.Compression.ZipFile]::Open($badZip, [IO.Compression.ZipArchiveMode]::Update)
try { [void]$archive.CreateEntry('files/../escape.txt') } finally { $archive.Dispose() }
Rejected { [RepositoryUpdates]::Extract($badZip, (Join-Path $temporary 'bad-extracted'), $fixture.Release) } 'Archive traversal accepted.'
# Version recording is tied to the installed DLL hash; replacing it invalidates the record.
$coopRoot = Join-Path $temporary 'coop-version'
Write-Fixture (Join-Path $coopRoot 'SeamlessCoop_here/SeamlessCoop/ds1sc.dll') 'fixture'
[RepositoryUpdates]::RecordCoopVersion($coopRoot, 'Seamless Co-op-899-0-0-4-123456.zip')
Assert ([RepositoryUpdates]::CoopInstalled($coopRoot) -eq 'v0.0.4') 'Installed co-op version was not recorded.'
Write-Fixture (Join-Path $coopRoot 'SeamlessCoop_here/SeamlessCoop/ds1sc.dll') 'different DLL'
Assert ([RepositoryUpdates]::CoopInstalled($coopRoot) -eq 'Unknown (installed)') 'Stale co-op version record was trusted.'
# DLL version strings are read without loading the DLL; certificate OIDs are ignored.
$embeddedRoot = Join-Path $temporary 'embedded-coop-version'
$embeddedDll = Join-Path $embeddedRoot 'SeamlessCoop_here/SeamlessCoop/ds1sc.dll'
$banner = "Dark Souls: Remastered Seamless Co-op mod by Yui [ YuiKeyNexus Version 2 ]`0"
Write-Fixture $embeddedDll ($banner + "`00.0.4`0" + "2.5.4.3`0")
Assert ([RepositoryUpdates]::CoopInstalled($embeddedRoot) -eq 'v0.0.4') 'Embedded DLL version was not detected.'
Write-Fixture $embeddedDll ($banner + "`00.0.4`0" + "0.0.4`0")
Assert ([RepositoryUpdates]::CoopInstalled($embeddedRoot) -eq 'v0.0.4') 'Duplicate copies of one version were rejected.'
Write-Fixture $embeddedDll ($banner + "`00.0.4`0" + "0.0.5`0")
Assert ([RepositoryUpdates]::CoopInstalled($embeddedRoot) -eq 'Unknown (installed)') 'Ambiguous embedded versions were guessed.'
Write-Fixture $embeddedDll ("`00.0.4`0")
Assert ([RepositoryUpdates]::CoopInstalled($embeddedRoot) -eq 'Unknown (installed)') 'Unidentified DLL supplied an embedded mod version.'
Write-Fixture $embeddedDll ($banner + "`02.5.4.3`0")
Assert ([RepositoryUpdates]::CoopInstalled($embeddedRoot) -eq 'Unknown (installed)') 'Certificate OID mistaken for the embedded mod version.'
# Same-version repairs replace only damaged files, including missing files.
$fixture = New-UpdateFixture 'repair'
foreach ($file in $fixture.Release.Files) {
    $target = Join-Path $fixture.Root $file.Path
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::Copy((Join-Path $fixture.Stage $file.Path), $target, $true)
}
Write-Fixture (Join-Path $fixture.Root 'data/old.txt') 'damaged content'
[IO.File]::Delete((Join-Path $fixture.Root 'data/new.txt'))
$plan = [RepositoryUpdates]::Json().Deserialize([IO.File]::ReadAllText((Join-Path $fixture.Job 'plan.json')), [UpdatePlan])
$plan.Repair = $true
Write-Fixture (Join-Path $fixture.Job 'plan.json') ([RepositoryUpdates]::Json().Serialize($plan))
Assert ([RepositoryUpdates]::DamagedFiles($fixture.Root, $fixture.Release).Count -eq 2) 'Damaged-file detection failed.'
$lock = [IO.File]::Open((Join-Path $fixture.Root '_launch-Q-Souls-1.exe'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { $result = [RepositoryUpdates]::Apply($fixture.Job) } finally { $lock.Dispose() }
Assert ($result.StartsWith('Repaired Q Souls 1 v0.2')) 'Same-version repair not applied.'
Assert ([RepositoryUpdates]::DamagedFiles($fixture.Root, $fixture.Release).Count -eq 0) 'Repair left damaged files.'
Assert (-not (Test-Path -LiteralPath (Join-Path $fixture.Job 'backup/_launch-Q-Souls-1.exe'))) 'Repair replaced an unchanged launcher.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Job 'backup/data/old.txt')) -eq 'damaged content') 'Repair did not back up the overwritten file.'
# Combined setup verification invokes only check-only mode and keeps game/music results.
$fixture = New-UpdateFixture 'verify-setup'
$stub = 'param([switch]$CheckOnly, [string]$CoopMode); if (-not $CheckOnly) { throw "Game launch forbidden in fixture" }; Write-Output "Game: fixture installation"; Write-Output "Saved music level: 5/10"'
Write-Fixture (Join-Path $fixture.Stage 'launcher/Start-Q-Souls-1.ps1') $stub
$entry = @($fixture.Release.Files | Where-Object { $_.Path -eq 'launcher/Start-Q-Souls-1.ps1' })[0]
$entry.Sha = [RepositoryUpdates]::BlobHash((Join-Path $fixture.Stage $entry.Path)); $entry.Size = (Get-Item -LiteralPath (Join-Path $fixture.Stage $entry.Path)).Length
foreach ($file in $fixture.Release.Files) {
    $target = Join-Path $fixture.Root $file.Path
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::Copy((Join-Path $fixture.Stage $file.Path), $target, $true)
}
$result = [LauncherWindow]::VerifySetup($fixture.Root, $false, $fixture.Release)
Assert ($result[0] -eq 'setup' -and $result[2] -eq 'Passed') 'Combined setup verification failed.'
Assert ($result[1].Contains('Game: fixture installation') -and $result[1].Contains('Saved music level: 5/10') -and $result[1].Contains('All files match.')) 'Combined output omitted a check.'
$bullets = [LauncherWindow]::BulletOutput($result[1])
foreach ($line in ($bullets -split "`r?`n")) { Assert ($line.StartsWith([char]0x2022 + ' ')) 'Output line is not a bullet.' }
Assert (($bullets -split "`r?`n").Count -ge 4) 'Output collapsed onto one line.'
Write-Fixture (Join-Path $fixture.Root 'VERSION.txt') '0.3'
$result = [LauncherWindow]::VerifySetup($fixture.Root, $false, $fixture.Release)
Assert ($result[2] -eq 'Failed' -and $result[1].Contains('Saved music level: 5/10') -and $result[1].Contains('has not been published')) 'Unpublished version hid the game/music results.'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.Root 'VERSION.txt')) -eq '0.3') 'Verification downgraded an unpublished version.'
Write-Output 'PASS: numeric versions, author version mapping, Git blob hashes, protected paths, tracked-file update, backups, ignored/untracked preservation, verification, same-version refusal, locked-file rollback, checksum rejection, archive extraction/traversal, hash-bound co-op version metadata, embedded DLL version detection and ambiguity handling. Fixture files only; no game launched.'
