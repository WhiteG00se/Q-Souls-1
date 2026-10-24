$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\save-options-tests' ([Guid]::NewGuid().ToString('N'))
$package = Join-Path $temporary 'SeamlessCoop_here\SeamlessCoop'
New-Item -ItemType Directory -Path (Join-Path $package 'locale') -Force | Out-Null
Add-Type -AssemblyName System.Windows.Forms
[void][Reflection.Assembly]::LoadFrom((Join-Path $repository '_launch-Q-Souls-1.exe'))
$iniPath = Join-Path $package 'ds1sc_settings.ini'
[IO.File]::WriteAllText($iniPath, "[PASSWORD]`r`ncooppassword = test-party`r`n[SAVE]`r`nsave_file_extension = co2`r`n")
[IO.File]::WriteAllText((Join-Path $package 'ds1sc.dll'), 'fixture, never loaded')
[IO.File]::WriteAllText((Join-Path $package 'locale\english.json'), '{}')
$flags = [Reflection.BindingFlags]'Instance,NonPublic'
function Assert($ok, $message) { if (-not $ok) { throw $message } }
function Field($form, $name) { return $form.GetType().GetField($name, $flags).GetValue($form) }
function Wait-Save($form, $failure = $false) {
    $method = $form.GetType().GetMethod('SettingsSaving', $flags)
    $limit = [DateTime]::UtcNow.AddSeconds(10)
    do { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10; $pending = $method.Invoke($form, @()) } while ($pending -and [DateTime]::UtcNow -lt $limit)
    Assert (-not $pending -and (Field $form 'saveFailed') -eq $failure) 'Save did not finish as expected.'
}
function Show-Fixture($form) {
    $form.Opacity = 0; $form.ShowInTaskbar = $false; $form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
    $form.Location = New-Object Drawing.Point(-32000, -32000); $form.Show(); [Windows.Forms.Application]::DoEvents()
}
function Extension { return [LauncherFiles]::ReadValue([IO.File]::ReadAllText($iniPath), 'SAVE', 'save_file_extension', '') }
$form = New-Object LauncherWindow($temporary, $false)
try {
    Show-Fixture $form; Wait-Save $form
    $sl2 = Field $form 'sl2'; $co2 = Field $form 'co2'; $custom = Field $form 'custom'
    $input = Field $form 'customExtension'; $row = Field $form 'customExtensionRow'; $group = Field $form 'customExtensionGroup'; $info = Field $form 'saveInfo'; $coop = Field $form 'seamless'
    Assert ($sl2.Checked -and (Extension) -eq 'sl2') 'Default did not stay sl2 despite the co-op package default.'
    Assert (-not $group.Visible) 'Custom input is visible for sl2.'
    Assert ($info.Text -ceq "Save: DRAKS0005.sl2`r`nVanilla, works with Steam cloud sync.`r`nOnly load modded saves in the modded game and vice versa.") 'sl2 remark differs.'
    $coop.Checked = $false; Wait-Save $form
    Assert ($sl2.Checked -and (Extension) -eq 'sl2') 'Disabling co-op changed the save extension.'
    $coop.Checked = $true; Wait-Save $form
    Assert ($sl2.Checked) 'Enabling co-op changed the save extension.'
    $co2.Checked = $true; Wait-Save $form
    Assert ((Extension) -eq 'co2' -and -not $group.Visible) 'co2 selection not saved.'
    Assert ($info.Text.EndsWith("Default for Seemless Co-op. Doesn't work with Steam cloud sync.")) 'co2 remark differs.'
    $coop.Checked = $false; Wait-Save $form
    Assert ($co2.Checked -and (Extension) -eq 'co2') 'Co-op toggle changed co2 selection.'
    $custom.Checked = $true; Wait-Save $form $true
    Assert ($group.Visible -and -not (Field $form 'play').Enabled) 'Empty custom selection did not show input/block Play.'
    $prefix = Field $form 'customExtensionPrefix'
    Assert ($prefix -is [Windows.Forms.Label] -and $prefix.Text -eq '.' -and $prefix.Parent -eq $group -and $input.BorderStyle -eq [Windows.Forms.BorderStyle]::None) 'Custom dot is not an integrated read-only prefix.'
    $input.Text = 'mySave9'; Wait-Save $form
    Assert ((Extension) -eq 'mySave9' -and (Field $form 'play').Enabled) 'Custom extension not persisted.'
    Assert ($info.Text.EndsWith("I hope you know what you're doing!")) 'Custom remark differs.'
    $input.Text = '../bad'; Wait-Save $form $true
    Assert ((Extension) -eq 'mySave9') 'Invalid custom value changed the active save setting.'
    $input.Text = 'mySave9'; Wait-Save $form
} finally { $form.Dispose() }
$form = New-Object LauncherWindow($temporary, $false)
try {
    Show-Fixture $form; Wait-Save $form
    Assert ((Field $form 'custom').Checked -and (Field $form 'customExtension').Text -eq 'mySave9') 'Custom choice lost after reopening.'
    Assert (-not (Field $form 'seamless').Checked) 'Co-op choice was altered by save mode.'
    (Field $form 'seamless').Checked = $true; Wait-Save $form
    Assert ((Field $form 'custom').Checked -and (Extension) -eq 'mySave9') 'Enabling co-op changed custom extension.'
    (Field $form 'sl2').Checked = $true; Wait-Save $form
    Assert ((Extension) -eq 'sl2' -and -not (Field $form 'customExtensionGroup').Visible) 'Returning to sl2 failed.'
} finally { $form.Dispose() }
Write-Output 'PASS: sl2 default, independent co-op toggle, sl2/co2/custom remarks, fixed dot, conditional input, autosave, invalid input blocking, and persistence across reopening. Fixture files only; no game launched.'
