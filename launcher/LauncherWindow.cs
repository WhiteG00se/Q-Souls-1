using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using System.Xml.Serialization;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

public class LauncherSettings { public bool ControllerHelper = false; public bool Seamless = true; public string SaveMode = "sl2"; public string CustomSaveExtension = ""; }

public static class LauncherFiles
{
    public static string ReadValue(string text, string section, string key, string fallback)
    {
        Match block = Regex.Match(text, @"(?ims)^\s*\[" + Regex.Escape(section) + @"\][^\r\n]*\r?\n(?<body>.*?)(?=^\s*\[|\z)");
        Match value = Regex.Match(block.Groups["body"].Value, @"(?im)^[ \t]*" + Regex.Escape(key) + @"[ \t]*=[ \t]*([^\r\n]*)");
        return value.Success ? value.Groups[1].Value.Trim() : fallback;
    }
    public static string SetPassword(string text, string password)
    {
        if (password.IndexOfAny(new char[] { '\r', '\n', '\0' }) >= 0)
            throw new InvalidOperationException("The password must be a single line.");
        Match section = Regex.Match(text, @"(?ims)^\s*\[PASSWORD\][^\r\n]*\r?\n(?<body>.*?)(?=^\s*\[|\z)");
        if (!section.Success) throw new InvalidOperationException("The Seamless settings are missing [PASSWORD].");
        Group body = section.Groups["body"];
        if (!Regex.IsMatch(body.Value, @"(?im)^\s*cooppassword\s*="))
            throw new InvalidOperationException("The Seamless settings are missing cooppassword.");
        string updated = Regex.Replace(body.Value, @"(?im)^([ \t]*cooppassword[ \t]*=[ \t]*)[^\r\n]*", m => m.Groups[1].Value + password);
        return text.Substring(0, body.Index) + updated + text.Substring(body.Index + body.Length);
    }
    public static string SetSaveExtension(string text, string extension) {
        if (!Regex.IsMatch(extension ?? "", "^[a-zA-Z0-9]{1,120}$")) throw new InvalidOperationException("Enter a save extension using 1 to 120 letters or numbers after the dot.");
        Match section = Regex.Match(text, @"(?ims)^\s*\[SAVE\][^\r\n]*\r?\n(?<body>.*?)(?=^\s*\[|\z)");
        if (!section.Success || !Regex.IsMatch(section.Groups["body"].Value, @"(?im)^[ \t]*save_file_extension[ \t]*=")) throw new InvalidOperationException("The Seamless settings are missing save_file_extension in [SAVE].");
        Group body = section.Groups["body"];
        string updated = Regex.Replace(body.Value, @"(?im)^([ \t]*save_file_extension[ \t]*=[ \t]*)[^\r\n]*", m => m.Groups[1].Value + extension);
        return text.Substring(0, body.Index) + updated + text.Substring(body.Index + body.Length);
    }
    public static void RequireRealDirectory(string path)
    {
        for (string item = Path.GetFullPath(path); item != null; item = Path.GetDirectoryName(item))
            if (Directory.Exists(item) && (File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Settings cannot be written through a directory link: " + item);
    }
    public static void Write(string path, string text)
    {
        RequireRealDirectory(Path.GetDirectoryName(path));
        if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Settings cannot be written through a file link.");
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            File.WriteAllText(temporary, text, new System.Text.UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temporary, path, null);
            else File.Move(temporary, path);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}

public static class LauncherTheme
{
    public static readonly Color Background = Color.FromArgb(17, 18, 20);
    public static readonly Color Surface = Color.FromArgb(26, 27, 29);
    public static readonly Color Input = Color.FromArgb(21, 22, 24);
    public static readonly Color Ink = Color.FromArgb(222, 216, 203);
    public static readonly Color Ember = Color.FromArgb(218, 155, 86);
    public static readonly Color Accent = Color.FromArgb(105, 63, 31);
    public static readonly Color AccentInk = Color.FromArgb(255, 229, 188);
    public static readonly Color Border = Color.FromArgb(66, 57, 47);
    [System.Runtime.InteropServices.DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
    [System.Runtime.InteropServices.DllImport("uxtheme.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr window, string app, string list);
    public static void Apply(Control control) {
        control.BackColor = control is Form ? Background : Surface;
        control.ForeColor = Ink;
        var text = control as TextBoxBase;
        if (text != null) { text.BackColor = Input; text.ForeColor = Ink; }
        var button = control as ButtonBase;
        if (button != null) {
            button.FlatStyle = FlatStyle.Flat;
            button.UseVisualStyleBackColor = false;
            button.FlatAppearance.BorderColor = Border;
            button.FlatAppearance.MouseOverBackColor = Color.FromArgb(57, 45, 34);
            button.FlatAppearance.MouseDownBackColor = Color.FromArgb(78, 55, 34);
            button.FlatAppearance.CheckedBackColor = Accent;
            var toggle = button as RadioButton;
            if (toggle != null) {
                Action refresh = delegate {
                    toggle.BackColor = toggle.Checked ? Accent : Surface;
                    toggle.ForeColor = toggle.Checked ? AccentInk : Ink;
                    toggle.FlatAppearance.MouseOverBackColor = toggle.Checked ? Accent : Color.FromArgb(57, 45, 34);
                    toggle.FlatAppearance.MouseDownBackColor = toggle.Checked ? Accent : Color.FromArgb(78, 55, 34);
                };
                toggle.CheckedChanged += delegate { refresh(); };
                refresh();
            }
            button.Padding = new Padding(3, 0, 3, 0);
            button.Paint += delegate(object sender, PaintEventArgs e) {
                if (button.Enabled) return;
                e.Graphics.Clear(button.BackColor);
                using (var pen = new Pen(Border)) e.Graphics.DrawRectangle(pen, 0, 0, button.Width - 1, button.Height - 1);
                TextRenderer.DrawText(e.Graphics, button.Text, button.Font, button.ClientRectangle,
                    Color.FromArgb(145, 139, 128), TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            };
        }
        control.HandleCreated += delegate {
            SetWindowTheme(control.Handle, "DarkMode_Explorer", null);
            if (control is Form) { int enabled = 1; DwmSetWindowAttribute(control.Handle, 20, ref enabled, 4); }
        };
        foreach (Control child in control.Controls) Apply(child);
    }
    public static void ShowMessage(string message, string title) {
        using (var dialog = new Form { Text = title, StartPosition = FormStartPosition.CenterScreen,
            ClientSize = new Size(480, 240), MinimizeBox = false, MaximizeBox = false,
            AutoScaleMode = AutoScaleMode.Dpi, Font = new Font("Segoe UI", 10) }) {
            var body = new TextBox { Text = message, Multiline = true, ReadOnly = true,
                ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BorderStyle = BorderStyle.None };
            var close = new Button { Text = "Close", Dock = DockStyle.Bottom, Height = 42, DialogResult = DialogResult.OK };
            dialog.Padding = new Padding(20); dialog.Controls.Add(body); dialog.Controls.Add(close);
            dialog.AcceptButton = close; dialog.CancelButton = close;
            Apply(dialog); dialog.ShowDialog();
        }
    }
}
// Draw the indicator explicitly so Windows themes cannot replace the accent color.
public sealed class AccentCheckBox : CheckBox
{
    protected override void OnPaint(PaintEventArgs e) {
        base.OnPaint(e);
        e.Graphics.Clear(BackColor);
        float scale = DeviceDpi / 96F;
        int side = (int)Math.Round(13 * scale);
        var box = new Rectangle(Padding.Left, (Height - side) / 2, side, side);
        using (var fill = new SolidBrush(Checked ? LauncherTheme.Accent : LauncherTheme.Input)) e.Graphics.FillRectangle(fill, box);
        using (var border = new Pen(LauncherTheme.Border)) e.Graphics.DrawRectangle(border, box);
        if (Checked) using (var pen = new Pen(Enabled ? LauncherTheme.AccentInk : SystemColors.GrayText, Math.Max(1, 2 * scale))) {
            e.Graphics.DrawLines(pen, new[] {
                new PointF(box.Left + 3 * scale, box.Top + 6 * scale),
                new PointF(box.Left + 5 * scale, box.Top + 9 * scale),
                new PointF(box.Left + 10 * scale, box.Top + 3 * scale)
            });
        }
        var text = new Rectangle(box.Right + (int)Math.Round(3 * scale), Padding.Top,
            Math.Max(0, Width - box.Right - (int)Math.Round(3 * scale) - Padding.Right), Height - Padding.Vertical);
        TextRenderer.DrawText(e.Graphics, Text, Font, text, Enabled ? ForeColor : SystemColors.GrayText,
            TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
        if (Focused && ShowFocusCues) ControlPaint.DrawFocusRectangle(e.Graphics, text, ForeColor, BackColor);
    }
}
// Match the textbox glyph spacing without Label's default horizontal padding.
public sealed class SaveExtensionPrefix : Label
{
    protected override void OnPaint(PaintEventArgs e) {
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, ForeColor,
            TextFormatFlags.NoPadding | TextFormatFlags.Right | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
    }
}
// Keep the full text accessible while drawing the version suffix in the accent face.
public sealed class VersionLabel : Label
{
    public bool FullAccent { get; set; }
    public bool AccentVersion { get; set; }
    static readonly TextFormatFlags Flags = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine;
    Match Suffix { get { return Regex.Match(Text, @"v\d+\.\d+(?:\.\d+){0,2}$"); } }
    Font AccentFont() { return new Font("Georgia", 13, Font.Style); }
    public override Size GetPreferredSize(Size proposedSize) {
        if (FullAccent) using (Font accent = AccentFont()) {
            Size size = TextRenderer.MeasureText(Text, accent, Size.Empty, Flags);
            return new Size(size.Width + Padding.Horizontal, size.Height + Padding.Vertical);
        }
        Match suffix = Suffix;
        if (!AccentVersion || !suffix.Success) return base.GetPreferredSize(proposedSize);
        using (Font accent = AccentFont()) {
            Size prefix = TextRenderer.MeasureText(Text.Substring(0, suffix.Index), Font, Size.Empty, Flags);
            Size version = TextRenderer.MeasureText(suffix.Value, accent, Size.Empty, Flags);
            return new Size(prefix.Width + version.Width + Padding.Horizontal, Math.Max(prefix.Height, version.Height) + Padding.Vertical);
        }
    }
    protected override void OnPaint(PaintEventArgs e) {
        if (FullAccent) {
            using (Font accent = AccentFont()) TextRenderer.DrawText(e.Graphics, Text, accent,
                new Rectangle(Padding.Left, Padding.Top, Math.Max(0, Width - Padding.Horizontal), Math.Max(0, Height - Padding.Vertical)),
                LauncherTheme.Ember, Flags | TextFormatFlags.VerticalCenter);
            return;
        }
        Match suffix = Suffix;
        if (!AccentVersion || !suffix.Success) { base.OnPaint(e); return; }
        string prefix = Text.Substring(0, suffix.Index);
        using (Font accent = AccentFont()) {
            int width = TextRenderer.MeasureText(e.Graphics, prefix, Font, Size.Empty, Flags).Width;
            var bounds = new Rectangle(Padding.Left, Padding.Top, Math.Max(0, Width - Padding.Horizontal), Math.Max(0, Height - Padding.Vertical));
            TextRenderer.DrawText(e.Graphics, prefix, Font, bounds, ForeColor, Flags | TextFormatFlags.VerticalCenter);
            bounds.X += width; bounds.Width = Math.Max(0, bounds.Width - width);
            TextRenderer.DrawText(e.Graphics, suffix.Value, accent, bounds, LauncherTheme.Ember, Flags | TextFormatFlags.VerticalCenter);
        }
    }
}
public sealed class LauncherWindow : Form
{
    readonly string repository, settingsPath, iniPath;
    readonly CheckBox seamless = new AccentCheckBox(), controllerHelper = new AccentCheckBox();
    readonly TextBox password = new TextBox();
    readonly RadioButton sl2 = new RadioButton(), co2 = new RadioButton(), custom = new RadioButton();
    readonly TextBox customExtension = new TextBox();
    readonly Panel customExtensionGroup = new Panel();
    readonly Label customExtensionPrefix = new SaveExtensionPrefix();
    readonly Label saveInfo = new Label(), status = new Label();
    readonly Label installedSouls = new VersionLabel(), newestSouls = new VersionLabel(), installedCoop = new VersionLabel(), newestCoop = new VersionLabel(), zipHint = new Label();
    readonly Button play = new Button(), check = new Button();
    readonly Button downloadCoop = new Button(), update = new Button();
    readonly RichTextBox details = new RichTextBox();
    readonly TableLayoutPanel layout, versionGroup, coopGroup, saveGroup;
    bool fittingContent, fitQueued;
    readonly BackgroundWorker worker = new BackgroundWorker(), versionWorker = new BackgroundWorker();
    readonly BackgroundWorker saveWorker = new BackgroundWorker();
    readonly System.Windows.Forms.Timer saveTimer = new System.Windows.Forms.Timer { Interval = 250 };
    sealed class SettingsChange { public bool Seamless; public bool ControllerHelper; public string Password; public string SaveMode; public string CustomExtension; public int Revision; }
    SettingsChange saving;
    int saveRevision, savedRevision;
    bool saveFailed, closeAfterSave, showInstallSuccess;
    const string PasswordPrompt = "Enter a co-op password. Everyone in your party must use the same password.";
    const string Sl2Remark = "Vanilla, works with Steam cloud sync.\r\nOnly load modded saves in the modded game and vice versa.";
    const string Co2Remark = "Default for Seemless Co-op. Doesn't work with Steam cloud sync.";
    const string CustomRemark = "You better know what you are doing!";
    const string InstallSuccess = "Seamless Co-op installed successfully";
    readonly Font boldVersionFont;
    PublishedVersion newest;
    bool checking, wasInstalled, restarting;
    readonly bool onlineChecks;

    public LauncherWindow(string root) : this(root, true) { }
    public LauncherWindow(string root, bool checkVersions)
    {
        SuspendLayout();
        repository = root; onlineChecks = checkVersions;
        settingsPath = Path.Combine(root, ".local", "launcher.xml");
        iniPath = Path.Combine(root, "SeamlessCoop_here", "SeamlessCoop", "ds1sc_settings.ini");
        Text = "Q Souls 1 Launcher";
        using (var stream = typeof(LauncherWindow).Assembly.GetManifestResourceStream("QS1.ico"))
        using (var icon = new Icon(stream)) Icon = (Icon)icon.Clone();
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        ClientSize = new Size(610, 400); MinimumSize = new Size(570, 0);
        AutoScaleDimensions = new SizeF(96F, 96F); AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 10);
        boldVersionFont = new Font(Font, FontStyle.Bold);
        layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 1, AutoScroll = true };
        layout.SuspendLayout();
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); Controls.Add(layout);
        versionGroup = CreateSettingsGroup(); coopGroup = CreateSettingsGroup(); saveGroup = CreateSettingsGroup();
        layout.Controls.Add(versionGroup); layout.Controls.Add(coopGroup); layout.Controls.Add(saveGroup);
        var headingRow = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0, 2, 0, 10) };
        headingRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        headingRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        headingRow.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        headingRow.Controls.Add(new VersionLabel { Text = "Q SOULS 1", FullAccent = true, AutoSize = true,
            Anchor = AnchorStyles.Left, Margin = new Padding(3, 0, 3, 0) }, 0, 0);
        headingRow.Controls.Add(new Label { Text = "for DARK SOULS: REMASTERED", AutoSize = true,
            Anchor = AnchorStyles.Right, Margin = new Padding(12, 0, 3, 0) }, 1, 0);
        versionGroup.Controls.Add(headingRow);
        string version = File.Exists(Path.Combine(root, "VERSION.txt")) ? File.ReadAllText(Path.Combine(root, "VERSION.txt")).Trim() : null;
        installedSouls.Text = "Installed: " + (String.IsNullOrWhiteSpace(version) ? "Unavailable" : "v" + version); installedSouls.AutoSize = true;
        newestSouls.Text = "Newest: Checking..."; newestSouls.AutoSize = true;
        var versions = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
            FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = Padding.Empty, Dock = DockStyle.Fill };
        versions.Controls.Add(installedSouls); versions.Controls.Add(newestSouls);
        var updateRow = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = Padding.Empty };
        updateRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        updateRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        updateRow.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        updateRow.Controls.Add(versions, 0, 0);
        update.Text = "Update to Q Souls 1"; update.AutoSize = true; update.Enabled = false;
        update.Click += delegate { BeginOperation("update"); };
        update.Anchor = AnchorStyles.Right;
        updateRow.Controls.Add(update, 1, 0); versionGroup.Controls.Add(updateRow);
        seamless.Text = "Enable Seamless Co-op"; seamless.AutoSize = true; coopGroup.Controls.Add(seamless);
        installedCoop.AutoSize = true; coopGroup.Controls.Add(installedCoop);
        newestCoop.Text = "Newest: Checking..."; newestCoop.AutoSize = true; coopGroup.Controls.Add(newestCoop);
        downloadCoop.Text = "Open Seamless Co-op Download Page"; downloadCoop.AutoSize = true;
        downloadCoop.Click += delegate {
            zipHint.Visible = true;
            try { Process.Start(new ProcessStartInfo("https://www.nexusmods.com/darksoulsremastered/mods/899?tab=files") { UseShellExecute = true }); }
            catch (Exception ex) { ShowError(ex); }
        };
        coopGroup.Controls.Add(downloadCoop);
        zipHint.Text = "Drag the downloaded ZIP file onto this window to install Seamless Co-op.";
        zipHint.AutoSize = true; zipHint.MaximumSize = new Size(550, 0); zipHint.Visible = false; coopGroup.Controls.Add(zipHint);
        AllowDrop = true;
        DragEnter += delegate(object sender, DragEventArgs e) {
            var files = e.Data.GetData(DataFormats.FileDrop) as string[];
            e.Effect = !worker.IsBusy && files != null && files.Length == 1 && files[0].EndsWith(".zip", StringComparison.OrdinalIgnoreCase) ? DragDropEffects.Copy : DragDropEffects.None;
        };
        DragDrop += delegate(object sender, DragEventArgs e) {
            var files = e.Data.GetData(DataFormats.FileDrop) as string[];
            if (files != null && files.Length == 1) InstallArchive(files[0]);
        };
        var passwordRow = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill };
        passwordRow.Controls.Add(new Label { Text = "Co-op password:", AutoSize = true, Margin = new Padding(0, 6, 12, 0) });
        password.Width = 230; password.UseSystemPasswordChar = false; passwordRow.Controls.Add(password);
        password.KeyDown += PasswordKeyDown;
        coopGroup.Controls.Add(passwordRow);
        controllerHelper.Text = "Controller detection helper"; controllerHelper.AutoSize = true;
        saveGroup.Controls.Add(controllerHelper);
        var saveModes = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill };
        saveModes.Controls.Add(new Label { Text = "Savegame:", AutoSize = true, Margin = new Padding(0, 7, 12, 0) });
        sl2.Text = ".sl2"; co2.Text = ".co2"; custom.Text = "custom";
        foreach (var mode in new[] { sl2, co2, custom }) { mode.Appearance = Appearance.Button; mode.AutoSize = true; mode.TextAlign = ContentAlignment.MiddleCenter; saveModes.Controls.Add(mode); }
        sl2.Checked = true; saveGroup.Controls.Add(saveModes);
        customExtensionGroup.BorderStyle = BorderStyle.FixedSingle; customExtensionGroup.BackColor = LauncherTheme.Input;
        customExtensionPrefix.Text = "."; customExtensionPrefix.Dock = DockStyle.Left; customExtensionPrefix.Width = 8;
        customExtensionPrefix.TextAlign = ContentAlignment.MiddleCenter; customExtensionPrefix.BackColor = LauncherTheme.Input; customExtensionPrefix.ForeColor = LauncherTheme.Ink;
        var customEntry = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 4, 4, 4), BackColor = LauncherTheme.Input };
        customExtension.BorderStyle = BorderStyle.None; customExtension.Dock = DockStyle.Fill;
        customEntry.Controls.Add(customExtension); customExtensionGroup.Controls.Add(customEntry); customExtensionGroup.Controls.Add(customExtensionPrefix);
        customExtensionPrefix.Click += delegate { customExtension.Focus(); };
        saveModes.Controls.Add(customExtensionGroup);
        saveInfo.AutoSize = false; saveInfo.Dock = DockStyle.Fill; saveInfo.MaximumSize = new Size(550, 0); saveGroup.Controls.Add(saveInfo);
        var openSaves = new Button { Text = "Open savefile folder", AutoSize = true };
        openSaves.Click += delegate {
            try {
                string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "NBGI", "DARK SOULS REMASTERED");
                if (!Directory.Exists(path)) throw new IOException("No save folder found. Create a character first.");
                Process.Start(new ProcessStartInfo("explorer.exe", "\"" + path + "\"") { UseShellExecute = true });
            } catch (Exception ex) { ShowError(ex); }
        };
        var actions = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill };
        play.Text = "Play"; check.Text = "Verify setup";
        foreach (Button button in new[] { play, openSaves, check }) { button.AutoSize = true; actions.Controls.Add(button); }
        saveGroup.Controls.Add(actions);
        status.AutoSize = true; status.MaximumSize = new Size(550, 0); status.Text = "Ready. Keep Steam running and signed in."; saveGroup.Controls.Add(status);
        details.Multiline = true; details.ReadOnly = true; details.DetectUrls = false; details.ScrollBars = RichTextBoxScrollBars.Vertical;
        details.AutoSize = false; details.Dock = DockStyle.Fill; details.MinimumSize = new Size(100, 0); layout.Controls.Add(details);
        FinishSettingsRows(versionGroup); FinishSettingsRows(coopGroup); FinishSettingsRows(saveGroup);
        layout.RowStyles.Clear();
        layout.RowCount = layout.Controls.Count;
        for (int i = 0; i < layout.Controls.Count; i++) {
            layout.SetCellPosition(layout.Controls[i], new TableLayoutPanelCellPosition(0, i));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        }
        details.TextChanged += delegate { ColorOutput(); RequestFit(); };
        layout.Layout += delegate { RequestFit(); };
        ClientSizeChanged += delegate { RequestFit(); };
        DpiChanged += delegate { ConstrainToScreen(); RequestFit(); };
        LocationChanged += delegate { ConstrainToScreen(); };
        Load += delegate { FitToContent(); };
        play.Click += delegate { Run(false); }; check.Click += delegate { if (SettingsReady()) { RefreshVersions(); BeginOperation("setup", seamless.Checked ? "Enabled" : "Disabled"); } };
        seamless.CheckedChanged += delegate { UpdateSaveInfo(); };
        FormClosing += delegate(object sender, FormClosingEventArgs e) {
            if (worker.IsBusy && !restarting) { e.Cancel = true; status.Text = "Please wait for the current operation to finish."; return; }
            if (SettingsSaving()) { e.Cancel = true; closeAfterSave = true; status.Text = "Saving settings before closing..."; }
        };
        worker.DoWork += RunBackend;
        worker.RunWorkerCompleted += OperationCompleted;
        versionWorker.DoWork += delegate(object sender, DoWorkEventArgs e) { e.Result = RepositoryUpdates.CheckVersions(); };
        versionWorker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
            if (IsDisposed) return;
            if (e.Error != null) { newestSouls.Text = "Newest: Unavailable"; newestCoop.Text = "Newest: Unavailable"; UpdateVersionStyles(); return; }
            var result = (VersionCheck)e.Result;
            newest = result.Souls;
            newestSouls.Text = "Newest: " + (newest == null ? "Unavailable" : "v" + newest.Version);
            newestCoop.Text = "Newest: " + (result.CoopVersion == null ? "Unavailable" : "v" + result.CoopVersion);
            UpdateVersionStyles();
            update.Text = newest == null ? "Update to Q Souls 1" : "Update to Q Souls 1 v" + newest.Version;
            UpdateButton();
            if (!worker.IsBusy && details.Text.Length == 0 && newest != null && !RepositoryUpdates.Distributable(newest)) details.Text = BulletOutput("The GitHub snapshot does not include the prebuilt GUI launcher yet. Publish the .exe and its .config with the repository to enable automatic updates.");
            if (!worker.IsBusy && details.Text.Length == 0 && (result.SoulsError != null || result.CoopError != null)) details.Text = BulletOutput("Version lookup: " + result.SoulsError + Environment.NewLine + result.CoopError + Environment.NewLine + "The launcher can still be used offline.");
        };
        string previousReport = Path.Combine(root, ".local", "setup-report.txt");
        if (File.Exists(previousReport)) details.Text = BulletOutput(File.ReadAllText(previousReport));
        RefreshInstallation();
        var settings = new LauncherSettings();
        if (File.Exists(settingsPath)) using (var reader = File.OpenRead(settingsPath)) settings = (LauncherSettings)new XmlSerializer(typeof(LauncherSettings)).Deserialize(reader);
        controllerHelper.Checked = settings.ControllerHelper;
        seamless.Checked = settings.Seamless && Installed();
        customExtension.Text = settings.CustomSaveExtension ?? "";
        if (settings.SaveMode == "co2") co2.Checked = true;
        else if (settings.SaveMode == "custom") custom.Checked = true;
        if (File.Exists(iniPath)) password.Text = LauncherFiles.ReadValue(File.ReadAllText(iniPath), "PASSWORD", "cooppassword", "");
        saveTimer.Tick += delegate { saveTimer.Stop(); StartSaving(); };
        saveWorker.DoWork += delegate(object sender, DoWorkEventArgs e) {
            var change = (SettingsChange)e.Argument;
            PersistSettings(change.Seamless, change.Password, change.SaveMode, change.CustomExtension, change.ControllerHelper);
        };
        saveWorker.RunWorkerCompleted += SaveCompleted;
        seamless.CheckedChanged += delegate { QueueSave(); };
        controllerHelper.CheckedChanged += delegate { QueueSave(); };
        password.TextChanged += delegate { QueueSave(); };
        foreach (var mode in new[] { sl2, co2, custom }) mode.CheckedChanged += delegate(object sender, EventArgs e) { if (((RadioButton)sender).Checked) { UpdateSaveInfo(); QueueSave(); } };
        customExtension.TextChanged += delegate { UpdateSaveInfo(); QueueSave(); };
        LauncherTheme.Apply(this);
        layout.BackColor = LauncherTheme.Background;
        play.BackColor = LauncherTheme.Accent; play.ForeColor = LauncherTheme.AccentInk;
        customExtensionGroup.BackColor = customExtensionPrefix.BackColor = customEntry.BackColor = LauncherTheme.Input;
        status.ForeColor = LauncherTheme.Ember;
        QueueSave();
        UpdateSaveInfo(); versionGroup.ResumeLayout(true); coopGroup.ResumeLayout(true); saveGroup.ResumeLayout(true); layout.ResumeLayout(true); ResumeLayout(true);
        Shown += delegate { FitToContent(); if (onlineChecks) RefreshVersions(); };
    }
    TableLayoutPanel CreateSettingsGroup() {
        var group = new TableLayoutPanel {
            ColumnCount = 1, AutoSize = false,
            Dock = DockStyle.Fill, Padding = new Padding(10), Margin = new Padding(3, 3, 3, 8),
            BackColor = LauncherTheme.Surface
        };
        group.SuspendLayout(); group.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        group.Paint += delegate(object sender, PaintEventArgs e) {
            using (var border = new Pen(LauncherTheme.Border)) e.Graphics.DrawRectangle(border, 0, 0, group.Width - 1, group.Height - 1);
        };
        group.Layout += delegate { RequestFit(); };
        return group;
    }
    static void FinishSettingsRows(TableLayoutPanel group) {
        group.RowStyles.Clear(); group.RowCount = group.Controls.Count;
        for (int i = 0; i < group.Controls.Count; i++) {
            group.SetCellPosition(group.Controls[i], new TableLayoutPanelCellPosition(0, i));
            group.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        }
    }
    int ContentRowHeight(Control control, int width, float scale) {
        if (control == zipHint && !zipHint.Visible) return 0;
        var flow = control as FlowLayoutPanel;
        if (flow != null) {
            // Measure the controls, never the old height of their container.
            flow.AutoSize = false;
            int used = flow.Padding.Left, rowHeight = 0, height = flow.Padding.Vertical;
            int available = Math.Max(1, width - flow.Padding.Horizontal);
            foreach (Control child in flow.Controls) {
                if (!child.Visible) continue;
                Size size;
                var input = child as TextBox;
                if (input != null) size = new Size(input.Width, input.PreferredHeight);
                else size = child.AutoSize ? child.GetPreferredSize(Size.Empty) : child.Size;
                int outerWidth = size.Width + child.Margin.Horizontal;
                if (flow.WrapContents && used > flow.Padding.Left && used - flow.Padding.Left + outerWidth > available) {
                    height += rowHeight; used = flow.Padding.Left; rowHeight = 0;
                }
                used += outerWidth;
                rowHeight = Math.Max(rowHeight, size.Height + child.Margin.Vertical);
                if (flow.GetFlowBreak(child)) { height += rowHeight; used = flow.Padding.Left; rowHeight = 0; }
            }
            return height + rowHeight;
        }
        if (control is VersionLabel) return control.GetPreferredSize(new Size(width, 0)).Height;
        var label = control as Label;
        if (label != null) {
            if (label.MaximumSize.Width > 0) width = Math.Min(width, label.MaximumSize.Width);
            return TextRenderer.MeasureText(label.Text, label.Font, new Size(Math.Max(1, width), Int32.MaxValue), TextFormatFlags.WordBreak | TextFormatFlags.NoPrefix | TextFormatFlags.TextBoxControl).Height + label.Padding.Vertical;
        }
        return control.GetPreferredSize(new Size(Math.Max(1, width), 0)).Height;
    }
    void SizeSettingsGroup(TableLayoutPanel group, float scale) {
        int width = Math.Max(1, layout.ClientSize.Width - layout.Padding.Horizontal - group.Margin.Horizontal - group.Padding.Horizontal);
        int height = group.Padding.Vertical;
        foreach (Control child in group.Controls) {
            int row = group.GetRow(child);
            int contentHeight = ContentRowHeight(child, Math.Max(1, width - child.Margin.Horizontal), scale);
            int rowHeight = contentHeight > 0 ? contentHeight + child.Margin.Vertical : 0;
            group.RowStyles[row].SizeType = SizeType.Absolute;
            group.RowStyles[row].Height = rowHeight;
            height += rowHeight;
        }
        RowStyle outerRow = layout.RowStyles[layout.GetRow(group)];
        outerRow.SizeType = SizeType.Absolute;
        outerRow.Height = height + group.Margin.Vertical;
    }
    void RequestFit() {
        if (fitQueued || fittingContent || !IsHandleCreated || IsDisposed) return;
        fitQueued = true;
        BeginInvoke(new Action(() => { fitQueued = false; if (!IsDisposed) FitToContent(); }));
    }
    protected override void WndProc(ref Message message) {
        base.WndProc(ref message);
        // Recheck after resolution, taskbar/work-area, or per-monitor DPI changes.
        if (message.Msg == 0x007E || message.Msg == 0x001A || message.Msg == 0x02E0) {
            ConstrainToScreen(); RequestFit();
        }
    }
    public static Rectangle ScreenBounds(Rectangle desired, Rectangle workingArea) {
        int width = Math.Min(Math.Max(1, desired.Width), workingArea.Width);
        int height = Math.Min(Math.Max(1, desired.Height), workingArea.Height);
        int left = Math.Max(workingArea.Left, Math.Min(desired.Left, workingArea.Right - width));
        int top = Math.Max(workingArea.Top, Math.Min(desired.Top, workingArea.Bottom - height));
        return new Rectangle(left, top, width, height);
    }
    public static Rectangle AvailableScreenArea(Rectangle workingArea, int dpi) {
        int margin = Math.Min((int)Math.Round(16 * dpi / 96F), Math.Max(0, (workingArea.Height - 1) / 4));
        return new Rectangle(workingArea.Left, workingArea.Top + margin, workingArea.Width, workingArea.Height - 2 * margin);
    }
    void ApplyScreenBounds(Rectangle desired) {
        Rectangle workingArea = AvailableScreenArea(Screen.FromControl(this).WorkingArea, DeviceDpi);
        Size minimum = new Size(Math.Min((int)Math.Round(570 * DeviceDpi / 96F), workingArea.Width), 0);
        if (MinimumSize != minimum) MinimumSize = minimum;
        if (MaximumSize != workingArea.Size) MaximumSize = workingArea.Size;
        desired.Width = Math.Max(minimum.Width, desired.Width);
        Rectangle fitted = ScreenBounds(desired, workingArea);
        if (Bounds != fitted) SetBounds(fitted.X, fitted.Y, fitted.Width, fitted.Height, BoundsSpecified.All);
    }
    void ConstrainToScreen() {
        if (fittingContent || !IsHandleCreated || IsDisposed || WindowState != FormWindowState.Normal) return;
        fittingContent = true;
        try { ApplyScreenBounds(Bounds); }
        finally { fittingContent = false; }
    }
    void FitToContent() {
        if (fittingContent || !IsHandleCreated || IsDisposed || WindowState != FormWindowState.Normal) return;
        fittingContent = true;
        try {
            ApplyScreenBounds(Bounds);
            float scale = DeviceDpi / 96F;
            int toggleGap = sl2.Margin.Right + co2.Margin.Left;
            int inputGap = (int)Math.Round(toggleGap * 2.5);
            int inset = (int)Math.Round(4 * scale);
            customExtensionPrefix.Width = (int)Math.Round(8 * scale);
            customExtensionGroup.Size = new Size(
                TextRenderer.MeasureText("0000000000", customExtension.Font, Size.Empty, TextFormatFlags.NoPadding).Width
                    + customExtensionPrefix.Width + inset + 2,
                custom.GetPreferredSize(Size.Empty).Height);
            int verticalInset = Math.Max(0, (customExtensionGroup.ClientSize.Height - customExtension.PreferredHeight) / 2);
            customExtension.Parent.Padding = new Padding(0, verticalInset, inset, verticalInset);
            int top = custom.Margin.Top;
            customExtensionGroup.Margin = new Padding(Math.Max(0, inputGap - custom.Margin.Right), top, sl2.Margin.Right, sl2.Margin.Bottom);
            for (int pass = 0; pass < 2; pass++) {
                int oldWidth = layout.ClientSize.Width;
                SizeSettingsGroup(versionGroup, scale); SizeSettingsGroup(coopGroup, scale); SizeSettingsGroup(saveGroup, scale);
                bool hasOutput = !String.IsNullOrWhiteSpace(details.Text);
                details.Visible = hasOutput;
                int outputHeight = 0;
                if (hasOutput) {
                    int textWidth = Math.Max(100, layout.ClientSize.Width - layout.Padding.Horizontal - details.Margin.Horizontal - SystemInformation.VerticalScrollBarWidth - (int)(12 * scale));
                    Size measured = TextRenderer.MeasureText(details.Text, details.Font, new Size(textWidth, Int32.MaxValue), TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl | TextFormatFlags.NoPrefix);
                    outputHeight = Math.Min(measured.Height + (int)(12 * scale), (int)(220 * scale));
                }
                RowStyle outputRow = layout.RowStyles[layout.GetRow(details)];
                outputRow.SizeType = SizeType.Absolute; outputRow.Height = hasOutput ? outputHeight + details.Margin.Vertical : 0;
                layout.AutoScrollMinSize = Size.Empty;
                layout.PerformLayout();
                int preferred = layout.Padding.Vertical;
                foreach (RowStyle row in layout.RowStyles) preferred += (int)Math.Ceiling(row.Height);
                int border = Height - ClientSize.Height;
                ApplyScreenBounds(new Rectangle(Left, Top, Width, preferred + border));
                layout.PerformLayout();
                if (oldWidth == layout.ClientSize.Width) break;
            }
        }
        finally { fittingContent = false; }
    }
    void PasswordKeyDown(object sender, KeyEventArgs e) {
        if (!e.Control || e.Alt || e.KeyCode != Keys.Back) return;
        // Standard TextBox otherwise inserts DEL (U+007F) for Ctrl+Backspace.
        e.Handled = true; e.SuppressKeyPress = true;
        if (password.SelectionLength > 0) { password.SelectedText = ""; return; }
        int end = password.SelectionStart, start = end;
        while (start > 0 && Char.IsWhiteSpace(password.Text[start - 1])) start--;
        if (start > 0) {
            bool word = Char.IsLetterOrDigit(password.Text[start - 1]) || password.Text[start - 1] == '_';
            while (start > 0 && !Char.IsWhiteSpace(password.Text[start - 1]) &&
                (Char.IsLetterOrDigit(password.Text[start - 1]) || password.Text[start - 1] == '_') == word) start--;
        }
        password.Select(start, end - start);
        if (password.SelectionLength > 0) password.SelectedText = "";
    }
    void RefreshVersions() { if (onlineChecks && !versionWorker.IsBusy) versionWorker.RunWorkerAsync(); }
    void RefreshInstallation() {
        bool installed = Installed();
        seamless.Enabled = installed && !worker.IsBusy;
        try { installedCoop.Text = "Installed: " + (installed ? RepositoryUpdates.CoopInstalled(repository) : "not installed"); }
        catch { installedCoop.Text = "Installed: Unknown (installed)"; }
        UpdateVersionStyles();
    }
    void UpdateVersionStyles() {
        StyleVersionPair(installedSouls, newestSouls);
        StyleVersionPair(installedCoop, newestCoop);
    }
    void StyleVersionPair(Label local, Label remote) {
        bool different = false;
        Match first = Regex.Match(local.Text, @"\sv(\d+\.\d+(?:\.\d+){0,2})$");
        Match second = Regex.Match(remote.Text, @"\sv(\d+\.\d+(?:\.\d+){0,2})$");
        if (first.Success && second.Success) {
            try { different = RepositoryUpdates.ParseVersion(first.Groups[1].Value) != RepositoryUpdates.ParseVersion(second.Groups[1].Value); }
            catch { }
        }
        ((VersionLabel)local).AccentVersion = ((VersionLabel)remote).AccentVersion = different;
        local.Font = remote.Font = different ? boldVersionFont : Font;
        local.Invalidate(); remote.Invalidate();
    }
    bool Installed() {
        foreach (string relative in new[] { "ds1sc.dll", "ds1sc_settings.ini", "locale\\english.json" })
            if (!File.Exists(Path.Combine(repository, "SeamlessCoop_here", "SeamlessCoop", relative))) return false;
        return true;
    }
    void UpdateButton() {
        bool available = false;
        try { available = RepositoryUpdates.Distributable(newest) && RepositoryUpdates.Newer(RepositoryUpdates.InstalledVersion(repository), newest.Version); } catch { }
        update.BackColor = available ? LauncherTheme.Accent : LauncherTheme.Surface;
        update.ForeColor = available ? LauncherTheme.AccentInk : LauncherTheme.Ink;
        update.Enabled = !worker.IsBusy && !SettingsSaving() && !saveFailed && available;
    }
    string SaveMode() { return co2.Checked ? "co2" : custom.Checked ? "custom" : "sl2"; }
    string SaveExtension() { return custom.Checked ? customExtension.Text : co2.Checked ? "co2" : "sl2"; }
    void UpdateSaveInfo() {
        password.Enabled = seamless.Checked && !worker.IsBusy;
        customExtensionGroup.Visible = custom.Checked;
        string extension = SaveExtension();
        string remark = custom.Checked ? CustomRemark : extension.Equals("sl2", StringComparison.OrdinalIgnoreCase) ? Sl2Remark : extension.Equals("co2", StringComparison.OrdinalIgnoreCase) ? Co2Remark : CustomRemark;
        saveInfo.Text = remark;
        RequestFit();
    }
    bool SettingsSaving() { return saveWorker.IsBusy || saveTimer.Enabled || (!saveFailed && savedRevision != saveRevision); }
    bool SettingsReady() { return !SettingsSaving() && !saveFailed && savedRevision == saveRevision; }
    void UpdatePlayButton() { play.Enabled = !worker.IsBusy && SettingsReady(); }
    void QueueSave() {
        saveRevision++; saveFailed = false;
        saveTimer.Stop(); saveTimer.Start();
        UpdatePlayButton(); UpdateButton();
    }
    void StartSaving() {
        if (saveWorker.IsBusy || IsDisposed) return;
        saving = new SettingsChange { ControllerHelper = controllerHelper.Checked, Seamless = seamless.Checked, Password = password.Text, SaveMode = SaveMode(), CustomExtension = customExtension.Text, Revision = saveRevision };
        try { saveWorker.RunWorkerAsync(saving); }
        catch (Exception ex) { saveFailed = true; closeAfterSave = false; ShowSaveError(ex); }
        UpdatePlayButton(); UpdateButton();
    }
    void SaveCompleted(object sender, RunWorkerCompletedEventArgs e) {
        if (IsDisposed) return;
        if (InvokeRequired) { BeginInvoke(new Action(() => SaveCompleted(sender, e))); return; }
        if (saving.Revision != saveRevision) {
            // A newer edit must reach disk before Play can be enabled.
            saveTimer.Stop(); StartSaving(); return;
        }
        if (e.Error != null) { saveFailed = true; closeAfterSave = false; ShowSaveError(e.Error); }
        else {
            savedRevision = saving.Revision; saveFailed = false;
            string passwordNotice = BulletOutput((showInstallSuccess ? InstallSuccess + Environment.NewLine : "") + PasswordPrompt);
            if (details.Text.Replace("\r", "") == passwordNotice.Replace("\r", "")) details.Text = showInstallSuccess ? BulletOutput(InstallSuccess) : "";
            SetSaveFeedback("\u2705 Settings saved");
        }
        UpdatePlayButton(); UpdateButton();
        if (closeAfterSave && SettingsReady()) { closeAfterSave = false; Close(); }
    }
    void SetSaveFeedback(string message) {
        var lines = new System.Collections.Generic.List<string>();
        foreach (string line in details.Text.Replace("\r", "").Split('\n')) {
            string content = line.Trim().TrimStart('\u2022', ' ');
            if (content.Length == 0 || content.StartsWith("\u2705 Settings saved") || content.StartsWith("\u26A0\uFE0F Could not save settings.")) continue;
            lines.Add(line);
        }
        lines.Add(message);
        details.Text = String.Join(Environment.NewLine, lines);
    }
    void ColorOutput() {
        details.SelectAll(); details.SelectionColor = LauncherTheme.Ink;
        int offset = 0;
        foreach (string line in details.Lines) {
            string content = line.TrimStart('\u2022', ' ');
            int symbol = offset + line.Length - content.Length;
            if (content.StartsWith("\u2705")) {
                details.Select(symbol, 1); details.SelectionColor = Color.FromArgb(142, 192, 145);
            }
            else if (content.StartsWith("\u26A0")) {
                details.Select(symbol, content.StartsWith("\u26A0\uFE0F") ? 2 : 1); details.SelectionColor = LauncherTheme.Ember;
            }
            offset += line.Length + 1;
        }
        details.Select(0, 0);
    }
    void ShowSaveError(Exception ex) {
        if (ex is InvalidOperationException && ex.Message == PasswordPrompt) {
            details.Text = BulletOutput((showInstallSuccess ? InstallSuccess + Environment.NewLine : "") + PasswordPrompt);
        }
        else {
            SetSaveFeedback("\u26A0\uFE0F Could not save settings. Change a setting to retry. " + ex.Message);
        }
        UpdatePlayButton(); UpdateButton();
    }
    void PersistSettings(bool enabled, string selectedPassword, string mode, string customValue, bool helper) {
        string extension = mode == "custom" ? customValue : mode;
        if (!Regex.IsMatch(extension ?? "", "^[a-zA-Z0-9]{1,120}$")) throw new InvalidOperationException("Enter a save extension using 1 to 120 letters or numbers after the dot.");
        if (enabled && String.IsNullOrWhiteSpace(selectedPassword)) throw new InvalidOperationException(PasswordPrompt);
        if (enabled && !Installed()) throw new IOException("The Seamless Co-op installation is incomplete.");
        if (Installed()) {
            string original = File.ReadAllText(iniPath), updated = LauncherFiles.SetSaveExtension(LauncherFiles.SetPassword(original, selectedPassword), extension);
            if (updated != original) LauncherFiles.Write(iniPath, updated);
        }
        var serializer = new XmlSerializer(typeof(LauncherSettings));
        using (var writer = new StringWriter()) {
            serializer.Serialize(writer, new LauncherSettings { ControllerHelper = helper, Seamless = enabled, SaveMode = mode, CustomSaveExtension = customValue });
            LauncherFiles.Write(settingsPath, writer.ToString().Replace("encoding=\"utf-16\"", "encoding=\"utf-8\""));
        }
    }
    protected override void Dispose(bool disposing) {
        if (disposing) saveTimer.Dispose();
        base.Dispose(disposing);
        if (disposing && boldVersionFont != null) boldVersionFont.Dispose();
    }
    void SetBusy(bool busy) {
        check.Enabled = downloadCoop.Enabled = !busy;
        UpdatePlayButton();
        seamless.Enabled = !busy && Installed(); password.Enabled = !busy && seamless.Checked;
        controllerHelper.Enabled = !busy;
        sl2.Enabled = co2.Enabled = custom.Enabled = customExtension.Enabled = !busy;
        UseWaitCursor = busy; UpdateButton();
    }
    void Run(bool validateOnly) {
        if (worker.IsBusy || !SettingsReady()) return;
        try {
            UpdateSaveInfo(); checking = validateOnly;
            details.Clear(); status.Text = validateOnly ? "Checking setup..." : "Preparing game...";
            worker.RunWorkerAsync(seamless.Checked); SetBusy(true);
        } catch (Exception ex) { SetBusy(false); ShowError(ex); }
    }
    void InstallArchive(string archive) {
        if (worker.IsBusy) return;
        if (SettingsSaving()) { status.Text = "Wait for settings to finish saving, then drop the ZIP again."; return; }
        wasInstalled = Installed();
        BeginOperation("install", archive);
    }
    void BeginOperation(string action, string archive = null) {
        if (worker.IsBusy) return;
        try {
            if (action == "update" && !SettingsReady()) return;
            showInstallSuccess = false;
            details.Clear();
            status.Text = action == "install" ? "Validating and installing Seamless Co-op..." : action == "setup" ? "Verifying setup and repairing mod files..." : "Downloading and verifying update...";
            worker.RunWorkerAsync(new string[] { action, archive }); SetBusy(true);
        } catch (Exception ex) { SetBusy(false); ShowError(ex); }
    }
    void RunBackend(object sender, DoWorkEventArgs e) {
        var action = e.Argument as string[];
        if (action != null) {
            if (action[0] == "install") { string result = SeamlessInstaller.Install(repository, action[1]); try { RepositoryUpdates.RecordCoopVersion(repository, action[1]); } catch { result += Environment.NewLine + "Installed version could not be recorded; it will display as Unknown."; } e.Result = new string[] { "install", result }; return; }
            if (action[0] == "setup") { e.Result = VerifySetup(repository, action[1] == "Enabled", newest); return; }
            var release = newest ?? RepositoryUpdates.FetchRelease("HEAD", true);
            e.Result = new string[] { "update", RepositoryUpdates.Prepare(repository, release) }; return;
        }
        e.Result = ExecuteGame(repository, (bool)e.Argument, checking);
    }
    public static Tuple<int, string> ExecuteGame(string repository, bool useCoop, bool validateOnly) {
        string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe");
        string script = Path.Combine(repository, "launcher", "Start-Q-Souls-1.ps1");
        var start = new ProcessStartInfo(powershell, "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + "\" -CoopMode " + (useCoop ? "Enabled" : "Disabled") + (validateOnly ? " -CheckOnly" : "")) {
            WorkingDirectory = repository, UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardOutput = true, RedirectStandardError = true
        };
        using (var process = Process.Start(start)) {
            var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync(); process.WaitForExit();
            return Tuple.Create(process.ExitCode, stdout.Result + stderr.Result);
        }
    }
    public static string BulletOutput(string text) {
        var lines = (text ?? "").Replace("\r", "").Split('\n');
        var output = new System.Collections.Generic.List<string>();
        foreach (string raw in lines) { string line = raw.Trim(); if (line.Length > 0) output.Add(line.StartsWith("\u2022 ") ? line : "\u2022 " + line); }
        return String.Join(Environment.NewLine, output);
    }
    public static string[] VerifySetup(string root, bool useCoop, PublishedVersion latest) {
        var report = new System.Text.StringBuilder();
        Tuple<int, string> game = null;
        try { game = ExecuteGame(root, useCoop, true); report.AppendLine(game.Item2); }
        catch (Exception ex) { report.AppendLine("Game setup check failed: " + ex.Message); }
        try {
            var release = RepositoryUpdates.InstalledRelease(root, latest ?? RepositoryUpdates.FetchRelease("HEAD", true));
            var damaged = RepositoryUpdates.DamagedFiles(root, release);
            if (damaged.Count == 0) {
                report.AppendLine(RepositoryUpdates.VerifySnapshot(root, release));
                return new[] { "setup", report.ToString(), game != null && game.Item1 == 0 ? "Passed" : "Failed" };
            }
            report.AppendLine("Found " + damaged.Count + " missing or changed mod files. Downloading matching files from GitHub.");
            string job = RepositoryUpdates.Prepare(root, release, true);
            bool restart = damaged.Exists(file => file.Path.Equals("_launch-Q-Souls-1.exe", StringComparison.OrdinalIgnoreCase) || file.Path.Equals("_launch-Q-Souls-1.exe.config", StringComparison.OrdinalIgnoreCase));
            if (restart) return new[] { "repair", job, report.ToString() };
            report.AppendLine(RepositoryUpdates.Apply(job));
            report.AppendLine(RepositoryUpdates.VerifySnapshot(root, release));
            game = ExecuteGame(root, useCoop, true);
            report.AppendLine("Setup check after repair:"); report.AppendLine(game.Item2);
            return new[] { "setup", report.ToString(), game.Item1 == 0 ? "Passed" : "Failed" };
        }
        catch (Exception ex) { report.AppendLine("Mod file verification/repair could not finish: " + ex.Message); return new[] { "setup", report.ToString(), "Failed" }; }
    }
    void OperationCompleted(object sender, RunWorkerCompletedEventArgs e) {
        if (IsDisposed) return;
        if (InvokeRequired) { BeginInvoke(new Action(() => OperationCompleted(sender, e))); return; }
        SetBusy(false);
        if (e.Error != null) { ShowError(e.Error); return; }
        var action = e.Result as string[];
        if (action != null) {
            if (action[0] == "update" || action[0] == "repair") {
                try {
                    string helper = Path.Combine(action[1], "UpdateHelper.exe");
                    Process.Start(new ProcessStartInfo(helper, "--apply-update \"" + action[1] + "\" " + Process.GetCurrentProcess().Id) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = action[1] });
                    restarting = true; Close();
                } catch (Exception ex) { ShowError(ex); }
                return;
            }
            details.Text = BulletOutput(action[1]);
            if (action[0] == "setup") { status.Text = action[2] == "Passed" ? "Setup verified. See details below." : "Setup verification needs attention. See details below."; return; }
            showInstallSuccess = true;
            RefreshInstallation();
            if (!wasInstalled) { password.Text = LauncherFiles.ReadValue(File.ReadAllText(iniPath), "PASSWORD", "cooppassword", ""); seamless.Checked = true; }
            UpdateSaveInfo();
            bool needsPassword = seamless.Checked && String.IsNullOrWhiteSpace(password.Text);
            details.Text = BulletOutput(InstallSuccess + (needsPassword ? Environment.NewLine + PasswordPrompt : ""));
            status.Text = needsPassword ? PasswordPrompt : InstallSuccess;
            return;
        }
        var result = (Tuple<int, string>)e.Result;
        details.Text = BulletOutput(result.Item2);
        status.Text = result.Item1 == 0 ? (checking ? "Setup checks passed. Game was not launched." : "Game launched.") : "Could not complete the operation. See details below.";
        if (result.Item1 == 0 && !checking) Close();
    }
    void ShowError(Exception ex) { status.Text = ex.Message; details.Text = BulletOutput(ex.ToString()); }
}

public sealed class UpdateWindow : Form
{
    public UpdateWindow(string job, int parentId) {
        Text = "Q Souls 1 Update";
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false; ClientSize = new Size(420, 100);
        AutoScaleDimensions = new SizeF(96, 96); AutoScaleMode = AutoScaleMode.Dpi;
        using (var stream = typeof(LauncherWindow).Assembly.GetManifestResourceStream("QS1.ico"))
        using (var icon = new Icon(stream)) Icon = (Icon)icon.Clone();
        var label = new Label { Dock = DockStyle.Fill, Padding = new Padding(16), Text = "Installing the verified update...", AutoSize = false };
        Controls.Add(label); LauncherTheme.Apply(this);
        var worker = new BackgroundWorker();
        FormClosing += delegate(object sender, FormClosingEventArgs e) { if (worker.IsBusy) e.Cancel = true; };
        worker.DoWork += delegate(object sender, DoWorkEventArgs e) {
            Process parent = null;
            try { parent = Process.GetProcessById(parentId); } catch (ArgumentException) { }
            if (parent != null) using (parent) { if (!parent.WaitForExit(60000)) throw new IOException("Close the launcher before updating. No files were changed."); }
            string applied = RepositoryUpdates.Apply(job);
            var plan = RepositoryUpdates.Json().Deserialize<UpdatePlan>(File.ReadAllText(Path.Combine(job, "plan.json")));
            if (plan.Repair) {
                bool coop = false;
                string settings = Path.Combine(plan.Repository, ".local", "launcher.xml");
                if (File.Exists(settings)) using (var input = File.OpenRead(settings)) coop = ((LauncherSettings)new XmlSerializer(typeof(LauncherSettings)).Deserialize(input)).Seamless;
                var checks = LauncherWindow.ExecuteGame(plan.Repository, coop, true);
                LauncherFiles.Write(Path.Combine(plan.Repository, ".local", "setup-report.txt"), LauncherWindow.BulletOutput(applied + Environment.NewLine + RepositoryUpdates.VerifySnapshot(plan.Repository, plan.Release) + Environment.NewLine + checks.Item2));
            }
            e.Result = applied;
        };
        worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
            if (e.Error != null) LauncherTheme.ShowMessage(e.Error.Message, Text);
            else {
                try {
                    var plan = RepositoryUpdates.Json().Deserialize<UpdatePlan>(File.ReadAllText(Path.Combine(job, "plan.json")));
                    Process.Start(new ProcessStartInfo(Path.Combine(plan.Repository, "_launch-Q-Souls-1.exe")) { UseShellExecute = true, WorkingDirectory = plan.Repository });
                } catch (Exception ex) { LauncherTheme.ShowMessage("Update installed. Reopen the launcher manually. " + ex.Message, Text); }
            }
            Close();
        };
        Shown += delegate { worker.RunWorkerAsync(); };
    }
}

static class LauncherEntry
{
    [STAThread]
    static void Main(string[] args) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        try {
            if (args.Length == 3 && args[0] == "--apply-update") Application.Run(new UpdateWindow(args[1], Int32.Parse(args[2])));
            else Application.Run(new LauncherWindow(AppDomain.CurrentDomain.BaseDirectory));
        } catch (Exception ex) { LauncherTheme.ShowMessage(ex.Message, "Q Souls 1 Launcher"); }
    }
}