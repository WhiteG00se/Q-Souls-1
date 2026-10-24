param([string]$GamePath = 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\respec-assets' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -Path @((Join-Path $repository 'launcher\StartingClasses.cs'), (Join-Path $repository 'launcher\RespecAssets.cs'), (Join-Path $PSScriptRoot 'Test-RespecAssets.cs'))
$originals = @(Get-ChildItem -LiteralPath (Join-Path $GamePath 'script\talk'), (Join-Path $GamePath 'event'), (Join-Path $GamePath 'msg') -Recurse -File)
$hashes = @{}
foreach ($file in $originals) { $hashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
[RespecAssetChecks]::Run($GamePath)
. (Join-Path $repository 'launcher\Initialize-Respec.ps1')
$fixture = Join-Path $temporary 'repository'
Initialize-Respec -GamePath $GamePath -Repository $fixture -ValidateOnly
if (Test-Path -LiteralPath $fixture) { throw 'Validation wrote files.' }
Initialize-Respec -GamePath $GamePath -Repository $fixture
$manifest = Join-Path $fixture '.local\respec\overrides.txt'
$before = [IO.File]::ReadAllText($manifest)
Initialize-Respec -GamePath $GamePath -Repository $fixture
if ($before -cne [IO.File]::ReadAllText($manifest)) { throw 'Repeat launch changed manifest.' }
foreach ($relative in [IO.File]::ReadAllLines($manifest)) {
    if (-not (Test-Path -LiteralPath (Join-Path $fixture ".local\respec\$relative") -PathType Leaf)) { throw 'Manifest target missing.' }
}
$unsafe = Join-Path $temporary 'unsafe'
New-Item -ItemType Directory -Path (Join-Path $unsafe '.local') -Force | Out-Null
New-Item -ItemType Junction -Path (Join-Path $unsafe '.local\respec') -Target $GamePath | Out-Null
$rejected = $false
try { Initialize-Respec -GamePath $GamePath -Repository $unsafe } catch { $rejected = $true }
if (-not $rejected) { throw 'Linked output destination accepted.' }
# A minimal installed-source fixture exercises event-flag collision detection.
$collision = Join-Path $temporary 'collision-game'
foreach ($folder in @('event','script\talk','msg\ENGLISH')) { New-Item -ItemType Directory -Path (Join-Path $collision $folder) -Force | Out-Null }
$talkSource = Join-Path $GamePath 'script\talk\m10_00_00_00.talkesdbnd.dcx'
Copy-Item -LiteralPath $talkSource -Destination (Join-Path $collision 'script\talk\m10_00_00_00.talkesdbnd.dcx')
Copy-Item -LiteralPath (Join-Path $GamePath 'msg\ENGLISH\menu.msgbnd.dcx') -Destination (Join-Path $collision 'msg\ENGLISH\menu.msgbnd.dcx')
$packed = [StartingClasses]::Pack([IO.File]::ReadAllBytes($talkSource), [BitConverter]::GetBytes([int][RespecAssets]::Ready))
[IO.File]::WriteAllBytes((Join-Path $collision 'event\collision.emevd.dcx'), $packed)
$rejected = $false; $count = 0
try { [void][RespecAssets]::Generate($collision, [ref]$count) } catch { $rejected = $_.Exception.Message -match 'event flag collision' }
if (-not $rejected) { throw 'Occupied event flag accepted.' }
foreach ($file in $originals) { if ($hashes[$file.FullName] -cne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Installed file changed: $($file.FullName)" } }
Write-Output "PASS: validate-only, external generation, repeat launch, manifest, junction refusal, event-flag collision and unchanged installed game files. Artifacts: $temporary"
