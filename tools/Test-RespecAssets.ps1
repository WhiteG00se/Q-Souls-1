param([string]$GamePath = 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\respec-assets' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -Path @((Join-Path $repository 'launcher\StartingClasses.cs'), (Join-Path $repository 'launcher\RespecAssets.cs'), (Join-Path $PSScriptRoot 'Test-RespecAssets.cs'))
$originals = @(Get-ChildItem -LiteralPath (Join-Path $GamePath 'script\talk'), (Join-Path $GamePath 'event'), (Join-Path $GamePath 'msg') -Recurse -File)
$originals += @(Get-Item -LiteralPath (Join-Path $GamePath 'menu\menu.drb'), (Join-Path $GamePath 'menu\menu.drb.dcx'))
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
foreach ($file in $originals) { if ($hashes[$file.FullName] -cne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Installed file changed: $($file.FullName)" } }
Write-Output "PASS: validate-only, external generation, repeat launch, manifest, junction refusal and unchanged installed game files. Artifacts: $temporary"
