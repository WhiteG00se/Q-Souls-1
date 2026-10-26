param([string]$GamePath = 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\respec-assets' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -Path @((Join-Path $repository 'launcher\StartingClasses.cs'), (Join-Path $repository 'launcher\RespecAssets.cs'), (Join-Path $repository 'launcher\QualityAssets.cs'), (Join-Path $repository 'launcher\HellAssets.cs'), (Join-Path $PSScriptRoot 'Test-RespecAssets.cs')) -ReferencedAssemblies @('System.dll', 'System.Core.dll', 'System.Xml.dll')
$originals = @(Get-ChildItem -LiteralPath (Join-Path $GamePath 'script\talk'), (Join-Path $GamePath 'event'), (Join-Path $GamePath 'msg') -Recurse -File)
$originals += @(Get-Item -LiteralPath (Join-Path $GamePath 'menu\menu.drb'), (Join-Path $GamePath 'menu\menu.drb.dcx'))
$originals += @(Get-Item -LiteralPath (Join-Path $GamePath 'map\MapStudio\m10_02_00_00.msb'), (Join-Path $GamePath 'map\MapStudio\m14_01_00_00.msb'))
$originals += @(Get-ChildItem -LiteralPath (Join-Path $GamePath 'map\m14_01_00_00') -File | Where-Object { $_.Name -match '^[hl]14_01_00_00\.hkx(bhd|bdt)$|^m(0010|0170)B1A14\.flver\.dcx$' })
foreach ($relative in @('map\m10\GI_EnvM_m10.tpfbhd', 'map\m10\GI_EnvM_m10.tpfbdt', 'map\m14\GI_EnvM_m14.tpfbhd', 'map\m14\GI_EnvM_m14.tpfbdt', 'map\m14\m14_9999.tpf.dcx', 'map\m10\m10_9999.tpf.dcx', 'map\m12\m12_9999.tpf.dcx', 'map\m12_00_00_00\m9000B0A12.flver.dcx',
    'map\MapStudio\m12_00_00_01.msb', 'param\DrawParam\a10_DrawParam.parambnd.dcx', 'param\DrawParam\a14_DrawParam.parambnd.dcx',
    'map\m14_01_00_00\m14_01_00_00.mcp', 'map\m14_01_00_00\m14_01_00_00.mcg', 'map\m14_01_00_00\m14_01_00_00.nvmbnd.dcx',
    'map\m14_01_00_00\m0100B1A14.flver.dcx')) {
    $originals += Get-Item -LiteralPath (Join-Path $GamePath $relative)
}
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
$generatedHashes = @{}
foreach ($relative in [IO.File]::ReadAllLines($manifest)) { $generatedHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $fixture ".local\respec\$relative")).Hash }
Initialize-Respec -GamePath $GamePath -Repository $fixture
if ($before -cne [IO.File]::ReadAllText($manifest)) { throw 'Repeat launch changed manifest.' }
foreach ($relative in $generatedHashes.Keys) {
    if ($generatedHashes[$relative] -cne (Get-FileHash -LiteralPath (Join-Path $fixture ".local\respec\$relative")).Hash) { throw "Repeat launch changed generated asset: $relative" }
}
$recipe = Join-Path $repository 'assets\hell\terrain.xml'
$badRecipe = Join-Path $temporary 'invalid.xml'
[xml]$invalidRecipe = [IO.File]::ReadAllText($recipe)
$invalidRecipe.hell.sha256 = 'invalid'
$invalidRecipe.Save($badRecipe)
$rejected = $false
try { [HellAssets]::Reconstruct($GamePath, $badRecipe) | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'Corrupt arena recipe accepted.' }
[xml]$invalidRecipe = [IO.File]::ReadAllText($recipe)
$invalidRecipe.hell.source[0].path = '..\outside.flver.dcx'
$invalidRecipe.Save($badRecipe)
$rejected = $false
try { [HellAssets]::Reconstruct($GamePath, $badRecipe) | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'Arena source traversal accepted.' }
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
