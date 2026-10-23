param([switch]$CheckOnly)

$ErrorActionPreference = 'Stop'

try {
    $steamPath = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steamPath) {
        $steamPath = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath
    }
    if (-not $steamPath -or -not (Test-Path -LiteralPath $steamPath -PathType Container)) {
        throw 'Steam was not found in its installation records. Install Steam and sign in first.'
    }

    $libraries = @($steamPath)
    $libraryFile = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $libraryFile -PathType Leaf) {
        $libraryText = Get-Content -LiteralPath $libraryFile -Raw
        foreach ($match in [regex]::Matches($libraryText, '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\', '\')
        }
    }

    $gamePath = $null
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $manifest = Join-Path $library 'steamapps\appmanifest_570940.acf'
        if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { continue }
        $manifestText = Get-Content -LiteralPath $manifest -Raw
        $installDir = [regex]::Match($manifestText, '"installdir"\s+"([^"]+)"')
        if (-not $installDir.Success) { continue }
        $candidate = Join-Path (Join-Path $library 'steamapps\common') $installDir.Groups[1].Value
        if (Test-Path -LiteralPath (Join-Path $candidate 'DarkSoulsRemastered.exe') -PathType Leaf) {
            $gamePath = $candidate
            break
        }
    }

    if (-not $gamePath) {
        throw 'Dark Souls Remastered was not found in Steam installation records. Install it through Steam or provide its correct path.'
    }
    if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
        throw 'Start Steam and sign in, then run this launcher again.'
    }

    Write-Host "Steam: $steamPath"
    Write-Host "Game:  $gamePath"
    if (Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue) {
        throw 'Close Dark Souls Remastered before using this launcher.'
    }
    $repository = Split-Path -Parent $PSScriptRoot
    $runtime = Join-Path $repository 'runtime\QSoulsRuntime.dll'
    foreach ($required in @($runtime, (Join-Path $repository 'assets\audio\title-theme.wav'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required mod file: $required" }
    }
    $dlls = @($runtime)
    $optionalRoot = Join-Path $repository 'SeamlessCoop_here'
    $packageFiles = @(Get-ChildItem -LiteralPath $optionalRoot -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne '.gitkeep' })
    if ($packageFiles.Count -gt 0) {
        foreach ($required in @('SeamlessCoop\ds1sc.dll', 'SeamlessCoop\ds1sc_settings.ini', 'SeamlessCoop\locale\english.json')) {
            if (-not (Test-Path -LiteralPath (Join-Path $optionalRoot $required) -PathType Leaf)) {
                throw "Incomplete Seamless Co-op package: missing $required. Extract the entire Nexus download into SeamlessCoop_here."
            }
        }
        $dlls += Join-Path $optionalRoot 'SeamlessCoop\ds1sc.dll'
        Write-Host 'Mode: Q-Souls-1 + Seamless Co-op'
    }
    else { Write-Host 'Mode: Q-Souls-1' }
    Add-Type -Path (Join-Path $PSScriptRoot 'ExternalModLauncher.cs')
    if ($CheckOnly) { Write-Host 'Checks passed; game not launched.'; exit 0 }
    # Keep the working directory external; the game reads installed assets through junctions.
    $runtimeRoot = Join-Path $repository '.local\game'
    New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
    $assetDirectories = @(Get-ChildItem -LiteralPath $gamePath -Directory |
        Where-Object { $_.Name -ne 'SeamlessCoop' })
    foreach ($directory in $assetDirectories) {
        $linkPath = Join-Path $runtimeRoot $directory.Name
        if (Test-Path -LiteralPath $linkPath) {
            $existing = Get-Item -LiteralPath $linkPath -Force
            if ($existing.LinkType -ne 'Junction' -or
                [IO.Path]::GetFullPath([string]$existing.Target[0]) -ne [IO.Path]::GetFullPath($directory.FullName)) {
                throw "Unexpected runtime asset path: $linkPath. Move .local aside and retry."
            }
        }
        else { New-Item -ItemType Junction -Path $linkPath -Target $directory.FullName | Out-Null }
    }
    $previousAppId = $env:SteamAppId
    $previousGameId = $env:SteamGameId
    try {
        $env:SteamAppId = '570940'
        $env:SteamGameId = '570940'
        $gameId = [ExternalModLauncher]::Launch((Join-Path $gamePath 'DarkSoulsRemastered.exe'), $runtimeRoot, [string[]]$dlls)
        Write-Host "Mod loaded. Game process: $gameId"
    }
    finally {
        $env:SteamAppId = $previousAppId
        $env:SteamGameId = $previousGameId
    }
    exit 0
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
