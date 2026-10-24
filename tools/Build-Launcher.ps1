$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /win32icon:"$repository\launcher\QS1.ico" "/resource:$repository\launcher\QS1.ico,QS1.ico" /win32manifest:"$repository\launcher\Launcher.manifest" /target:winexe /platform:x64 /optimize+ /warnaserror+ /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll /out:"$repository\_launch-Q-Souls-1.exe" "$repository\launcher\LauncherWindow.cs" "$repository\launcher\SeamlessInstaller.cs" "$repository\launcher\RepositoryUpdates.cs"
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed.' }
