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
    static int LastVisibleBottom(Control parent, Control group) {
        int bottom = group.Padding.Top;
        foreach (Control child in parent.Controls) {
            if (!child.Visible) continue;
            if (child is Panel && child.HasChildren) bottom = Math.Max(bottom, LastVisibleBottom(child, group));
            else bottom = Math.Max(bottom, group.PointToClient(child.PointToScreen(new Point(0, child.Height))).Y);
        }
        return bottom;
    }
    static void CheckGroupGaps(Form form, float dpi) {
        foreach (string name in new[] { "versionGroup", "coopGroup", "saveGroup" }) {
            var group = (Control)form.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(form);
            int gap = group.ClientSize.Height - group.Padding.Bottom - LastVisibleBottom(group, group);
            if (gap < 0 || gap > Math.Ceiling(12 * dpi / 96)) throw new Exception(name + " retains excess blank space: " + gap + " px beyond its padding.");
        }
    }
    [STAThread] static void Main(string[] args) {
        try {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var assembly = Assembly.LoadFrom(args[0]);
            var target = (System.Runtime.Versioning.TargetFrameworkAttribute)Attribute.GetCustomAttribute(assembly, typeof(System.Runtime.Versioning.TargetFrameworkAttribute));
            if (target.FrameworkName != ".NETFramework,Version=v4.8") throw new Exception("Wrong framework target.");
            using (var form = (Form)Activator.CreateInstance(assembly.GetType("LauncherWindow"), new object[] { args[1], false })) {
                form.Opacity = 0;
                form.ShowInTaskbar = false;
                form.StartPosition = FormStartPosition.Manual;
                form.Location = new Point(-32000, -32000);
                form.Show(); Application.DoEvents();
            if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), new IntPtr(-4)))
                throw new Exception("PerMonitorV2 awareness was not enabled. Context=" + GetThreadDpiAwarenessContext() + "; OS=" + Environment.OSVersion + "; Config=" + AppDomain.CurrentDomain.SetupInformation.ConfigurationFile);

                if (!Screen.FromControl(form).WorkingArea.Contains(form.Bounds)) throw new Exception("Launcher extends beyond its monitor working area.");
                float dpi;
                using (var graphics = form.CreateGraphics()) dpi = graphics.DpiX;
                int expected = (int)Math.Round(610 * dpi / 96);
                if (Math.Abs(form.ClientSize.Width - expected) > 3)
                    throw new Exception("Form did not scale from 96 DPI: width=" + form.ClientSize.Width + ", expected=" + expected);
                CheckGroupGaps(form, dpi);
                var fields = BindingFlags.Instance | BindingFlags.NonPublic;
                ((RadioButton)form.GetType().GetField("co2", fields).GetValue(form)).Checked = true;
                Application.DoEvents(); CheckGroupGaps(form, dpi);
                ((TextBox)form.GetType().GetField("customExtension", fields).GetValue(form)).Text = "backup";
                ((RadioButton)form.GetType().GetField("custom", fields).GetValue(form)).Checked = true;
                Application.DoEvents(); CheckGroupGaps(form, dpi);
                using (var bitmap = new Bitmap(form.Width, form.Height)) {
                    form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
                    bitmap.Save(Path.Combine(args[1], "compact-dpi.png"));
                }
                File.WriteAllText(args[2], "PASS: PerMonitorV2 enabled, .NET 4.8 target, native form scaling at " + dpi + " DPI (width " + form.ClientSize.Width + "); all settings groups fit their visible contents in sl2, co2, and custom modes. No game launched.");
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
