using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

public class RepoFile { public string Path; public string Sha; public long Size; }
public class PublishedVersion { public string Version; public string Commit; public string Tree; public List<RepoFile> Files; }
public class UpdatePlan { public string Repository; public PublishedVersion Release; public bool Repair; }
public class VersionCheck {
    public PublishedVersion Souls;
    public string SoulsError;
    public string CoopVersion;
    public string CoopError;
}

public static class RepositoryUpdates
{
    const string Api = "https://api.github.com/repos/WhiteG00se/Q-Souls-1/";
    const string Raw = "https://raw.githubusercontent.com/WhiteG00se/Q-Souls-1/";
    const long MaximumBytes = 512L * 1024 * 1024;
    public static JavaScriptSerializer Json() { return new JavaScriptSerializer { MaxJsonLength = 16 * 1024 * 1024 }; }
    static HttpWebRequest Request(string url) {
        ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
        var request = (HttpWebRequest)WebRequest.Create(url);
        request.UserAgent = "Q-Souls-1-Launcher";
        request.Accept = "application/vnd.github+json";
        request.Timeout = 15000; request.ReadWriteTimeout = 30000;
        return request;
    }
    static string Text(string url) {
        using (var response = Request(url).GetResponse())
        using (var stream = response.GetResponseStream())
        using (var reader = new StreamReader(stream)) {
            string text = reader.ReadToEnd();
            if (text.Length > 16 * 1024 * 1024) throw new IOException("Version metadata is too large.");
            return text;
        }
    }
    static Dictionary<string, object> Object(string url) { return Json().Deserialize<Dictionary<string, object>>(Text(url)); }
    static string Sha(string value) {
        if (!Regex.IsMatch(value ?? "", "^[0-9a-f]{40}$")) throw new IOException("Invalid GitHub revision.");
        return value;
    }
    public static Version ParseVersion(string text) {
        Version version;
        text = (text ?? "").Trim().TrimStart('v', 'V');
        if (!Regex.IsMatch(text, @"^\d+\.\d+(\.\d+){0,2}$") || !Version.TryParse(text, out version)) throw new IOException("Invalid version: " + text);
        return new Version(version.Major, version.Minor, Math.Max(0, version.Build), Math.Max(0, version.Revision));
    }
    public static bool Newer(string installed, string newest) { return ParseVersion(newest) > ParseVersion(installed); }
    public static string InstalledVersion(string root) { return File.ReadAllText(System.IO.Path.Combine(root, "VERSION.txt")).Trim(); }
    public static string NormalizeCoopVersion(string text) {
        Match match = Regex.Match(text ?? "", @"(?m)^>\s*(\d+)\.(\d)(\d+)\s+1\s*$");
        if (!match.Success) throw new IOException("The author's version information could not be read.");
        return match.Groups[1].Value + "." + match.Groups[2].Value + "." + Int32.Parse(match.Groups[3].Value);
    }
    static string EmbeddedCoopVersion(string dll) {
        // Read the DLL as data; never load or execute third-party code to identify it.
        if (new FileInfo(dll).Length > 64L * 1024 * 1024) return null;
        string data = Encoding.ASCII.GetString(File.ReadAllBytes(dll));
        if (!data.Contains("Dark Souls: Remastered Seamless Co-op mod by Yui")) return null;
        var versions = new HashSet<string>(StringComparer.Ordinal);
        // Three-part, complete C strings exclude certificate OIDs such as 2.5.4.3.
        foreach (Match match in Regex.Matches(data, @"(?<=\x00)v?\d+\.\d+\.\d+(?=\x00)")) {
            Version parsed;
            string version = match.Value.TrimStart('v');
            if (Version.TryParse(version, out parsed) && parsed > new Version(0, 0, 0)) versions.Add(version);
        }
        return versions.Count == 1 ? versions.Single() : null;
    }
    public static string CoopInstalled(string root) {
        string dll = System.IO.Path.Combine(root, "SeamlessCoop_here", "SeamlessCoop", "ds1sc.dll");
        if (!File.Exists(dll)) return "Not installed";
        var info = FileVersionInfo.GetVersionInfo(dll);
        foreach (string version in new[] { info.ProductVersion, info.FileVersion }) {
            Match match = Regex.Match(version ?? "", @"\d+\.\d+\.\d+(?:\.\d+)?");
            if (match.Success && ParseVersion(match.Value) > new Version(0, 0, 0, 0)) return "v" + match.Value;
        }
        string embedded = EmbeddedCoopVersion(dll);
        if (embedded != null) return "v" + embedded;
        string record = System.IO.Path.Combine(root, ".local", "seamless-version.json");
        if (File.Exists(record)) {
            var data = Json().Deserialize<Dictionary<string, string>>(File.ReadAllText(record));
            if (data.ContainsKey("Sha") && data.ContainsKey("Version") && BlobHash(dll) == data["Sha"]) return "v" + data["Version"];
        }
        return "Unknown (installed)";
    }
    public static void RecordCoopVersion(string root, string archive) {
        string name = System.IO.Path.GetFileNameWithoutExtension(archive);
        Match match = Regex.Match(name, @"(?:^|-)899-(\d+)-(\d+)-(\d+)(?:-|$)");
        if (!match.Success) match = Regex.Match(name, @"(?:^|[ _-])v(\d+)[.\-_](\d+)[.\-_](\d+)(?:[ _-]|$)", RegexOptions.IgnoreCase);
        if (!match.Success) return;
        string version = match.Groups[1].Value + "." + match.Groups[2].Value + "." + match.Groups[3].Value;
        ParseVersion(version);
        string dll = System.IO.Path.Combine(root, "SeamlessCoop_here", "SeamlessCoop", "ds1sc.dll");
        LauncherFiles.Write(System.IO.Path.Combine(root, ".local", "seamless-version.json"), Json().Serialize(new Dictionary<string, string> { { "Version", version }, { "Sha", BlobHash(dll) } }));
    }
    public static VersionCheck CheckVersions() {
        var result = new VersionCheck();
        try { result.Souls = FetchRelease("HEAD", true); } catch (Exception ex) { result.SoulsError = ex.Message; }
        try { result.CoopVersion = NormalizeCoopVersion(Text("https://raw.githubusercontent.com/yuiamoroll/DarkSouls1SeamlessCoopRelease/main/VERSION")); }
        catch (Exception ex) { result.CoopError = ex.Message; }
        return result;
    }
    public static PublishedVersion FetchRelease(string reference, bool files) {
        var commit = Object(Api + "commits/" + Uri.EscapeDataString(reference));
        var body = (Dictionary<string, object>)commit["commit"];
        var tree = (Dictionary<string, object>)body["tree"];
        var release = new PublishedVersion { Commit = Sha((string)commit["sha"]), Tree = Sha((string)tree["sha"]) };
        release.Version = Text(Raw + release.Commit + "/VERSION.txt").Trim();
        ParseVersion(release.Version);
        if (files) LoadFiles(release);
        return release;
    }
    static bool Protected(string path) {
        return path.Equals(".git", StringComparison.OrdinalIgnoreCase) || path.StartsWith(".git/", StringComparison.OrdinalIgnoreCase) ||
            path.Equals(".local", StringComparison.OrdinalIgnoreCase) || path.StartsWith(".local/", StringComparison.OrdinalIgnoreCase) ||
            (path.StartsWith("SeamlessCoop_here/", StringComparison.OrdinalIgnoreCase) && !path.Equals("SeamlessCoop_here/.gitkeep", StringComparison.OrdinalIgnoreCase));
    }
    public static string Target(string root, string relative) {
        if (String.IsNullOrEmpty(relative) || relative.Contains("\\") || relative.Contains(":") || relative.StartsWith("/") || Protected(relative)) throw new IOException("Protected or unsafe repository path: " + relative);
        foreach (string part in relative.Split('/')) {
            if (part == "." || part == ".." || part.Length == 0 || part.EndsWith(".") || part.EndsWith(" ") || part.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) >= 0 || Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)", RegexOptions.IgnoreCase)) throw new IOException("Unsafe repository filename.");
        }
        string path = System.IO.Path.GetFullPath(System.IO.Path.Combine(root, relative.Replace('/', System.IO.Path.DirectorySeparatorChar)));
        string prefix = System.IO.Path.GetFullPath(root).TrimEnd('\\') + "\\";
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new IOException("Repository path escaped its root.");
        LauncherFiles.RequireRealDirectory(System.IO.Path.GetDirectoryName(path));
        if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new IOException("Cannot replace a file link: " + relative);
        return path;
    }
    static void LoadFiles(PublishedVersion release) {
        var response = Object(Api + "git/trees/" + Sha(release.Tree) + "?recursive=1");
        if ((bool)response["truncated"]) throw new IOException("GitHub returned an incomplete file list.");
        release.Files = new List<RepoFile>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long size = 0;
        foreach (Dictionary<string, object> item in (System.Collections.IEnumerable)response["tree"]) {
            if ((string)item["type"] == "tree") continue;
            if ((string)item["type"] != "blob" || ((string)item["mode"] != "100644" && (string)item["mode"] != "100755")) throw new IOException("Repository links and submodules are not supported.");
            string path = (string)item["path"];
            if (Protected(path)) continue;
            Target(System.IO.Path.GetTempPath(), path);
            if (!seen.Add(path)) throw new IOException("Duplicate repository path.");
            long length = Convert.ToInt64(item["size"]);
            size = checked(size + length);
            if (length < 0 || size > MaximumBytes || release.Files.Count > 10000) throw new IOException("The published repository is too large.");
            release.Files.Add(new RepoFile { Path = path, Sha = Sha((string)item["sha"]), Size = length });
        }
    }
    public static string BlobHash(string path) {
        using (var sha = SHA1.Create())
        using (var stream = File.OpenRead(path)) {
            byte[] header = Encoding.ASCII.GetBytes("blob " + stream.Length + "\0");
            sha.TransformBlock(header, 0, header.Length, header, 0);
            byte[] buffer = new byte[81920]; int count;
            while ((count = stream.Read(buffer, 0, buffer.Length)) > 0) sha.TransformBlock(buffer, 0, count, buffer, 0);
            sha.TransformFinalBlock(new byte[0], 0, 0);
            return BitConverter.ToString(sha.Hash).Replace("-", "").ToLowerInvariant();
        }
    }
    public static string Verify(string root, PublishedVersion latest) { return VerifySnapshot(root, InstalledRelease(root, latest)); }
    public static PublishedVersion InstalledRelease(string root, PublishedVersion latest) {
        string installed = InstalledVersion(root);
        PublishedVersion release = latest;
        string state = System.IO.Path.Combine(root, ".local", "installed-release.json");
        if (File.Exists(state)) {
            var recorded = Json().Deserialize<PublishedVersion>(File.ReadAllText(state));
            if (ParseVersion(recorded.Version) == ParseVersion(installed)) release = FetchRelease(Sha(recorded.Commit), true);
        }
        if (ParseVersion(release.Version) != ParseVersion(installed)) {
            if (ParseVersion(installed) > ParseVersion(latest.Version)) throw new IOException("Installed v" + installed + " has not been published on GitHub yet. File verification is available after it is published.");
            var history = (System.Collections.IEnumerable)Json().DeserializeObject(Text(Api + "commits?path=VERSION.txt&per_page=100"));
            release = null;
            foreach (Dictionary<string, object> item in history) {
                var candidate = FetchRelease(Sha((string)item["sha"]), false);
                if (ParseVersion(candidate.Version) == ParseVersion(installed)) { LoadFiles(candidate); release = candidate; break; }
            }
            if (release == null) throw new IOException("No published file snapshot was found for installed v" + installed + ".");
        }
        return release;
    }
    public static string VerifySnapshot(string root, PublishedVersion release) {
        var problems = new List<string>(); int checkedFiles = 0;
        foreach (var file in release.Files) {
            string path = Target(root, file.Path); checkedFiles++;
            if (!File.Exists(path)) problems.Add("Missing: " + file.Path);
            else if (new FileInfo(path).Length != file.Size || BlobHash(path) != file.Sha) problems.Add("Changed: " + file.Path);
        }
        return "Verified " + checkedFiles + " published files for Q Souls 1 v" + release.Version + " (" + release.Commit.Substring(0, 8) + ")." + Environment.NewLine +
            (problems.Count == 0 ? "All files match. Local settings, ignored content and saves were not checked or changed." : String.Join(Environment.NewLine, problems));
    }
    public static List<RepoFile> DamagedFiles(string root, PublishedVersion release) {
        return release.Files.Where(file => {
            string path = Target(root, file.Path);
            return !File.Exists(path) || new FileInfo(path).Length != file.Size || BlobHash(path) != file.Sha;
        }).ToList();
    }
    static void RequireOutsideGame(string root) {
        LauncherFiles.RequireRealDirectory(root);
        for (string parent = System.IO.Path.GetFullPath(root); parent != null; parent = System.IO.Path.GetDirectoryName(parent))
            if (File.Exists(System.IO.Path.Combine(parent, "DarkSoulsRemastered.exe"))) throw new IOException("Keep the mod outside the game directory.");
        foreach (var game in Process.GetProcessesByName("DarkSoulsRemastered")) { game.Dispose(); throw new IOException("Close Dark Souls Remastered before updating."); }
    }
    public static bool Distributable(PublishedVersion release) {
        return release != null && release.Files != null && new[] { "_launch-Q-Souls-1.exe", "_launch-Q-Souls-1.exe.config", "VERSION.txt", "runtime/QSoulsRuntime.dll", "launcher/Start-Q-Souls-1.ps1" }.All(name => release.Files.Any(f => f.Path.Equals(name, StringComparison.OrdinalIgnoreCase)));
    }
    public static string Prepare(string root, PublishedVersion release) { return Prepare(root, release, false); }
    public static string Prepare(string root, PublishedVersion release, bool repair) {
        RequireOutsideGame(root);
        if (repair ? ParseVersion(InstalledVersion(root)) != ParseVersion(release.Version) : !Newer(InstalledVersion(root), release.Version)) throw new IOException("The published version does not match this operation.");
        if (!Distributable(release)) throw new IOException("The published version is missing a prebuilt launcher or required runtime files. Publish the complete repository first.");
        string job = System.IO.Path.Combine(root, ".local", "updates", Guid.NewGuid().ToString("N"));
        LauncherFiles.RequireRealDirectory(job);
        Directory.CreateDirectory(job);
        string zipPath = System.IO.Path.Combine(job, "repository.zip");
        using (var response = Request("https://api.github.com/repos/WhiteG00se/Q-Souls-1/zipball/" + Sha(release.Commit)).GetResponse())
        using (var input = response.GetResponseStream())
        using (var output = File.Create(zipPath)) {
            byte[] buffer = new byte[81920]; int count; long total = 0;
            while ((count = input.Read(buffer, 0, buffer.Length)) > 0) { total += count; if (total > MaximumBytes) throw new IOException("Repository download is too large."); output.Write(buffer, 0, count); }
        }
        Extract(zipPath, System.IO.Path.Combine(job, "files"), release);
        LauncherFiles.Write(System.IO.Path.Combine(job, "plan.json"), Json().Serialize(new UpdatePlan { Repository = System.IO.Path.GetFullPath(root), Release = release, Repair = repair }));
        File.Copy(System.IO.Path.Combine(root, "_launch-Q-Souls-1.exe"), System.IO.Path.Combine(job, "UpdateHelper.exe"));
        string helperConfig = System.IO.Path.Combine(root, "_launch-Q-Souls-1.exe.config");
        if (!File.Exists(helperConfig)) helperConfig = System.IO.Path.Combine(job, "files", "_launch-Q-Souls-1.exe.config");
        File.Copy(helperConfig, System.IO.Path.Combine(job, "UpdateHelper.exe.config"));
        return job;
    }
    public static void Extract(string zipPath, string destination, PublishedVersion release) {
        using (var zip = ZipFile.OpenRead(zipPath)) {
            var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);
            string prefix = null;
            foreach (var entry in zip.Entries) {
                string name = entry.FullName.Replace('\\', '/');
                int slash = name.IndexOf('/');
                if (slash < 1) throw new IOException("Invalid GitHub archive layout.");
                string currentPrefix = name.Substring(0, slash + 1);
                if (prefix == null) prefix = currentPrefix;
                if (prefix != currentPrefix) throw new IOException("Multiple repository roots in archive.");
                if (name.EndsWith("/")) continue;
                string relative = name.Substring(slash + 1);
                Target(destination, relative);
                if (((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000 || entries.ContainsKey(relative)) throw new IOException("Invalid or duplicate archive entry.");
                entries.Add(relative, entry);
            }
            long total = 0;
            foreach (var file in release.Files) {
                Sha(file.Sha);
                ZipArchiveEntry entry;
                if (!entries.TryGetValue(file.Path, out entry) || entry.Length != file.Size) throw new IOException("Missing or incorrect download: " + file.Path);
                total = checked(total + entry.Length);
                if (entry.Length < 0 || total > MaximumBytes) throw new IOException("Repository contents are too large.");
                string path = Target(destination, file.Path);
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path));
                using (var input = entry.Open())
                using (var output = new FileStream(path, FileMode.CreateNew)) {
                    byte[] buffer = new byte[81920]; int count; long written = 0;
                    while ((count = input.Read(buffer, 0, buffer.Length)) > 0) { written += count; if (written > file.Size) throw new IOException("Oversized archive entry."); output.Write(buffer, 0, count); }
                }
                if (new FileInfo(path).Length != file.Size || BlobHash(path) != file.Sha) throw new IOException("Downloaded file failed verification: " + file.Path);
            }
        }
    }
    static void ReplaceFile(string source, string destination) {
        string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".update";
        try {
            File.Copy(source, temporary, false);
            if (File.Exists(destination)) File.Replace(temporary, destination, null);
            else File.Move(temporary, destination);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public static string Apply(string job) {
        var plan = Json().Deserialize<UpdatePlan>(File.ReadAllText(System.IO.Path.Combine(job, "plan.json")));
        string root = System.IO.Path.GetFullPath(plan.Repository);
        string expected = System.IO.Path.Combine(root, ".local", "updates") + "\\";
        string fullJob = System.IO.Path.GetFullPath(job);
        if (!fullJob.StartsWith(expected, StringComparison.OrdinalIgnoreCase) || !Regex.IsMatch(fullJob.Substring(expected.Length), "^[a-f0-9]{32}$")) throw new IOException("Invalid update staging directory.");
        RequireOutsideGame(root); LauncherFiles.RequireRealDirectory(job);
        Sha(plan.Release.Commit);
        if ((plan.Repair ? ParseVersion(InstalledVersion(root)) != ParseVersion(plan.Release.Version) : !Newer(InstalledVersion(root), plan.Release.Version)) || !Distributable(plan.Release)) throw new IOException("This update is incomplete or no longer newer.");
        using (var updateLock = new FileStream(System.IO.Path.Combine(root, ".local", "update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) {
            var files = plan.Release.Files.OrderBy(f => f.Path.Equals("VERSION.txt", StringComparison.OrdinalIgnoreCase) ? 2 : f.Path.Equals("_launch-Q-Souls-1.exe", StringComparison.OrdinalIgnoreCase) ? 1 : 0).ToList();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in files) {
                if (!seen.Add(file.Path)) throw new IOException("Duplicate update path.");
                Target(root, file.Path);
                string staged = Target(System.IO.Path.Combine(job, "files"), file.Path);
                if (!File.Exists(staged) || new FileInfo(staged).Length != file.Size || BlobHash(staged) != file.Sha) throw new IOException("Staged update failed verification: " + file.Path);
            }
            if (ParseVersion(InstalledVersion(System.IO.Path.Combine(job, "files"))) != ParseVersion(plan.Release.Version)) throw new IOException("The staged version does not match the update plan.");
            var obsolete = new List<RepoFile>();
            string oldState = System.IO.Path.Combine(root, ".local", "installed-release.json");
            if (!plan.Repair && File.Exists(oldState)) {
                var previous = Json().Deserialize<PublishedVersion>(File.ReadAllText(oldState));
                if (previous.Files != null) foreach (var old in previous.Files) {
                    if (seen.Contains(old.Path)) continue;
                    string target = Target(root, old.Path);
                    // Preserve locally changed files even when upstream removed them.
                    if (File.Exists(target) && BlobHash(target) == old.Sha) obsolete.Add(old);
                }
            }
            var replaced = new List<RepoFile>();
            try {
                foreach (var file in obsolete) {
                    string target = Target(root, file.Path), backup = Target(System.IO.Path.Combine(job, "backup"), file.Path);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(backup)); File.Copy(target, backup, false);
                    File.Delete(target); replaced.Add(file);
                }
                foreach (var file in files) {
                    string target = Target(root, file.Path);
                    if (plan.Repair && File.Exists(target) && new FileInfo(target).Length == file.Size && BlobHash(target) == file.Sha) continue;
                    string backup = Target(System.IO.Path.Combine(job, "backup"), file.Path);
                    if (File.Exists(target)) { Directory.CreateDirectory(System.IO.Path.GetDirectoryName(backup)); File.Copy(target, backup, false); }
                    else if (Directory.Exists(target)) throw new IOException("A directory blocks update file: " + file.Path);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target));
                    ReplaceFile(Target(System.IO.Path.Combine(job, "files"), file.Path), target);
                    replaced.Add(file);
                }
                LauncherFiles.Write(System.IO.Path.Combine(root, ".local", "installed-release.json"), Json().Serialize(plan.Release));
                return (plan.Repair ? "Repaired Q Souls 1 v" : "Updated Q Souls 1 to v") + plan.Release.Version + ". Previous files are backed up in " + System.IO.Path.Combine(job, "backup");
            }
            catch (Exception failure) {
                var errors = new List<string>();
                replaced.Reverse();
                foreach (var file in replaced) {
                    try {
                        string target = Target(root, file.Path), backup = Target(System.IO.Path.Combine(job, "backup"), file.Path);
                        if (File.Exists(backup)) ReplaceFile(backup, target);
                        else if (File.Exists(target)) File.Delete(target);
                    } catch (Exception ex) { errors.Add(file.Path + ": " + ex.Message); }
                }
                if (errors.Count > 0) throw new IOException("Update failed. Some files need manual restoration from " + System.IO.Path.Combine(job, "backup") + ": " + String.Join("; ", errors), failure);
                throw new IOException("Update failed; previous files were restored. " + failure.Message, failure);
            }
        }
    }
}
