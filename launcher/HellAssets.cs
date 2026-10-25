using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Xml;

// Reconstruct arena assets from the user's installed game; no game assets are shipped.
public static class HellAssets
{
    public const int HellText = 15000991, FirelinkText = 15000992;
    public const int ToHellFlag = 11025950, ToFirelinkFlag = 11415950;
    const int MenuIndex = 13;
    static void Need(bool ok, string message) { if (!ok) throw new InvalidDataException("Cursed Arena: " + message); }
    static int I(byte[] data, int offset) { Need(offset >= 0 && offset <= data.Length - 4, "invalid asset bounds."); return BitConverter.ToInt32(data, offset); }
    static void Put(byte[] data, int offset, int value) { Array.Copy(BitConverter.GetBytes(value), 0, data, offset, 4); }
    static byte[] Slice(byte[] data, int offset, int length) { Need(offset >= 0 && length >= 0 && offset <= data.Length - length, "invalid asset range."); byte[] result = new byte[length]; Array.Copy(data, offset, result, 0, length); return result; }
    static string Hash(byte[] data) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant(); }
    static byte[] ReadGame(string game, string relative)
    {
        Need(!Path.IsPathRooted(relative) && relative.IndexOf(':') < 0 && relative.IndexOf("..", StringComparison.Ordinal) < 0, "invalid source path.");
        string root = Path.GetFullPath(game).TrimEnd('\\') + "\\", path = Path.GetFullPath(Path.Combine(root, relative.Replace('/', '\\')));
        Need(path.StartsWith(root, StringComparison.OrdinalIgnoreCase), "source escaped game directory.");
        Need(File.Exists(path), "missing required game component: " + relative + ". Install the game or provide the correct path.");
        return File.ReadAllBytes(path);
    }
    static bool Compressed(byte[] b) { return b.Length >= 4 && b[0] == 'D' && b[1] == 'C' && b[2] == 'X' && b[3] == 0; }
    public static byte[] Reconstruct(string game, string recipe)
    {
        var settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 8 * 1024 * 1024 };
        var document = new XmlDocument { XmlResolver = null };
        using (var reader = XmlReader.Create(recipe, settings)) document.Load(reader);
        XmlElement root = document.DocumentElement;
        Need(root != null && root.Name == "hell" && root.GetAttribute("version") == "1", "unsupported recipe.");
        int size = int.Parse(root.GetAttribute("size")), compression = int.Parse(root.GetAttribute("compression"));
        Need(size > 0 && size <= 64 * 1024 * 1024, "invalid output size.");
        var sources = new List<byte[]>(); var headers = new List<byte[]>();
        foreach (XmlElement e in root.SelectNodes("source")) {
            byte[] original = ReadGame(game, e.GetAttribute("path"));
            int offset = int.Parse(e.GetAttribute("offset")), length = int.Parse(e.GetAttribute("length"));
            byte[] b = length == 0 ? original : Slice(original, offset, length);
            headers.Add(b);
            if (bool.Parse(e.GetAttribute("inflate")) && Compressed(b)) b = StartingClasses.Unpack(b);
            Need(Hash(b) == e.GetAttribute("sha256"), "unsupported or modified game asset: " + e.GetAttribute("path") + ". Arena generation stopped.");
            sources.Add(b);
        }
        using (var output = new MemoryStream()) {
            foreach (XmlNode node in root.ChildNodes) {
                XmlElement e = node as XmlElement; if (e == null || e.Name == "source") continue;
                byte[] part;
                if (e.Name == "copy") {
                    int index = int.Parse(e.GetAttribute("source")); Need(index >= 0 && index < sources.Count, "invalid source index.");
                    part = Slice(sources[index], int.Parse(e.GetAttribute("offset")), int.Parse(e.GetAttribute("length")));
                } else { Need(e.Name == "bytes", "unknown recipe operation."); part = Convert.FromBase64String(e.InnerText); }
                Need(output.Length <= size - part.Length, "recipe exceeds declared size."); output.Write(part, 0, part.Length);
            }
            byte[] result = output.ToArray(); Need(result.Length == size && Hash(result) == root.GetAttribute("sha256"), "recipe output verification failed.");
            if (compression >= 0) { Need(compression < headers.Count && Compressed(headers[compression]), "missing compression template."); result = StartingClasses.Pack(headers[compression], result); }
            else Need(compression == -1, "invalid compression mode.");
            return result;
        }
    }
    static byte[] Number(int value) { if (value >= -64 && value <= 63) return new byte[] { (byte)(value + 64), 0xa1 }; var b = new byte[6]; b[0] = 0x82; Array.Copy(BitConverter.GetBytes(value), 0, b, 1, 4); b[5] = 0xa1; return b; }
    static int Integer(byte[] b) { Need((b.Length == 2 && b[1] == 0xa1) || (b.Length == 6 && b[0] == 0x82 && b[5] == 0xa1), "unexpected menu expression."); return b.Length == 2 ? b[0] - 64 : I(b, 1); }
    public static byte[] PatchTravelMenu(byte[] bytes, int textId, int requestFlag)
    {
        var esd = new RespecAssets.Esd(bytes); int changes = 0;
        foreach (var states in esd.Groups.Values) {
            RespecAssets.State menu = null, cleanup = null;
            foreach (var state in states.Values) {
                bool list = false, clear = false, end = false;
                foreach (var c in state.Enter) {
                    if (c.Bank != 1) continue;
                    if (c.Id == 19) { Need(Integer(c.Args[0]) != MenuIndex, "travel menu index collision."); if (Integer(c.Args[1]) == RespecAssets.TextId) list = true; }
                    if (c.Id == 8) clear = true;
                    if (c.Id == 43) end = true;
                }
                if (list) { Need(menu == null, "multiple native bonfire menus."); menu = state; }
                // Native cleanup ends rest, closes the menu, and returns to the idle state.
                if (clear && end && state.Conditions.Count == 1) cleanup = state;
            }
            if (menu == null) continue;
            Need(cleanup != null, "native bonfire cleanup state missing.");
            long id = 0; foreach (long candidate in states.Keys) if (candidate >= id) id = candidate + 1;
            var travel = new RespecAssets.State { Id = id };
            foreach (var original in cleanup.Enter) {
                var command = new RespecAssets.Command(original.Id); command.Bank = original.Bank;
                command.Args.AddRange(original.Args); travel.Enter.Add(command);
            }
            travel.Enter.Add(new RespecAssets.Command(11, requestFlag, 1));
            travel.Conditions.AddRange(cleanup.Conditions);
            // Test 23: GetTalkListEntryResult(), then compare with our new menu index.
            menu.Enter.Add(new RespecAssets.Command(19, MenuIndex, textId, -1));
            menu.Conditions.Add(new RespecAssets.Condition(id, new byte[] { 0x57, 0x84, (byte)(64 + MenuIndex), 0x95, 0xa1 }));
            states.Add(id, travel); changes++;
        }
        Need(changes == 1, "expected exactly one bonfire menu.");
        byte[] result = esd.Write(); Need(Hash(new RespecAssets.Esd(result).Write()) == Hash(result), "travel menu round-trip failed.");
        return result;
    }
    static void PatchTalk(Dictionary<string, byte[]> files)
    {
        const string firelink = "script\\talk\\m10_02_00_00.talkesdbnd.dcx", izalith = "script\\talk\\m14_01_00_00.talkesdbnd.dcx";
        var b = new RespecAssets.Binder(files[firelink]); int count = 0;
        foreach (var entry in b.Entries) if (Path.GetFileName(entry.Name) == "t100200.esd") { entry.Bytes = PatchTravelMenu(entry.Bytes, HellText, ToHellFlag); count++; }
        Need(count == 1, "Firelink bonfire script missing."); files[firelink] = b.Write();
        b = new RespecAssets.Binder(files[izalith]); RespecAssets.Entry template = null; int max = 0;
        foreach (var entry in b.Entries) {
            Need(Path.GetFileName(entry.Name) != "t141020.esd", "Area talk identifier collision.");
            if (Path.GetFileName(entry.Name) == "t141000.esd") template = entry;
            if (entry.Id > max) max = entry.Id;
        }
        Need(template != null, "native Izalith bonfire script missing.");
        b.Entries.Add(new RespecAssets.Entry { Name = template.Name.Replace("t141000.esd", "t141020.esd"), Id = max + 1, Flags = template.Flags,
            Bytes = PatchTravelMenu(template.Bytes, FirelinkText, ToFirelinkFlag) });
        files[izalith] = b.Write();
        foreach (string path in new List<string>(files.Keys)) if (path.StartsWith("msg\\", StringComparison.OrdinalIgnoreCase)) {
            b = new RespecAssets.Binder(files[path]); count = 0;
            foreach (var entry in b.Entries) if (entry.Name.EndsWith("\\Event_text_.fmg", StringComparison.OrdinalIgnoreCase)) {
                entry.Bytes = RespecAssets.PatchFmg(RespecAssets.PatchFmg(entry.Bytes, "Travel to Cursed Arena", HellText), "Travel to Firelink Shrine", FirelinkText); count++;
            }
            Need(count > 0, "travel text table missing."); files[path] = b.Write();
        }
    }
    static void PatchPlaceNames(string game, Dictionary<string, byte[]> files)
    {
        foreach (string menu in new List<string>(files.Keys)) if (menu.EndsWith("menu.msgbnd.dcx", StringComparison.OrdinalIgnoreCase)) {
            string path = menu.Replace("menu.msgbnd.dcx", "item.msgbnd.dcx");
            var binder = new RespecAssets.Binder(ReadGame(game, path)); int count = 0;
            foreach (var entry in binder.Entries) if (entry.Name.EndsWith(@"\Place_name_.fmg", StringComparison.OrdinalIgnoreCase)) {
                // The collision ID selects the large title; ID + 10000 is its small subtitle.
                entry.Bytes = RespecAssets.PatchFmg(RespecAssets.PatchFmg(entry.Bytes, "Cursed Arena", 9990), " ", 19990); count++;
            }
            Need(count > 0, "place-name table missing."); files.Add(path, binder.Write());
        }
    }
    static void AppendCollision(string game, Dictionary<string, byte[]> files, char prefix, byte[] hkx)
    {
        string stem = "map\\m14_01_00_00\\" + prefix + "14_01_00_00", bhdPath = stem + ".hkxbhd", bdtPath = stem + ".hkxbdt";
        byte[] header = ReadGame(game, bhdPath), data = ReadGame(game, bdtPath);
        Need(header.Length >= 32 && Encoding.ASCII.GetString(header, 0, 4) == "BHF3" && header[12] == 0x74 && header[13] == 0 && header[14] == 0 && header[15] == 0, "unsupported collision archive.");
        int n = I(header, 16); Need(n > 0 && n < 10000 && 32 + n * 24 <= header.Length, "invalid collision header.");
        byte[] template = Slice(data, I(header, 40), I(header, 36));
        if (prefix == 'l') {
            hkx = (byte[])hkx.Clone(); byte[] search = Encoding.ASCII.GetBytes("h9900B1"); int changed = 0;
            for (int i = 0; i <= hkx.Length - search.Length; i++) {
                int j = 0; while (j < search.Length && hkx[i + j] == search[j]) j++;
                if (j == search.Length) { hkx[i] = (byte)'l'; changed++; }
            }
            Need(changed == 1, "collision body name missing.");
        }
        AppendArchive(game, files, bhdPath, bdtPath, "m14_01_00_00\\" + prefix + "9900B1A14.hkx.dcx", StartingClasses.Pack(template, hkx));
    }
    static void AppendArchive(string game, Dictionary<string, byte[]> files, string bhdPath, string bdtPath, string name, byte[] packed)
    {
        byte[] header = ReadGame(game, bhdPath), data = ReadGame(game, bdtPath);
        Need(header.Length >= 32 && Encoding.ASCII.GetString(header, 0, 4) == "BHF3" && header[12] == 0x74, "unsupported split archive.");
        int n = I(header, 16); Need(n > 0 && n < 10000 && 32 + n * 24 <= header.Length, "invalid split header.");
        byte[] nameBytes = Encoding.ASCII.GetBytes(name + "\0");
        int record = 32 + n * 24, dataOffset = checked((data.Length + 15) & ~15), maxId = -1;
        byte[] newHeader = new byte[header.Length + 24 + nameBytes.Length];
        Array.Copy(header, newHeader, record); Array.Copy(header, record, newHeader, record + 24, header.Length - record);
        for (int i = 0; i < n; i++) {
            int p = 32 + i * 24, path = I(header, p + 16); Need(path >= record && path < header.Length, "invalid collision name.");
            int end = path; while (end < header.Length && header[end] != 0) end++; Need(end < header.Length, "unterminated collision name.");
            Need(Encoding.ASCII.GetString(header, path, end - path) != name, "collision identifier already exists.");
            Put(newHeader, p + 16, checked(path + 24)); maxId = Math.Max(maxId, I(header, p + 12));
        }
        Put(newHeader, 16, n + 1); Put(newHeader, record, 0x40); Put(newHeader, record + 4, packed.Length);
        Put(newHeader, record + 8, dataOffset); Put(newHeader, record + 12, maxId + 1);
        Put(newHeader, record + 16, header.Length + 24); Put(newHeader, record + 20, packed.Length);
        Array.Copy(nameBytes, 0, newHeader, header.Length + 24, nameBytes.Length);
        byte[] newData = new byte[checked(dataOffset + packed.Length)]; Array.Copy(data, newData, data.Length); Array.Copy(packed, 0, newData, dataOffset, packed.Length);
        files.Add(bhdPath, newHeader); files.Add(bdtPath, newData);
    }

    sealed class BannerBlock { public string Name; public byte[] Bytes; public int Count; }
    static byte[] Join(byte[] a, byte[] b) { byte[] c = new byte[a.Length + b.Length]; Array.Copy(a, c, a.Length); Array.Copy(b, 0, c, a.Length, b.Length); return c; }
    static string UiString(byte[] b, int p) {
        int end = p; while (end + 1 < b.Length && (b[end] != 0 || b[end + 1] != 0)) end += 2;
        Need(end + 1 < b.Length, "unterminated layout string."); return Encoding.Unicode.GetString(b, p, end - p);
    }
    static void Short(byte[] b, int p, short value) { Array.Copy(BitConverter.GetBytes(value), 0, b, p, 2); }
    public static byte[] PatchBannerLayout(byte[] source)
    {
        bool compressed = Compressed(source); byte[] b = compressed ? StartingClasses.Unpack(source) : source;
        Need(Encoding.ASCII.GetString(b, 0, 4) == "DRB\0", "unsupported banner layout.");
        var blocks = new List<BannerBlock>(); var byName = new Dictionary<string, BannerBlock>();
        for (int p = 16; p < b.Length;) {
            string name = Encoding.ASCII.GetString(b, p, 4); int size = I(b, p + 4), count = I(b, p + 8);
            Need(size >= 0 && size % 16 == 0 && count >= 0 && I(b, p + 12) == 0, "unsupported layout block.");
            var block = new BannerBlock { Name = name, Count = count, Bytes = Slice(b, p + 16, size) };
            blocks.Add(block); byName.Add(name.TrimEnd('\0'), block); p = checked(p + 16 + size);
        }
        var strings = byName["STR"]; var shapes = byName["SHAP"]; var parameters = byName["SHPR"];
        var controls = byName["CTRL"]; var objects = byName["DLGO"]; var groups = byName["DLG"];
        int usedStrings = 0;
        for (int i = 0; i < strings.Count; i++) {
            string value = UiString(strings.Bytes, usedStrings);
            Need(value != "mapname_99_09_0", "arena headline already exists.");
            usedStrings += Encoding.Unicode.GetByteCount(value) + 2;
        }
        int root = -1, template = -1;
        for (int g = 0; g < groups.Count; g++) {
            int group = g * 64; string name = UiString(strings.Bytes, I(groups.Bytes, group));
            if (name == "F23_MapName") { Need(root < 0, "duplicate map banner."); root = group; }
            for (int j = 0; j < I(groups.Bytes, group + 32); j++) {
                int obj = I(groups.Bytes, group + 36) + j * 32;
                if (name == "F23_MapName" && UiString(strings.Bytes, I(objects.Bytes, obj)) == "subtext") template = obj;
            }
        }
        Need(root >= 0 && template >= 0, "native map banner text missing.");
        byte[] item = Slice(objects.Bytes, template, 32);
        byte[] record = Slice(shapes.Bytes, I(item, 4), 8); int start = I(record, 4), end = parameters.Bytes.Length;
        for (int j = 0; j < shapes.Count; j++) { int offset = I(shapes.Bytes, j * 8 + 4); if (offset > start && offset < end) end = offset; }
        byte[] original = Slice(parameters.Bytes, start, end - start);
        Need(UiString(strings.Bytes, I(record, 0)) == "Text", "banner template is not text.");
        int baseSize = original.Length == 46 ? 16 : 8;
        Need((original.Length == 38 || original.Length == 46) && original[baseSize + 15] == 2 && I(original, baseSize + 16) == 28, "unsupported native dynamic banner text.");
        byte[] textStrings = Slice(strings.Bytes, 0, usedStrings);
        int controlName = textStrings.Length; textStrings = Join(textStrings, Encoding.Unicode.GetBytes("mapname_99_09_0\0"));
        int literal = textStrings.Length; textStrings = Join(textStrings, Encoding.Unicode.GetBytes("Cursed Arena\0"));
        // The native selector decomposes ID 9990 as area 99, block 09, suffix 0,
        // and toggles all mapname_* controls generically, regardless of shape type.
        // Reuse the font atlas and animation; no new texture or runtime hook.
        byte[] data = (byte[])original.Clone();
        Short(data, 0, 256); Short(data, 2, 0); Short(data, 4, 1024); Short(data, 6, 56);
        Short(data, baseSize + 2, 0); Short(data, baseSize + 12, 10);
        data[baseSize + 14] = 10; data[baseSize + 15] = 0; // centered native large font; literal text
        Put(data, baseSize + 20, literal);
        Put(record, 4, parameters.Bytes.Length); parameters.Bytes = Join(parameters.Bytes, data);
        Put(item, 0, controlName); Put(item, 4, shapes.Count * 8);
        shapes.Bytes = Join(Slice(shapes.Bytes, 0, shapes.Count * 8), record); shapes.Count++;
        byte[] control = Slice(controls.Bytes, I(item, 8), 8); Put(item, 8, controls.Count * 8);
        controls.Bytes = Join(Slice(controls.Bytes, 0, controls.Count * 8), control); controls.Count++;
        int insertion = I(groups.Bytes, root + 36) + I(groups.Bytes, root + 32) * 32;
        objects.Bytes = Join(Join(Slice(objects.Bytes, 0, insertion), item), Slice(objects.Bytes, insertion, objects.Bytes.Length - insertion)); objects.Count++;
        for (int g = 0; g < groups.Count; g++) {
            int group = g * 64;
            if (group == root) Put(groups.Bytes, group + 32, I(groups.Bytes, group + 32) + 1);
            else if (I(groups.Bytes, group + 36) >= insertion) Put(groups.Bytes, group + 36, I(groups.Bytes, group + 36) + 32);
        }
        strings.Bytes = textStrings; strings.Count += 2;
        using (var output = new MemoryStream()) using (var writer = new BinaryWriter(output)) {
            writer.Write(Slice(b, 0, 16));
            foreach (var block in blocks) {
                writer.Write(Encoding.ASCII.GetBytes(block.Name)); writer.Write((block.Bytes.Length + 15) & ~15); writer.Write(block.Count); writer.Write(0);
                writer.Write(block.Bytes); while (output.Length % 16 != 0) writer.Write((byte)0);
            }
            byte[] result = output.ToArray(); return compressed ? StartingClasses.Pack(source, result) : result;
        }
    }

    public static void Add(string game, string recipes, Dictionary<string, byte[]> files)
    {
        string[,] mappings = {
            { "terrain", "map\\m14_01_00_00\\m9900B1A14.flver.dcx" },
            { "sky", "map\\m14_01_00_00\\m9901B1A14.flver.dcx" },
            { "sky-textures", "map\\m14\\m14_9999.tpf.dcx" },
            { "lighting", "param\\DrawParam\\a14_DrawParam.parambnd.dcx" },
            { "navmesh", "map\\m14_01_00_00\\m14_01_00_00.nvmbnd.dcx" },
            { "navmesh-rooms", "map\\m14_01_00_00\\m14_01_00_00.mcp" },
            { "navmesh-gates", "map\\m14_01_00_00\\m14_01_00_00.mcg" },
            { "izalith-map", "map\\MapStudio\\m14_01_00_00.msb" },
            { "firelink-map", "map\\MapStudio\\m10_02_00_00.msb" },
            { "izalith-event", "event\\m14_01_00_00.emevd.dcx" },
            { "firelink-event", "event\\m10_02_00_00.emevd.dcx" }
        };
        for (int i = 0; i < mappings.GetLength(0); i++) files.Add(mappings[i, 1], Reconstruct(game, Path.Combine(recipes, mappings[i, 0] + ".xml")));
        byte[] collision = Reconstruct(game, Path.Combine(recipes, "collision.xml"));
        AppendCollision(game, files, 'h', collision); AppendCollision(game, files, 'l', collision);
        AppendArchive(game, files, @"map\m14\GI_EnvM_m14.tpfbhd", @"map\m14\GI_EnvM_m14.tpfbdt",
            @"\GI_Env_m14_01_0008.tpf.dcx", Reconstruct(game, Path.Combine(recipes, "outdoor-probe.xml")));
        PatchTalk(files); PatchPlaceNames(game, files);
        foreach (string path in new List<string>(files.Keys)) if (path.StartsWith("menu\\", StringComparison.OrdinalIgnoreCase)) files[path] = PatchBannerLayout(files[path]);
    }
}
