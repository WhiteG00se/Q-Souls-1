# Read only the global profile's audio settings; never write or decrypt character slots.
function Read-SavedAudioLevels {
    param([string]$SavePath)
    if (-not $SavePath -or -not (Test-Path -LiteralPath $SavePath -PathType Leaf)) {
        return ,([byte[]]@(10, 10, 10))
    }
    $stream = $null
    $reader = $null
    $aes = $null
    $decryptor = $null
    try {
        $stream = [IO.File]::Open($SavePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $reader = New-Object IO.BinaryReader($stream)
        $header = $reader.ReadBytes(64)
        if ($header.Length -ne 64 -or [Text.Encoding]::ASCII.GetString($header, 0, 4) -ne 'BND4' -or
            [BitConverter]::ToUInt32($header, 12) -ne 11 -or
            [BitConverter]::ToUInt64($header, 16) -ne 64 -or
            [BitConverter]::ToUInt64($header, 32) -ne 32) { throw 'Unsupported save header.' }
        [void]$stream.Seek(64 + 10 * 32, [IO.SeekOrigin]::Begin)
        $entry = $reader.ReadBytes(32)
        if ($entry.Length -ne 32) { throw 'Incomplete profile entry.' }
        $length = [BitConverter]::ToUInt64($entry, 8)
        $offset = [BitConverter]::ToUInt32($entry, 16)
        $nameOffset = [BitConverter]::ToUInt32($entry, 20)
        if ($length -lt 64 -or $offset + $length -gt $stream.Length -or $nameOffset + 26 -gt $stream.Length) {
            throw 'Invalid profile bounds.'
        }
        [void]$stream.Seek($nameOffset, [IO.SeekOrigin]::Begin)
        $name = [Text.Encoding]::Unicode.GetString($reader.ReadBytes(26)).TrimEnd([char]0)
        if ($name -ne 'USER_DATA010') { throw 'Global profile entry not found.' }
        [void]$stream.Seek($offset, [IO.SeekOrigin]::Begin)
        $iv = $reader.ReadBytes(16)
        $ciphertext = $reader.ReadBytes(48)
        if ($iv.Length -ne 16 -or $ciphertext.Length -ne 48) { throw 'Incomplete profile settings.' }
        $aes = [Security.Cryptography.Aes]::Create()
        $aes.Mode = [Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [Security.Cryptography.PaddingMode]::None
        $aes.Key = [byte[]]@(0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10)
        $aes.IV = $iv
        $decryptor = $aes.CreateDecryptor()
        $profile = $decryptor.TransformFinalBlock($ciphertext, 0, $ciphertext.Length)
        if ([BitConverter]::ToUInt32($profile, 16) -ne 0x60000) { throw 'Unsupported profile format.' }
        $levels = [byte[]]@($profile[0x1c], $profile[0x1d], $profile[0x1e])
        if (@($levels | Where-Object { $_ -gt 10 }).Count) { throw 'Invalid saved audio levels.' }
        return ,$levels
    }
    finally {
        if ($decryptor) { $decryptor.Dispose() }
        if ($aes) { $aes.Dispose() }
        if ($reader) { $reader.Dispose() }
        elseif ($stream) { $stream.Dispose() }
    }
}

function Get-ProfileAudioLevels {
    param([bool]$Seamless, [ValidatePattern('^[a-zA-Z0-9]{1,120}$')][string]$SaveExtension)
    $root = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'NBGI\DARK SOULS REMASTERED'
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { return Read-SavedAudioLevels }
    if (-not $SaveExtension) { $SaveExtension = if ($Seamless) { 'co2' } else { 'sl2' } }
    $filename = 'DRAKS0005.' + $SaveExtension
    $account = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam\ActiveProcess' -ErrorAction SilentlyContinue).ActiveUser
    if ($account) {
        $steamId = [uint64]76561197960265728 + [uint32]$account
        foreach ($name in @(([uint32]$account).ToString(), $steamId.ToString(), $steamId.ToString('x16'))) {
            $path = Join-Path (Join-Path $root $name) $filename
            if (Test-Path -LiteralPath $path -PathType Leaf) { return Read-SavedAudioLevels -SavePath $path }
        }
        return Read-SavedAudioLevels
    }
    $profiles = @(Get-ChildItem -LiteralPath $root -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName $filename) -PathType Leaf
    })
    if ($profiles.Count -eq 1) { return Read-SavedAudioLevels -SavePath (Join-Path $profiles[0].FullName $filename) }
    if ($profiles.Count -gt 1) { throw 'Sign into Steam so the launcher can select the correct saved audio settings.' }
    return Read-SavedAudioLevels
}
