param([switch]$CheckOnly, [ValidateSet('Auto', 'Enabled', 'Disabled')][string]$CoopMode = 'Auto', [ValidatePattern('^[a-zA-Z0-9]{1,120}$')][string]$SaveExtension)

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
    Write-Host "Steam: $steamPath"
    Write-Host "Game:  $gamePath"
    if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
        if ($CheckOnly) { Write-Host 'Steam is not running; start it and sign in before playing.' }
        else { throw 'Start Steam and sign in, then run this launcher again.' }
    }
    if (-not $CheckOnly -and (Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue)) {
        throw 'Close Dark Souls Remastered before using this launcher.'
    }
    $repository = Split-Path -Parent $PSScriptRoot
    $runtime = Join-Path $repository 'runtime\QSoulsRuntime.dll'
    $dlls = @($runtime)
    $optionalRoot = Join-Path $repository 'SeamlessCoop_here'
    $packageFiles = @(Get-ChildItem -LiteralPath $optionalRoot -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne '.gitkeep' })
    $useSeamless = $CoopMode -eq 'Enabled' -or ($CoopMode -eq 'Auto' -and $packageFiles.Count -gt 0)
    if ($useSeamless) {
        foreach ($required in @('SeamlessCoop\ds1sc.dll', 'SeamlessCoop\ds1sc_settings.ini', 'SeamlessCoop\locale\english.json')) {
            if (-not (Test-Path -LiteralPath (Join-Path $optionalRoot $required) -PathType Leaf)) {
                throw "Incomplete Seamless Co-op package: missing $required. Extract the entire Nexus download into SeamlessCoop_here."
            }
        }
        $dlls += Join-Path $optionalRoot 'SeamlessCoop\ds1sc.dll'
        Write-Host 'Mode: Q-Souls-1 + Seamless Co-op'
    }
    else { Write-Host 'Mode: Q-Souls-1' }
    . (Join-Path $PSScriptRoot 'Read-SavedAudioLevels.ps1')
    $controllerHelper = $false
    $helperSettingsPath = Join-Path $repository '.local\launcher.xml'
    if (Test-Path -LiteralPath $helperSettingsPath -PathType Leaf) {
        [xml]$helperSettings = Get-Content -LiteralPath $helperSettingsPath -Raw
        $controllerHelper = $helperSettings.LauncherSettings.ControllerHelper -eq 'true'
    }
    if (-not $SaveExtension) {
        $SaveExtension = 'sl2'
        $settingsPath = Join-Path $repository '.local\launcher.xml'
        if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
            [xml]$settings = Get-Content -LiteralPath $settingsPath -Raw
            if ($settings.LauncherSettings.SaveMode -eq 'co2') { $SaveExtension = 'co2' }
            elseif ($settings.LauncherSettings.SaveMode -eq 'custom') { $SaveExtension = [string]$settings.LauncherSettings.CustomSaveExtension }
        }
    }
    if ($SaveExtension -notmatch '^[a-zA-Z0-9]{1,120}$') { throw 'Enter a save extension using 1 to 120 letters or numbers after the dot.' }
    if ($useSeamless) {
        $ini = Get-Content -LiteralPath (Join-Path $optionalRoot 'SeamlessCoop\ds1sc_settings.ini') -Raw
        $extensionMatch = [regex]::Match($ini, '(?im)^[ \t]*save_file_extension[ \t]*=[ \t]*([a-z0-9]{1,120})[ \t]*\r?$')
        if (-not $extensionMatch.Success -or $extensionMatch.Groups[1].Value -cne $SaveExtension) {
            if ($CheckOnly) { Write-Host "Seamless save extension needs to be saved as .$SaveExtension in the launcher." }
            else { throw 'Save the selected extension in the launcher before playing.' }
        }
    }
    Write-Host "Save file: DRAKS0005.$SaveExtension"
    $savedAudio = Get-ProfileAudioLevels -Seamless $useSeamless -SaveExtension $saveExtension
    Write-Host ("Saved music level: {0}/10" -f $savedAudio[0])
    foreach ($required in @($runtime, (Join-Path $repository 'VERSION.txt'), (Join-Path $repository 'assets\audio\title-theme.wav'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required mod file: $required" }
    }
    Add-Type -Path (Join-Path $PSScriptRoot 'ExternalModLauncher.cs')
    # Keep generated parameters external; other assets are read through junctions.
    $runtimeRoot = Join-Path $repository '.local\game'
    . (Join-Path $PSScriptRoot 'Initialize-StartingClasses.ps1')
    . (Join-Path $repository 'launcher\Initialize-Respec.ps1')
    Initialize-Respec -GamePath $gamePath -Repository $repository -ValidateOnly:$CheckOnly
    Initialize-StartingClasses -GamePath $gamePath -RuntimeRoot $runtimeRoot -ValidateOnly:$CheckOnly
    if ($CheckOnly) { Write-Host 'Checks passed; game not launched.'; exit 0 }
    if ($useSeamless) {
        . (Join-Path $PSScriptRoot 'Initialize-SeamlessRuntime.ps1')
        Initialize-SeamlessRuntime -OptionalRoot $optionalRoot -RuntimeRoot $runtimeRoot
    }
    $assetDirectories = @(Get-ChildItem -LiteralPath $gamePath -Directory |
        Where-Object { $_.Name -notin @('SeamlessCoop', 'param') })
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
    $previousAudio = $env:QSOULS_SAVED_AUDIO
    $previousSeamless = $env:QSOULS_SEAMLESS
    $previousExtension = $env:QSOULS_SAVE_EXTENSION
    $previousSaveRoot = $env:QSOULS_SAVE_ROOT
    $previousControllerHelper = $env:QSOULS_CONTROLLER_HELPER
    try {
        $env:QSOULS_CONTROLLER_HELPER = if ($controllerHelper) { '1' } else { '0' }
        $env:SteamAppId = '570940'
        $env:SteamGameId = '570940'
        $env:QSOULS_SAVED_AUDIO = $savedAudio -join ','
        $env:QSOULS_SEAMLESS = if ($useSeamless) { '1' } else { '0' }
        $env:QSOULS_SAVE_EXTENSION = $SaveExtension
        $env:QSOULS_SAVE_ROOT = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'NBGI\DARK SOULS REMASTERED'
        $gameId = [ExternalModLauncher]::Launch((Join-Path $gamePath 'DarkSoulsRemastered.exe'), $runtimeRoot, [string[]]$dlls)
        Write-Host "Mod loaded. Game process: $gameId"
    }
    finally {
        $env:SteamAppId = $previousAppId
        $env:SteamGameId = $previousGameId
        $env:QSOULS_SAVED_AUDIO = $previousAudio
        $env:QSOULS_SEAMLESS = $previousSeamless
        $env:QSOULS_SAVE_EXTENSION = $previousExtension
        $env:QSOULS_SAVE_ROOT = $previousSaveRoot
        $env:QSOULS_CONTROLLER_HELPER = $previousControllerHelper
    }
    exit 0
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
