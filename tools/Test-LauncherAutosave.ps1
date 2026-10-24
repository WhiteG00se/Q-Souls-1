$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\autosave-tests' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop\locale') -Force | Out-Null
Add-Type -AssemblyName System.Windows.Forms
[void][Reflection.Assembly]::LoadFrom((Join-Path $repository '_launch-Q-Souls-1.exe'))
function Assert($condition, $message) { if (-not $condition) { throw $message } }
$flags = [Reflection.BindingFlags]'Instance,NonPublic'
function Field($form, $name) { return $form.GetType().GetField($name, $flags).GetValue($form) }
function Wait-Save($form, $failure = $false) {
    $limit = [DateTime]::UtcNow.AddSeconds(10)
    $method = $form.GetType().GetMethod('SettingsSaving', $flags)
    do { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10; $pending = $method.Invoke($form, @()) } while ($pending -and [DateTime]::UtcNow -lt $limit)
    Assert (-not $pending) 'Autosave timed out.'
    Assert ((Field $form 'saveFailed') -eq $failure) 'Unexpected autosave result.'
}
$iniPath = Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop\ds1sc_settings.ini'
$ini = "; preserve comments`r`n[PASSWORD]`r`ncooppassword = initial`r`n[SAVE]`r`nsave_file_extension = co2`r`n"
[IO.File]::WriteAllText($iniPath, $ini)
[IO.File]::WriteAllText((Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop\ds1sc.dll'), 'fixture, never loaded')
[IO.File]::WriteAllText((Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop\locale\english.json'), '{}')
$form = New-Object LauncherWindow($temporary, $false)
try {
    $form.ShowInTaskbar = $false; $form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
    $form.Location = New-Object Drawing.Point(-32000, -32000); $form.Show(); [Windows.Forms.Application]::DoEvents()
    $play = Field $form 'play'; $password = Field $form 'password'; $coop = Field $form 'seamless'
    Assert (-not $play.Enabled) 'Play enabled while startup settings were pending.'
    Assert (-not $password.UseSystemPasswordChar -and $password.PasswordChar -eq [char]0) 'Password remains masked.'
    Assert ($form.GetType().GetField('save', $flags) -eq $null) 'Save settings button still exists.'
    Wait-Save $form
    Assert ($play.Enabled) 'Play did not enable after successful initial save.'
    $password.Text = 'first edit'
    Assert (-not $play.Enabled) 'Play did not disable immediately after edit.'
    $password.Text = 'final edit'
    Wait-Save $form
    Assert ($play.Enabled) 'Play did not enable after latest edit was saved.'
    Assert ([LauncherFiles]::ReadValue([IO.File]::ReadAllText($iniPath), 'PASSWORD', 'cooppassword', '') -eq 'final edit') 'Autosave missed the latest password.'
    Assert ([IO.File]::ReadAllText($iniPath).Contains('; preserve comments')) 'Autosave changed unrelated INI content.'
    # Simulate a real disk-save failure with a locked settings file.
    $settingsPath = Join-Path $temporary '.local\launcher.xml'
    $lock = [IO.File]::Open($settingsPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $password.Text = 'blocked save'
        Wait-Save $form $true
        Assert (-not $play.Enabled) 'Play enabled after a failed save.'
        Assert ((Field $form 'status').Text.StartsWith('Could not save settings.')) 'Save failure not shown.'
    } finally { $lock.Dispose() }
    Assert (-not $play.Enabled) 'Releasing the lock bypassed the failed-save state.'
    $password.Text = 'retry save'
    Assert (-not $play.Enabled) 'Play enabled before retry completed.'
    Wait-Save $form
    Assert ($play.Enabled) 'Play did not recover after successful retry.'
    $password.Text = ''
    Wait-Save $form $true
    Assert (-not $play.Enabled) 'Co-op with an invalid empty password enabled Play.'
    $password.Text = 'saved while switching mode'; $coop.Checked = $false
    Wait-Save $form
    Assert ([LauncherFiles]::ReadValue([IO.File]::ReadAllText($iniPath), 'PASSWORD', 'cooppassword', '') -eq 'saved while switching mode') 'Disabling co-op discarded a pending password edit.'
    $serializer = New-Object Xml.Serialization.XmlSerializer([LauncherSettings])
    $stream = [IO.File]::OpenRead($settingsPath)
    try { Assert (-not $serializer.Deserialize($stream).Seamless) 'Co-op toggle did not autosave.' } finally { $stream.Dispose() }
    # Closing waits for the most recent edit to persist.
    $coop.Checked = $true; $password.Text = 'save on close'
    $form.Close()
    $limit = [DateTime]::UtcNow.AddSeconds(10)
    while (-not $form.IsDisposed -and [DateTime]::UtcNow -lt $limit) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10 }
    Assert ($form.IsDisposed) 'Window did not close after saving.'
    Assert ([LauncherFiles]::ReadValue([IO.File]::ReadAllText($iniPath), 'PASSWORD', 'cooppassword', '') -eq 'save on close') 'Close lost the pending edit.'
} finally { $form.Dispose() }
Write-Output 'PASS: automatic saves, latest edit persistence, Play disabled while pending and after real save failure, retry recovery, validation, mode switch preservation, close flush, always-visible password, removed Save button. Fixture files only; no game launched.'
