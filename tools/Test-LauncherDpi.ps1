$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path 'D:\AI\Temp\Q-Souls-1\launcher-dpi' ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$probeSource = Join-Path $temporary 'DpiProbe.cs'
@"
using System;
using System.IO;
using System.Drawing;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]
static class DpiProbe {
    [DllImport("user32.dll")] static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] static extern bool AreDpiAwarenessContextsEqual(IntPtr a, IntPtr b);
    [STAThread] static void Main(string[] args) {
        try {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var assembly = Assembly.LoadFrom(args[0]);
            var target = (System.Runtime.Versioning.TargetFrameworkAttribute)Attribute.GetCustomAttribute(assembly, typeof(System.Runtime.Versioning.TargetFrameworkAttribute));
            if (target.FrameworkName != ".NETFramework,Version=v4.8") throw new Exception("Wrong framework target.");
            using (var form = (Form)Activator.CreateInstance(assembly.GetType("LauncherWindow"), new object[] { args[1], false })) {
                form.ShowInTaskbar = false;
                form.StartPosition = FormStartPosition.Manual;
                form.Location = new Point(-32000, -32000);
                form.Show(); Application.DoEvents();
            if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), new IntPtr(-4)))
                throw new Exception("PerMonitorV2 awareness was not enabled. Context=" + GetThreadDpiAwarenessContext() + "; OS=" + Environment.OSVersion + "; Config=" + AppDomain.CurrentDomain.SetupInformation.ConfigurationFile);

                float dpi;
                using (var graphics = form.CreateGraphics()) dpi = graphics.DpiX;
                int expected = (int)Math.Round(610 * dpi / 96);
                if (Math.Abs(form.ClientSize.Width - expected) > 3)
                    throw new Exception("Form did not scale from 96 DPI: width=" + form.ClientSize.Width + ", expected=" + expected);
                File.WriteAllText(args[2], "PASS: PerMonitorV2 enabled, .NET 4.8 target, native form scaling at " + dpi + " DPI (width " + form.ClientSize.Width + "). No game launched.");
            }
        } catch (Exception ex) { File.WriteAllText(args[2], "FAIL: " + ex); Environment.ExitCode = 1; }
    }
}
"@ | Set-Content -LiteralPath $probeSource -Encoding UTF8
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$probe = Join-Path $temporary 'DpiProbe.exe'
& $compiler /nologo /target:winexe /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /win32manifest:"$repository\launcher\Launcher.manifest" /out:$probe $probeSource
if ($LASTEXITCODE -ne 0) { throw 'DPI probe compilation failed.' }
Copy-Item -LiteralPath (Join-Path $repository '_launch-Q-Souls-1.exe.config') -Destination ($probe + '.config')
$resultPath = Join-Path $temporary 'result.txt'
$process = Start-Process -FilePath $probe -ArgumentList @(('"' + (Join-Path $repository '_launch-Q-Souls-1.exe') + '"'), ('"' + $temporary + '"'), ('"' + $resultPath + '"')) -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath $resultPath
if ($process.ExitCode -ne 0) { throw 'DPI awareness or scaling check failed.' }
