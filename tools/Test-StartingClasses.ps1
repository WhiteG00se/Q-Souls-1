param([string]$GamePath = 'D:\Games\Steam\steamapps\common\DARK SOULS REMASTERED')
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
. (Join-Path $repository 'launcher\Initialize-StartingClasses.ps1')
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\starting-classes' ([Guid]::NewGuid().ToString())
$source = Join-Path $GamePath 'param\GameParam\GameParam.parambnd.dcx'
if (-not (Test-Path -LiteralPath $source)) { throw "Missing game component: $source. Provide the correct path or install the game." }
$originalHash = (Get-FileHash -LiteralPath $source).Hash
Add-Type -Path (Join-Path $repository 'launcher\StartingClasses.cs')
$original = [IO.File]::ReadAllBytes($source)
$before = [StartingClasses]::Unpack($original)
$packed = [StartingClasses]::Patch($original)
$after = [StartingClasses]::Unpack($packed)
if ($before.Length -ne $after.Length) { throw 'Binder length changed.' }

# Independently inspect the output's row table and allow only the requested fields.
$allowed = [Collections.Generic.HashSet[int]]::new()
$classes = @('Warrior','Knight','Wanderer','Thief','Bandit','Hunter','Sorcerer','Pyromancer','Cleric','Deprived')
$classCount = 0
$deprivedRows = @()
for ($i = 0; $i -lt [BitConverter]::ToInt32($after, 16); $i++) {
    $entry = 32 + 24 * $i
    $offset = [BitConverter]::ToInt32($after, $entry + 8)
    if ([Text.Encoding]::ASCII.GetString($after, $offset + 12, 20) -ne "CHARACTER_INIT_PARAM`0") { continue }
    $rows = [BitConverter]::ToUInt16($after, $offset + 10)
    for ($j = 0; $j -lt $rows; $j++) {
        $row = $offset + 48 + 12 * $j
        $id = [BitConverter]::ToInt32($after, $row)
        if (($id -lt 2000 -or $id -gt 2009) -and ($id -lt 3000 -or $id -gt 3009)) { continue }
        $classIndex = $id % 1000
        $purpose = if ($id -lt 3000) { 'Character' } else { 'Menu' }
        $start = $offset + [BitConverter]::ToInt32($after, $row + 4)
        $level = [BitConverter]::ToInt16($after, $start + 0xc0)
        $stats = @(0xc2,0xc3,0xc4,0xc5,0xc6,0xcb,0xc7,0xc8 | ForEach-Object { [int]$after[$start + $_] })
        if ($level -ne 1 -or ($stats -join ',') -ne '12,10,10,10,10,10,10,10') {
            throw "Incorrect stats for $($classes[$classIndex])."
        }
        foreach ($field in @(0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xcb)) {
            [void]$allowed.Add($start + $field)
        }
        if ($classIndex -eq 9) { $deprivedRows += $row }
        $classCount++
        Write-Output "$purpose $($classes[$classIndex]): SL $level; VIT/ATT/END/STR/DEX/RES/INT/FAI = $($stats -join '/')"
    }
}
if ($classCount -ne 20) { throw 'Did not verify all ten character classes AND all ten menu previews.' }
$changed = 0
for ($i = 0; $i -lt $before.Length; $i++) {
    if ($before[$i] -ne $after[$i]) {
        if (-not $allowed.Contains($i)) { throw "Unrelated binder byte changed at $i." }
        $changed++
    }
}
if ([Convert]::ToBase64String([StartingClasses]::Unpack([StartingClasses]::Patch($packed))) -ne
    [Convert]::ToBase64String($after)) { throw 'Patch is not idempotent.' }

# Corrupt archives and missing/duplicate class rows must fail before writing.
$badChecksum = [byte[]]$original.Clone()
$badChecksum[$badChecksum.Length - 1] = $badChecksum[$badChecksum.Length - 1] -bxor 1
$badArchives = @($badChecksum, ([byte[]]@(1,2,3)))
foreach ($deprivedRow in $deprivedRows) {
    foreach ($replacement in @(-1, ([BitConverter]::ToInt32($before, $deprivedRow) - 1))) {
        $invalid = [byte[]]$before.Clone()
        [Array]::Copy([BitConverter]::GetBytes([int]$replacement), 0, $invalid, $deprivedRow, 4)
        $badArchives += ,([StartingClasses]::Pack($original, $invalid))
    }
}
foreach ($bad in $badArchives) {
    $rejected = $false
    try { [void][StartingClasses]::Patch($bad) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid archive accepted.' }
}

# Exercise a fresh runtime, repeat launch, legacy junction migration, and link refusal.
$runtime = Join-Path $temporary 'fresh'
Initialize-StartingClasses -GamePath $GamePath -RuntimeRoot $runtime -ValidateOnly
if (Test-Path -LiteralPath $runtime) { throw 'Check-only mode created runtime files.' }
Initialize-StartingClasses -GamePath $GamePath -RuntimeRoot $runtime
Initialize-StartingClasses -GamePath $GamePath -RuntimeRoot $runtime
$generated = Join-Path $runtime 'param\GameParam\GameParam.parambnd.dcx'
if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($generated)) -ne [Convert]::ToBase64String($packed)) {
    throw 'Generated override differs from verified archive.'
}
$legacy = Join-Path $temporary 'legacy'
New-Item -ItemType Directory -Path $legacy -Force | Out-Null
New-Item -ItemType Junction -Path (Join-Path $legacy 'param') -Target (Join-Path $GamePath 'param') | Out-Null
Initialize-StartingClasses -GamePath $GamePath -RuntimeRoot $legacy
if ((Get-Item -LiteralPath (Join-Path $legacy 'param')).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'Legacy parameter junction was not replaced.'
}
$unsafe = Join-Path $temporary 'unsafe'
New-Item -ItemType Directory -Path (Join-Path $unsafe 'param') -Force | Out-Null
New-Item -ItemType Junction -Path (Join-Path $unsafe 'param\GameParam') -Target (Join-Path $GamePath 'param\GameParam') | Out-Null
$rejected = $false
try { Initialize-StartingClasses -GamePath $GamePath -RuntimeRoot $unsafe } catch { $rejected = $true }
if (-not $rejected) { throw 'Nested runtime junction accepted.' }
if ((Get-FileHash -LiteralPath $source).Hash -ne $originalHash) { throw 'Installed game parameters changed.' }
Write-Output "PASS: all ten character classes and all ten menu previews; $changed requested byte changes; unrelated data preserved; compression round-trip; invalid archives; repeat generation; junction migration/refusal; installed source unchanged."
Write-Output "Test artifacts: $temporary"
