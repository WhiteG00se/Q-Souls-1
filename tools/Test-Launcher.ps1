$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\launcher-tests' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[void][Reflection.Assembly]::LoadFrom((Join-Path $repository '_launch-Q-Souls-1.exe'))
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Wait-Settings($form) {
    if (-not $form.Visible) {
        $form.Opacity = 0; $form.ShowInTaskbar = $false; $form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
        $form.Location = New-Object Drawing.Point(-32000, -32000); $form.Show()
        [Windows.Forms.Application]::DoEvents()
    }
    $flags = [Reflection.BindingFlags]'Instance,NonPublic'
    $method = $form.GetType().GetMethod('SettingsSaving', $flags)
    $limit = [DateTime]::UtcNow.AddSeconds(10)
    do {
        [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10
        $pending = $method.Invoke($form, @())
    } while ($pending -and [DateTime]::UtcNow -lt $limit)
    if ($pending) {
        $debugStatus = $form.GetType().GetField('status', $flags).GetValue($form).Text
        $rev = $form.GetType().GetField('saveRevision', $flags).GetValue($form)
        $saved = $form.GetType().GetField('savedRevision', $flags).GetValue($form)
        $sw = $form.GetType().GetField('saveWorker', $flags).GetValue($form)
        $timer = $form.GetType().GetField('saveTimer', $flags).GetValue($form)
        throw "Settings save timed out: $debugStatus; revision=$rev saved=$saved worker=$($sw.IsBusy) timer=$($timer.Enabled)"
    }
    Assert (-not $form.GetType().GetField('saveFailed', $flags).GetValue($form)) 'Settings save failed.'
}
$ini = "; keep comment`r`n[PASSWORD]`r`ncooppassword = old`r`n`r`n[SAVE]`r`nsave_file_extension = sl2`r`n"
$password = 'literal$1\test=123'
$updated = [LauncherFiles]::SetPassword($ini, $password)
Assert ($updated -ceq $ini.Replace('old', $password)) 'Password update changed unrelated settings.'
Assert ([LauncherFiles]::ReadValue($updated, 'PASSWORD', 'cooppassword', '') -ceq $password) 'Password round trip failed.'
Assert ([LauncherFiles]::ReadValue($ini.Replace('old', ''), 'PASSWORD', 'cooppassword', 'fallback') -eq '') 'Empty password consumed another line.'
Assert ([LauncherFiles]::ReadValue($ini, 'SAVE', 'save_file_extension', '') -eq 'sl2') 'Save extension not read.'
$rejected = $false
try { [void][LauncherFiles]::SetPassword($ini, "bad`nentry") } catch { $rejected = $true }
Assert $rejected 'Multiline password was accepted.'
$settingsPath = Join-Path $temporary '.local\launcher.xml'
[LauncherFiles]::Write($settingsPath, '<LauncherSettings><Seamless>false</Seamless></LauncherSettings>')
[LauncherFiles]::Write($settingsPath, '<LauncherSettings><Seamless>true</Seamless></LauncherSettings>')
$serializer = New-Object Xml.Serialization.XmlSerializer([LauncherSettings])
$stream = [IO.File]::OpenRead($settingsPath)
try { Assert ($serializer.Deserialize($stream).Seamless) 'Settings overwrite failed.' } finally { $stream.Dispose() }
# Construct controls without showing the launcher or executing the backend/game.
$form = New-Object LauncherWindow($temporary, $false)
try {
    Assert ($form.Text -eq 'Q Souls 1 Launcher') 'Launcher form missing.'
    Assert ($form.Icon -ne $null) 'QS1 window icon missing.'
    Assert ($form.AllowDrop) 'ZIP drag-and-drop disabled.'
    Assert ($form.GetType().GetField('verify', [Reflection.BindingFlags]'Instance,NonPublic') -eq $null) 'Separate file verification button still exists.'
    $setupButton = $form.GetType().GetField('check', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert ($setupButton.Text -eq 'Verify setup') 'Merged setup button has the wrong label.'
    Assert (($setupButton.Parent.Controls | ForEach-Object { $_.Text }) -join '|' -eq 'Play|Open savefile folder|Verify setup') 'Setup button is not in the requested position.'
    $flags = [Reflection.BindingFlags]'Instance,NonPublic'
    $localVersion = $form.GetType().GetField('installedSouls', $flags).GetValue($form)
    $remoteVersion = $form.GetType().GetField('newestSouls', $flags).GetValue($form)
    $originalLocal = $localVersion.Text; $originalRemote = $remoteVersion.Text
    $styleVersions = $form.GetType().GetMethod('UpdateVersionStyles', $flags)
    $localVersion.Text = 'Installed: v0.9'
    $remoteVersion.Text = 'Newest: v0.10'
    [void]$styleVersions.Invoke($form, @())
    Assert ($localVersion.Font.Bold -and $remoteVersion.Font.Bold) 'Different versions must both be bold.'
    Assert ($localVersion.AccentVersion -and $remoteVersion.AccentVersion) 'Different versions must accent both version suffixes.'
    $remoteVersion.Text = 'Newest: v0.9'
    [void]$styleVersions.Invoke($form, @())
    Assert (-not $localVersion.Font.Bold -and -not $remoteVersion.Font.Bold) 'Matching versions must return to regular weight.'
    Assert (-not $localVersion.AccentVersion -and -not $remoteVersion.AccentVersion) 'Matching versions must remove conditional accents.'
    $localVersion.Text = $originalLocal; $remoteVersion.Text = $originalRemote
    [void]$styleVersions.Invoke($form, @())
    Wait-Settings $form
    $updateButton = $form.GetType().GetField('update', $flags).GetValue($form)
    Assert (-not $updateButton.Enabled) 'Update enabled before a version check.'
    $zipHint = $form.GetType().GetField('zipHint', $flags).GetValue($form)
    Assert (-not $zipHint.Visible) 'ZIP hint shown before the download-page click.'
    $remote = New-Object PublishedVersion
    $remote.Version = '0.8'
    $remote.Files = New-Object 'Collections.Generic.List[RepoFile]'
    foreach ($name in @('_launch-Q-Souls-1.exe', '_launch-Q-Souls-1.exe.config', 'VERSION.txt', 'runtime/QSoulsRuntime.dll', 'launcher/Start-Q-Souls-1.ps1')) {
        $entry = New-Object RepoFile; $entry.Path = $name; $remote.Files.Add($entry)
    }
    [IO.File]::WriteAllText((Join-Path $temporary 'VERSION.txt'), '0.7')
    $form.GetType().GetField('newest', $flags).SetValue($form, $remote)
    $buttonMethod = $form.GetType().GetMethod('UpdateButton', $flags)
    [void]$buttonMethod.Invoke($form, @())
    Assert ($updateButton.Enabled) 'Newer complete release did not enable Update.'
    $remote.Version = '0.7'; [void]$buttonMethod.Invoke($form, @())
    Assert (-not $updateButton.Enabled) 'Same version enabled Update.'
    $remote.Version = '0.6'; [void]$buttonMethod.Invoke($form, @())
    Assert (-not $updateButton.Enabled) 'Older version enabled Update.'
    $remote.Version = '0.8'; $remote.Files.Clear(); [void]$buttonMethod.Invoke($form, @())
    Assert (-not $updateButton.Enabled) 'Incomplete published release enabled Update.'
    $panel = $form.Controls[0]
    $coop = $form.GetType().GetField('seamless', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert (-not $coop.Enabled -and -not $coop.Checked) 'Missing co-op package was enabled.'
    $iniDirectory = Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop'
    New-Item -ItemType Directory -Path (Join-Path $iniDirectory 'locale') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $iniDirectory 'ds1sc_settings.ini'), $ini)
    [IO.File]::WriteAllText((Join-Path $iniDirectory 'ds1sc.dll'), 'fixture, never loaded')
    [IO.File]::WriteAllText((Join-Path $iniDirectory 'locale\english.json'), '{}')
} finally { $form.Dispose() }
[LauncherFiles]::Write($settingsPath, '<LauncherSettings><Seamless>true</Seamless></LauncherSettings>')
$form = New-Object LauncherWindow($temporary, $false)
try {
    Wait-Settings $form
    $panel = $form.Controls[0]
    $coop = $form.GetType().GetField('seamless', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert ($coop.Enabled -and $coop.Checked) 'Installed co-op or persisted selection missing.'
    $saveLabel = $form.GetType().GetField('saveInfo', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert ($saveLabel.Text.Contains('DRAKS0005.sl2') -and $saveLabel.Text.Contains('Vanilla, works with Steam cloud sync.')) 'Cloud status incorrect.'
    $coop.Checked = $false
    Assert ($saveLabel.Text.Contains('DRAKS0005.sl2')) 'Changing mode did not update save information.'
    Wait-Settings $form
    $stream = [IO.File]::OpenRead($settingsPath)
    try { Assert (-not $serializer.Deserialize($stream).Seamless) 'Disabled co-op did not persist.' } finally { $stream.Dispose() }
    Assert ([IO.File]::ReadAllText((Join-Path $iniDirectory 'ds1sc_settings.ini')) -ceq $ini) 'Disabled co-op changed its INI.'
    $coop.Checked = $true
    $passwordBox = $form.GetType().GetField('password', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    $passwordBox.Text = $password
    Wait-Settings $form
    Assert ([IO.File]::ReadAllText((Join-Path $iniDirectory 'ds1sc_settings.ini')) -ceq $updated) 'GUI password save changed unrelated INI content.'
    $form.Opacity = 0; $form.ShowInTaskbar = $false
    $form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
    $form.Location = New-Object Drawing.Point(-32000, -32000)
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    $form.PerformLayout()
    $bitmap = New-Object Drawing.Bitmap($form.Width, $form.Height)
    try { $form.DrawToBitmap($bitmap, (New-Object Drawing.Rectangle(0, 0, $form.Width, $form.Height))); $bitmap.Save((Join-Path $temporary 'launcher.png')) } finally { $bitmap.Dispose() }
} finally { $form.Dispose() }
# Exercise a dropped archive through the GUI's actual background installation path.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$dropRoot = Join-Path $temporary 'drop-fixture'
New-Item -ItemType Directory -Path $dropRoot | Out-Null
$zipPath = Join-Path $temporary 'Seamless-Co-op-899-0-0-4-123456.zip'
$archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $entries = @{
        'SeamlessCoop/ds1sc.dll' = 'fixture, never loaded'
        'SeamlessCoop/ds1sc_settings.ini' = "[PASSWORD]`r`ncooppassword = `r`n[SAVE]`r`nsave_file_extension = sl2`r`n"
        'SeamlessCoop/locale/english.json' = '{}'
    }
    foreach ($name in $entries.Keys) {
        $writer = New-Object IO.StreamWriter($archive.CreateEntry($name).Open())
        try { $writer.Write($entries[$name]) } finally { $writer.Dispose() }
    }
} finally { $archive.Dispose() }
$form = New-Object LauncherWindow($dropRoot, $false)
try {
    $form.Opacity = 0; $form.ShowInTaskbar = $false; $form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
    $form.Location = New-Object Drawing.Point(-32000, -32000); $form.Show(); [Windows.Forms.Application]::DoEvents()
    Wait-Settings $form
    $data = New-Object Windows.Forms.DataObject
    $data.SetData([Windows.Forms.DataFormats]::FileDrop, [string[]]@($zipPath))
    $drag = New-Object Windows.Forms.DragEventArgs($data, 0, 0, 0, [Windows.Forms.DragDropEffects]::Copy, [Windows.Forms.DragDropEffects]::Copy)
    $form.GetType().GetMethod('OnDragDrop', [Reflection.BindingFlags]'Instance,NonPublic').Invoke($form, @($drag.PSObject.BaseObject)) | Out-Null
    $worker = $form.GetType().GetField('worker', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    $status = $form.GetType().GetField('status', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    $testDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while (($worker.IsBusy -or $status.Text.StartsWith('Validating')) -and [DateTime]::UtcNow -lt $testDeadline) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 20 }
    [Windows.Forms.Application]::DoEvents()
    Assert (-not $worker.IsBusy) 'GUI archive installation timed out.'
    $status = $form.GetType().GetField('status', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    $detailBox = $form.GetType().GetField('details', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert ($detailBox.Text.Contains('Seamless Co-op installed successfully')) ('GUI did not finish dropped ZIP installation: ' + $status.Text)
    $savePending = $form.GetType().GetMethod('SettingsSaving', [Reflection.BindingFlags]'Instance,NonPublic')
    $testDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10; $pending = $savePending.Invoke($form, @()) } while ($pending -and [DateTime]::UtcNow -lt $testDeadline)
    Assert (-not $pending) 'Post-install password validation timed out.'
    $expected = [char]0x2022 + ' Seamless Co-op installed successfully' + [Environment]::NewLine + [char]0x2022 + ' Enter a co-op password. Everyone in your party must use the same password.'
    Assert ($detailBox.Text.Replace("`r", "") -ceq $expected.Replace("`r", "")) 'Blank password replaced installation success with technical error output.'
    $play = $form.GetType().GetField('play', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert (-not $play.Enabled) 'Play enabled without a co-op password.'
    $passwordBox = $form.GetType().GetField('password', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    $passwordBox.Text = 'shared'
    Wait-Settings $form
    Assert ($play.Enabled) 'Play did not enable after the password was saved.'
    Assert ($detailBox.Text.Contains('Seamless Co-op installed successfully') -and $detailBox.Text.Contains(([char]0x2705 + ' Settings saved')) -and -not $detailBox.Text.Contains('Enter a co-op password.')) 'Post-password success feedback is incorrect.'
    $coop = $form.GetType().GetField('seamless', [Reflection.BindingFlags]'Instance,NonPublic').GetValue($form)
    Assert ($coop.Checked -and $coop.Enabled) 'Successful install did not enable co-op controls.'
    Assert ([RepositoryUpdates]::CoopInstalled($dropRoot) -eq 'v0.0.4') 'Dropped ZIP did not record co-op version.'
} finally { $form.Dispose() }
$exe = [IO.File]::ReadAllBytes((Join-Path $repository '_launch-Q-Souls-1.exe'))
$pe = [BitConverter]::ToInt32($exe, 0x3c)
Assert ([BitConverter]::ToUInt16($exe, $pe + 24 + 68) -eq 2) 'Executable uses the console subsystem.'
foreach ($file in @('launcher\Start-Q-Souls-1.ps1', 'launcher\Read-SavedAudioLevels.ps1', 'tools\Build-Launcher.ps1')) {
    $tokens = $null; $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $repository $file), [ref]$tokens, [ref]$errors)
    Assert ($errors.Count -eq 0) "PowerShell syntax failed: $file"
}
Write-Output 'PASS: password preservation and validation, empty values, settings persistence, mode controls, save status, GUI save actions, Windows GUI subsystem, PowerShell syntax. No game launched.'
Write-Output (Join-Path $temporary 'launcher.png')
