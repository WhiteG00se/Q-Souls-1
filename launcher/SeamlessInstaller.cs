using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

public static class SeamlessInstaller
{
    const long MaximumBytes = 512L * 1024 * 1024;
    static readonly string[] Required = { "SeamlessCoop/ds1sc.dll", "SeamlessCoop/ds1sc_settings.ini", "SeamlessCoop/locale/english.json" };

    static string SafeName(ZipArchiveEntry entry)
    {
        string name = entry.FullName.Replace('\\', '/');
        if (String.IsNullOrEmpty(name) || name.StartsWith("/") || name.Contains(":")) throw new IOException("The ZIP contains an unsafe path.");
        string[] parts = name.TrimEnd('/').Split('/');
        foreach (string part in parts) {
            if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)", RegexOptions.IgnoreCase))
                throw new IOException("The ZIP contains an unsafe filename.");
        }
        if (((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
            throw new IOException("Links are not allowed in the ZIP.");
        return name;
    }
    static void CopyExisting(string source, string destination)
    {
        if (!Directory.Exists(source)) return;
        LauncherFiles.RequireRealDirectory(source);
        Directory.CreateDirectory(destination);
        foreach (string item in Directory.GetFileSystemEntries(source)) {
            if ((File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0) throw new IOException("Remove directory or file links from SeamlessCoop_here before installing.");
            string target = Path.Combine(destination, Path.GetFileName(item));
            if (Directory.Exists(item)) CopyExisting(item, target);
            else File.Copy(item, target, false);
        }
    }
    static void ValidatePackage(string root)
    {
        foreach (string name in Required) {
            string path = Path.Combine(root, name.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(path) || new FileInfo(path).Length == 0) throw new IOException("The ZIP is not a complete Dark Souls Remastered Seamless Co-op package: missing " + name);
        }
        string ini = File.ReadAllText(Path.Combine(root, "SeamlessCoop", "ds1sc_settings.ini"));
        if (LauncherFiles.ReadValue(ini, "PASSWORD", "cooppassword", null) == null ||
            !Regex.IsMatch(LauncherFiles.ReadValue(ini, "SAVE", "save_file_extension", ""), "^[a-zA-Z0-9]{1,120}$"))
            throw new IOException("The package has invalid Seamless settings.");
        var locale = new JavaScriptSerializer().DeserializeObject(File.ReadAllText(Path.Combine(root, "SeamlessCoop", "locale", "english.json")));
        if (!(locale is Dictionary<string, object>)) throw new IOException("The package has an invalid English locale.");
    }
    public static string Install(string repository, string archivePath)
    {
        string root = Path.GetFullPath(repository);
        LauncherFiles.RequireRealDirectory(root);
        for (string ancestor = root; ancestor != null; ancestor = Path.GetDirectoryName(ancestor))
            if (File.Exists(Path.Combine(ancestor, "DarkSoulsRemastered.exe"))) throw new IOException("Keep Q-Souls-1 outside the game directory.");
        if (!archivePath.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) throw new IOException("Select the original Seamless Co-op ZIP downloaded from Nexus.");
        if (Process.GetProcessesByName("DarkSoulsRemastered").Length != 0) throw new IOException("Close Dark Souls Remastered before installing co-op.");
        string local = Path.Combine(root, ".local");
        string destination = Path.Combine(root, "SeamlessCoop_here");
        LauncherFiles.RequireRealDirectory(local);
        LauncherFiles.RequireRealDirectory(destination);
        string job = Path.Combine(local, "seamless-install-" + Guid.NewGuid().ToString("N"));
        string payload = Path.Combine(job, "payload"), stage = Path.Combine(job, "stage");
        string backup = Path.Combine(local, "seamless-backups", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N"));
        bool moved = false;
        try {
            using (var zip = ZipFile.OpenRead(archivePath)) {
                if (zip.Entries.Count > 4096) throw new IOException("The ZIP has too many files.");
                long total = 0;
                var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                string prefix = null;
                foreach (var entry in zip.Entries) {
                    string name = SafeName(entry);
                    if (!names.Add(name.TrimEnd('/'))) throw new IOException("The ZIP contains duplicate paths.");
                    total = checked(total + entry.Length);
                    if (total > MaximumBytes) throw new IOException("The ZIP is too large.");
                    const string suffix = "SeamlessCoop/ds1sc.dll";
                    if (name.Equals(suffix, StringComparison.OrdinalIgnoreCase) || name.EndsWith("/" + suffix, StringComparison.OrdinalIgnoreCase)) {
                        if (prefix != null) throw new IOException("The ZIP contains multiple co-op packages.");
                        prefix = name.Substring(0, name.Length - suffix.Length);
                    }
                }
                if (prefix == null) throw new IOException("Select the Dark Souls Remastered Seamless Co-op ZIP, not a different mod or a source-code archive.");
                Directory.CreateDirectory(payload);
                foreach (var entry in zip.Entries) {
                    string name = entry.FullName.Replace('\\', '/');
                    if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
                    string relative = name.Substring(prefix.Length);
                    if (!relative.StartsWith("SeamlessCoop/", StringComparison.OrdinalIgnoreCase) && !relative.Equals("ds1sc_launcher.exe", StringComparison.OrdinalIgnoreCase)) continue;
                    string target = Path.GetFullPath(Path.Combine(payload, relative.Replace('/', Path.DirectorySeparatorChar)));
                    if (!target.StartsWith(payload + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new IOException("Unsafe ZIP path.");
                    if (name.EndsWith("/")) { Directory.CreateDirectory(target); continue; }
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (var input = entry.Open())
                    using (var output = new FileStream(target, FileMode.CreateNew)) {
                        byte[] buffer = new byte[81920];
                        long written = 0;
                        int count;
                        while ((count = input.Read(buffer, 0, buffer.Length)) > 0) {
                            written = checked(written + count);
                            if (written > entry.Length || written > MaximumBytes) throw new IOException("The ZIP contains an oversized entry.");
                            output.Write(buffer, 0, count);
                        }
                        if (written != entry.Length) throw new IOException("The ZIP contains a truncated entry.");
                    }
                }
            }
            ValidatePackage(payload);
            CopyExisting(destination, stage);
            Directory.CreateDirectory(stage);
            foreach (string file in Directory.GetFiles(payload, "*", SearchOption.AllDirectories)) {
                string relative = file.Substring(payload.Length + 1);
                string target = Path.Combine(stage, relative);
                // Preserve every existing INI, including passwords and gameplay settings.
                if (File.Exists(target) && Path.GetExtension(target).Equals(".ini", StringComparison.OrdinalIgnoreCase)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.Copy(file, target, true);
            }
            ValidatePackage(stage);
            if (Process.GetProcessesByName("DarkSoulsRemastered").Length != 0) throw new IOException("Close the game before installing co-op.");
            if (Directory.Exists(destination)) {
                LauncherFiles.RequireRealDirectory(Path.GetDirectoryName(backup));
                Directory.CreateDirectory(Path.GetDirectoryName(backup));
                Directory.Move(destination, backup); moved = true;
            }
            try { Directory.Move(stage, destination); }
            catch { if (moved) Directory.Move(backup, destination); throw; }
            return "Seamless Co-op installed. Your existing settings were preserved." + (moved ? Environment.NewLine + "Previous installation: " + backup : "");
        }
        finally {
            if (Directory.Exists(job)) {
                LauncherFiles.RequireRealDirectory(job);
                Directory.Delete(job, true);
            }
        }
    }
}
