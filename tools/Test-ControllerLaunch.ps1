# Test only the settings/environment portions of the real launch script, with a fake launcher.
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\controller-launch-tests' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $temporary '.local') -Force | Out-Null
Add-Type @"
using System;
public static class ExternalModLauncher {
    public static bool Fail;
    public static string Helper, AppId;
    public static int Launch(string executable, string directory, string[] dlls) {
        Helper = Environment.GetEnvironmentVariable("QSOULS_CONTROLLER_HELPER");
        AppId = Environment.GetEnvironmentVariable("SteamAppId");
        if (Fail) throw new Exception("Fixture launch failure");
        return 123;
    }
}
"@
function Assert($ok, $message) { if (-not $ok) { throw $message } }
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repository 'launcher\Start-Q-Souls-1.ps1'), [ref]$tokens, [ref]$errors)
Assert (-not $errors) 'Launch script syntax errors.'
$body = $ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] } | Select-Object -First 1
$statements = $body.Body.Statements
$selection = @($statements | Where-Object { $_.Extent.Text -match '^\$(controllerHelper|helperSettingsPath)\s*=' -or $_.Extent.Text -match '^if \(Test-Path -LiteralPath \$helperSettingsPath' })
$previous = @($statements | Where-Object { $_.Extent.Text -match '^\$previous\w+\s*=' })
$launch = $statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] -and $_.Extent.Text.Contains('[ExternalModLauncher]::Launch') }
Assert ($selection.Count -eq 3 -and $previous.Count -eq 7 -and $launch) 'Launch script test sections not found.'
$repository = $temporary
$gamePath = $temporary; $runtimeRoot = $temporary; $dlls = @(); $savedAudio = @(1, 2, 3); $useSeamless = $false; $SaveExtension = 'sl2'
$names = @('SteamAppId', 'SteamGameId', 'QSOULS_SAVED_AUDIO', 'QSOULS_SEAMLESS', 'QSOULS_SAVE_EXTENSION', 'QSOULS_SAVE_ROOT', 'QSOULS_CONTROLLER_HELPER')
$original = @{}
foreach ($name in $names) { $original[$name] = [Environment]::GetEnvironmentVariable($name); [Environment]::SetEnvironmentVariable($name, 'fixture-parent') }
try {
    foreach ($xml in @('<LauncherSettings/>', '<LauncherSettings><ControllerHelper>false</ControllerHelper></LauncherSettings>', '<LauncherSettings><ControllerHelper>true</ControllerHelper></LauncherSettings>')) {
        [IO.File]::WriteAllText((Join-Path $temporary '.local\launcher.xml'), $xml)
        foreach ($failure in @($false, $true)) {
            [ExternalModLauncher]::Fail = $failure
            foreach ($statement in $selection) { . ([scriptblock]::Create($statement.Extent.Text)) }
            foreach ($statement in $previous) { . ([scriptblock]::Create($statement.Extent.Text)) }
            $failed = $false
            try { . ([scriptblock]::Create($launch.Extent.Text)) } catch { $failed = $true }
            Assert ($failed -eq $failure) 'Unexpected fixture launch result.'
            $expected = if ($xml.Contains('>true<')) { '1' } else { '0' }
            Assert ([ExternalModLauncher]::Helper -eq $expected -and [ExternalModLauncher]::AppId -eq '570940') 'Launch setting not propagated.'
            foreach ($name in $names) { Assert ([Environment]::GetEnvironmentVariable($name) -eq 'fixture-parent') "Parent environment not restored: $name" }
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $original[$name]) }
}
Write-Output 'PASS: legacy/default-off and enabled setting propagation; parent environment restored after success and failure. Fake launcher only; no game launched.'
