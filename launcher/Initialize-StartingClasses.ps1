function Initialize-StartingClasses {
    param([string]$GamePath, [string]$RuntimeRoot, [switch]$ValidateOnly)
    $game = [IO.Path]::GetFullPath($GamePath).TrimEnd('\')
    $root = [IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
    if ($root.Equals($game, [StringComparison]::OrdinalIgnoreCase) -or
        $root.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Runtime data must be outside the game directory.'
    }
    # Never write through a junction in the runtime root or any of its parents.
    $ancestor = $root
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Runtime parent must be a real directory: $ancestor"
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    $source = Join-Path $game 'param\GameParam\GameParam.parambnd.dcx'
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing game component: $source. Install the game or provide the correct path."
    }
    if (-not ('StartingClasses' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'StartingClasses.cs') }
    $patched = [StartingClasses]::Patch([IO.File]::ReadAllBytes($source))
    if ($ValidateOnly) { return }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $paramRoot = Join-Path $root 'param'
    if (Test-Path -LiteralPath $paramRoot) {
        $existing = Get-Item -LiteralPath $paramRoot -Force
        if ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            $expected = Join-Path $game 'param'
            if ($existing.LinkType -ne 'Junction' -or
                -not ([IO.Path]::GetFullPath([string]$existing.Target[0])).Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Unexpected runtime parameter link: $paramRoot"
            }
            # Delete only the junction itself, without recursion; its game target is untouched.
            [IO.Directory]::Delete($paramRoot)
        }
        else {
            # Refuse nested links before copying or replacing any files.
            $pending = [Collections.Generic.Stack[string]]::new()
            $pending.Push($paramRoot)
            while ($pending.Count) {
                foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                        throw "Unexpected link inside runtime parameters: $($item.FullName)"
                    }
                    if ($item.PSIsContainer) { $pending.Push($item.FullName) }
                }
            }
        }
    }
    New-Item -ItemType Directory -Path $paramRoot -Force | Out-Null
    Copy-Item -Path (Join-Path $game 'param\*') -Destination $paramRoot -Recurse -Force
    [IO.File]::WriteAllBytes((Join-Path $paramRoot 'GameParam\GameParam.parambnd.dcx'), $patched)
}
