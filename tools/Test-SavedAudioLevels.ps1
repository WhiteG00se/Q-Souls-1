param([string]$TempRoot = 'D:\AI\Temp\Q-Souls-1\music-volume')
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'launcher\Read-SavedAudioLevels.ps1')
New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null
$fixture = Join-Path $TempRoot 'audio-settings-fixture.co2'
function Write-AudioFixture([byte[]]$Levels) {
    $data = New-Object byte[] 784
    [Text.Encoding]::ASCII.GetBytes('BND4').CopyTo($data, 0)
    [BitConverter]::GetBytes([uint32]11).CopyTo($data, 12)
    [BitConverter]::GetBytes([uint64]64).CopyTo($data, 16)
    [BitConverter]::GetBytes([uint64]32).CopyTo($data, 32)
    [BitConverter]::GetBytes([uint64]80).CopyTo($data, 392)
    [BitConverter]::GetBytes([uint32]704).CopyTo($data, 400)
    [BitConverter]::GetBytes([uint32]416).CopyTo($data, 404)
    [Text.Encoding]::Unicode.GetBytes("USER_DATA010`0").CopyTo($data, 416)
    $plain = New-Object byte[] 64
    [BitConverter]::GetBytes([uint32]0x60000).CopyTo($plain, 16)
    $Levels.CopyTo($plain, 28)
    $aes = [Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [Security.Cryptography.PaddingMode]::None
        $aes.Key = [byte[]]@(0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10)
        $aes.IV = New-Object byte[] 16
        $encryptor = $aes.CreateEncryptor()
        try { $encryptor.TransformFinalBlock($plain, 0, $plain.Length).CopyTo($data, 720) }
        finally { $encryptor.Dispose() }
    }
    finally { $aes.Dispose() }
    [IO.File]::WriteAllBytes($fixture, $data)
}
foreach ($levels in @(([byte[]]@(7,7,7)), ([byte[]]@(0,0,0)), ([byte[]]@(10,3,0)))) {
    Write-AudioFixture $levels
    $before = (Get-FileHash -LiteralPath $fixture).Hash
    $actual = Read-SavedAudioLevels -SavePath $fixture
    if (($actual -join ',') -ne ($levels -join ',')) { throw 'Saved audio decoding mismatch.' }
    if ((Get-FileHash -LiteralPath $fixture).Hash -ne $before) { throw 'Save reader changed fixture.' }
}
Write-AudioFixture ([byte[]]@(11,7,7))
$rejected = $false
try { [void](Read-SavedAudioLevels -SavePath $fixture) } catch { $rejected = $true }
if (-not $rejected) { throw 'Invalid saved audio level was accepted.' }
[IO.File]::WriteAllBytes($fixture, ([byte[]]@(0,0,0,0)))
$rejected = $false
try { [void](Read-SavedAudioLevels -SavePath $fixture) } catch { $rejected = $true }
if (-not $rejected) { throw 'Invalid container was accepted.' }
if (((Read-SavedAudioLevels) -join ',') -ne '10,10,10') { throw 'New-profile default mismatch.' }
Write-Output 'PASS: encrypted saved settings, saved mute, bounds/level rejection, defaults, and read-only behavior.'
