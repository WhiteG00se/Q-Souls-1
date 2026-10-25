function Initialize-Respec {
    param([string]$GamePath, [string]$Repository, [switch]$ValidateOnly)
    if (-not ('RespecAssets' -as [type])) {
        # Compile together: Add-Type assemblies are not persisted as third-party dependencies.
        Add-Type -Path @((Join-Path $PSScriptRoot 'StartingClasses.cs'), (Join-Path $PSScriptRoot 'RespecAssets.cs'), (Join-Path $PSScriptRoot 'HellAssets.cs')) -ReferencedAssemblies @('System.dll', 'System.Core.dll', 'System.Xml.dll')
    }
    $bonfires = 0
    $files = [RespecAssets]::Generate($GamePath, [ref]$bonfires, (Join-Path $PSScriptRoot '..\assets\text\RespecLocales.inc'))
    [HellAssets]::Add($GamePath, (Join-Path $PSScriptRoot '..\assets\hell'), $files)
    Write-Host "Respec: $bonfires bonfire scripts validated. Cursed Arena assets validated."
    if ($ValidateOnly) { return }
    $root = [IO.Path]::GetFullPath((Join-Path $Repository '.local\respec')).TrimEnd('\')
    $game = [IO.Path]::GetFullPath($GamePath).TrimEnd('\')
    if ($root.Equals($game, [StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Respec overrides must be outside the game directory.'
    }
    # Check every destination before writing anything; never traverse a junction.
    foreach ($relative in @('overrides.txt') + @($files.Keys)) {
        $path = [IO.Path]::GetFullPath((Join-Path $root $relative))
        if (-not $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid Respec override path.' }
        while ($path) {
            if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Respec override path must not contain links: $path"
            }
            $path = Split-Path -Parent $path
        }
    }
    foreach ($relative in $files.Keys) {
        $target = Join-Path $root $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllBytes($target, $files[$relative])
    }
    [IO.File]::WriteAllLines((Join-Path $root 'overrides.txt'), [string[]]@($files.Keys | Sort-Object), [Text.Encoding]::ASCII)
}
