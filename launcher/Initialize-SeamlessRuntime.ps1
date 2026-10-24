function Initialize-SeamlessRuntime {
    param([Parameter(Mandatory)][string]$OptionalRoot, [Parameter(Mandatory)][string]$RuntimeRoot)
    $source = [IO.Path]::GetFullPath((Join-Path $OptionalRoot 'SeamlessCoop'))
    $RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
    $destination = Join-Path $RuntimeRoot 'SeamlessCoop'
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    $existing = Get-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
    if ($existing -and -not ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        if (-not $existing.PSIsContainer) { throw "Runtime Seamless path is not a directory: $destination" }
        $backup = Join-Path $RuntimeRoot ('SeamlessCoop.backup-' + [Guid]::NewGuid().ToString('N'))
        # Both paths must be direct children of the external runtime directory before moving.
        if ([IO.Path]::GetDirectoryName($existing.FullName) -ne $RuntimeRoot -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($backup)) -ne $RuntimeRoot) {
            throw 'Runtime Seamless backup is outside the runtime directory.'
        }
        Move-Item -LiteralPath $existing.FullName -Destination $backup
        Write-Host "Previous Seamless runtime files preserved: $backup"
        $existing = $null
    }
    if ($existing) {
        if ($existing.LinkType -ne 'Junction' -or
            [IO.Path]::GetFullPath([string]$existing.Target[0]) -ne $source) {
            throw "Unexpected runtime Seamless path: $destination. Move .local aside and retry."
        }
    }
    else { New-Item -ItemType Junction -Path $destination -Target $source | Out-Null }
}
