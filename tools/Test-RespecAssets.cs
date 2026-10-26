using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class RespecAssetChecks
{
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    static bool Equal(byte[] a, byte[] b) { if (a.Length != b.Length) return false; for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false; return true; }
    static int N(byte[] b) { return b.Length == 2 ? b[0] - 64 : BitConverter.ToInt32(b, 1); }
    static string Command(RespecAssets.Command c) { var b = new StringBuilder(); b.Append(c.Bank).Append(':').Append(c.Id); foreach (var arg in c.Args) b.Append('|').Append(Convert.ToBase64String(arg)); return b.ToString(); }
    static string Condition(RespecAssets.Condition c) { var b = new StringBuilder(); b.Append(c.Target).Append(':').Append(Convert.ToBase64String(c.Eval)); foreach (var cmd in c.Pass) b.Append('|').Append(Command(cmd)); foreach (var sub in c.Sub) b.Append('{').Append(Condition(sub)).Append('}'); return b.ToString(); }
    // Independent FMG inspection: don't use the production text writer to verify text.
    static SortedDictionary<int, string> Texts(byte[] b)
    {
        var result = new SortedDictionary<int, string>(); int groups = BitConverter.ToInt32(b, 12), table = BitConverter.ToInt32(b, 20);
        for (int i = 0; i < groups; i++) {
            int p = 28 + i * 12, index = BitConverter.ToInt32(b, p), first = BitConverter.ToInt32(b, p + 4), last = BitConverter.ToInt32(b, p + 8);
            for (int id = first; id <= last; id++) {
                int offset = BitConverter.ToInt32(b, table + index++ * 4); string text = null;
                if (offset != 0) { int end = offset; while (b[end] != 0 || b[end + 1] != 0) end += 2; text = Encoding.Unicode.GetString(b, offset, end - offset); }
                result.Add(id, text);
            }
        }
        Check(BitConverter.ToInt32(b, 4) == b.Length, "FMG file size"); return result;
    }
    sealed class LayoutBlock { public byte[] Bytes; public int Count; }
    static Dictionary<string, LayoutBlock> Layout(byte[] input)
    {
        byte[] b = Encoding.ASCII.GetString(input, 0, 4) == "DRB\0" ? input : StartingClasses.Unpack(input);
        var blocks = new Dictionary<string, LayoutBlock>(); int position = 16;
        while (position < b.Length) {
            string name = Encoding.ASCII.GetString(b, position, 4).TrimEnd('\0');
            int size = BitConverter.ToInt32(b, position + 4), count = BitConverter.ToInt32(b, position + 8);
            Check(size >= 0 && size % 16 == 0 && position + 16 <= b.Length - size, "Valid layout section bounds/alignment.");
            var bytes = new byte[size]; Array.Copy(b, position + 16, bytes, 0, size);
            blocks.Add(name, new LayoutBlock { Bytes = bytes, Count = count }); position += 16 + size;
        }
        Check(position == b.Length, "No truncated layout section."); return blocks;
    }
    static int At(byte[] b, int offset) { return BitConverter.ToInt32(b, offset); }
    static string LayoutName(byte[] b, int offset) {
        int end = offset; while (b[end] != 0 || b[end + 1] != 0) end += 2;
        return Encoding.Unicode.GetString(b, offset, end - offset);
    }
    static byte[] Part(byte[] b, int offset, int size) { var result = new byte[size]; Array.Copy(b, offset, result, 0, size); return result; }
    static void CheckLayout(byte[] source, byte[] output)
    {
        var before = Layout(source); var after = Layout(output);
        foreach (var block in before) {
            if (block.Key == "STR" || block.Key == "SHAP" || block.Key == "SHPR" || block.Key == "CTRL" || block.Key == "DLGO" || block.Key == "DLG") continue;
            Check(block.Value.Count == after[block.Key].Count && Equal(block.Value.Bytes, after[block.Key].Bytes), "Textures, control parameters, animations and schedules untouched.");
        }
        bool remastered = Encoding.ASCII.GetString(source, 0, 4) == "DCX\0";
        int additions = remastered ? 4 : 2;
        Check(after["STR"].Count == before["STR"].Count + additions && after["SHAP"].Count == before["SHAP"].Count + additions && after["CTRL"].Count == before["CTRL"].Count + additions && after["DLGO"].Count == before["DLGO"].Count + additions && after["DLG"].Count == before["DLG"].Count, "Only Free Points and the fifth Start category added.");
        foreach (string name in new[] { "SHAP", "CTRL" }) Check(Equal(Part(before[name].Bytes, 0, before[name].Count * 8), Part(after[name].Bytes, 0, before[name].Count * 8)), "Existing shape/control references preserved.");
        byte[] expectedParameters = (byte[])before["SHPR"].Bytes.Clone();
        if (remastered) {
            for (int g = 0; g < before["DLG"].Count; ++g) {
                int at = g * 64;
                if (LayoutName(before["STR"].Bytes, At(before["DLG"].Bytes, at)) != "R00-00") continue;
                int start = At(before["DLG"].Bytes, at + 36), count = At(before["DLG"].Bytes, at + 32);
                for (int j = 0; j < count; ++j) {
                    int obj = start + j * 32;
                    if (LayoutName(before["STR"].Bytes, At(before["DLGO"].Bytes, obj)) != "category_right") continue;
                    int data = At(before["SHAP"].Bytes, At(before["DLGO"].Bytes, obj + 4) + 4);
                    foreach (int offset in new[] {0, 4}) Array.Copy(BitConverter.GetBytes((short)(BitConverter.ToInt16(expectedParameters, data + offset) + 50)), 0, expectedParameters, data + offset, 2);
                }
            }
        }
        Check(Equal(expectedParameters, Part(after["SHPR"].Bytes, 0, expectedParameters.Length)), "Only the right shoulder guide shifts; original icon geometry/styles preserved.");
        int newRows = 0, qualityWidgets = 0;
        for (int g = 0; g < before["DLG"].Count; ++g) {
            byte[] oldGroup = before["DLG"].Bytes, newGroup = after["DLG"].Bytes; int group = g * 64;
            string name = LayoutName(before["STR"].Bytes, At(oldGroup, group)); bool level = name == "C01-03", quality = remastered && name == "R00-00";
            int count = At(oldGroup, group + 32), oldOffset = At(oldGroup, group + 36), newOffset = At(newGroup, group + 36);
            Check(At(newGroup, group + 32) == count + (level || quality ? 2 : 0) && Equal(Part(oldGroup, group, 32), Part(newGroup, group, 32)) && Equal(Part(oldGroup, group + 40, 24), Part(newGroup, group + 40, 24)), "All original group bounds and controls unchanged.");
            for (int i = 0; i < count; ++i) Check(Equal(Part(before["DLGO"].Bytes, oldOffset + i * 32, 32), Part(after["DLGO"].Bytes, newOffset + i * 32, 32)), "Every original widget/navigation record unchanged.");
            if (quality) {
                int qualityX = -1;
                for (int j = 0; j < 2; ++j) {
                    int obj = newOffset + (count + j) * 32;
                    Check(LayoutName(after["STR"].Bytes, At(after["DLGO"].Bytes, obj)) == (j == 0 ? "cursor0_4" : "icon_q_souls"), "New Q category cursor/glyph names.");
                    int shape = At(after["DLGO"].Bytes, obj + 4), data = At(after["SHAP"].Bytes, shape + 4);
                    int x = BitConverter.ToInt16(after["SHPR"].Bytes, data), y = BitConverter.ToInt16(after["SHPR"].Bytes, data + 2);
                    Check(x >= 1100 && x < 1150 && y >= 64 && y < 80, "Q fits after the fourth icon in the horizontal bar.");
                    if (j == 0) qualityX = x;
                    else Check(x == qualityX + 6 && At(after["SHPR"].Bytes, data + 40) == RespecAssets.QIconTextId, "Q text is inside its isolated native hit target.");
                    qualityWidgets++;
                }
            }
            if (!level) continue;
            for (int i = 0; i < 2; i++) {
                int obj = newOffset + (count + i) * 32; byte[] objects = after["DLGO"].Bytes;
                string widget = LayoutName(after["STR"].Bytes, At(objects, obj));
                Check(widget == (i == 0 ? "qs_free_points_label" : "qs_free_points_value"), "New widgets have isolated names.");
                int shape = At(objects, obj + 4), data = At(after["SHAP"].Bytes, shape + 4);
                byte[] parameters = after["SHPR"].Bytes;
                Check(BitConverter.ToInt16(parameters, data) == (i == 0 ? 75 : 218) && BitConverter.ToInt16(parameters, data + 2) == 16 && BitConverter.ToInt16(parameters, data + 4) == (i == 0 ? 215 : 288) && BitConverter.ToInt16(parameters, data + 6) == 44, "Read-only row fits above Name; no original stat or Humanity row moves.");
                int baseSize = Encoding.ASCII.GetString(source, 0, 4) == "DRB\0" ? 8 : 16;
                Check(At(parameters, data + baseSize + 4) == (i == 0 ? 7 : 3) && BitConverter.ToInt16(parameters, data + baseSize + 12) == 6 && parameters[data + baseSize + 14] == (i == 0 ? 8 : 9), "New row preserves native label/value font, colors and alignment.");
                Check(parameters[data + baseSize + 15] == 1 && At(parameters, data + baseSize + 24) == (i == 0 ? RespecAssets.FreePointsTextId : 201111), "Native FMG label and independent numeric variable used."); newRows++;
            }
        }
        Check(newRows == 2 && qualityWidgets == (remastered ? 2 : 0), "Only intended Level Up and Start widgets added.");
        if (remastered) {
            bool duplicate = false; try { RespecAssets.PatchQualityLayout(output); } catch (InvalidDataException) { duplicate = true; }
            Check(duplicate, "Duplicate quality layout refused.");
        }
        bool rejected = false; try { RespecAssets.PatchLevelUpLayout(output); } catch (InvalidDataException) { rejected = true; }
        Check(rejected, "Already-patched layout rejected safely.");
    }
    public static void Run(string game)
    {
        int count, repeatedCount;
        var files = RespecAssets.Generate(game, out count); var repeated = RespecAssets.Generate(game, out repeatedCount);
        Check(count == 75 && repeatedCount == count && files.Count == 33, "All bonfire scripts and language archives covered.");
        int bonfires = 0, tutorial = 0, dlc = 0, languages = 0;
        foreach (var pair in files) {
            Check(Equal(pair.Value, repeated[pair.Key]), "Repeat generation deterministic.");
            if (pair.Key.StartsWith("menu\\", StringComparison.Ordinal)) {
                CheckLayout(File.ReadAllBytes(Path.Combine(game, pair.Key)), pair.Value); continue;
            }
            var before = new RespecAssets.Binder(File.ReadAllBytes(Path.Combine(game, pair.Key)));
            var after = new RespecAssets.Binder(pair.Value);
            Check(before.Entries.Count == after.Entries.Count, "Binder entry count preserved.");
            for (int i = 0; i < before.Entries.Count; i++) {
                var a = before.Entries[i]; var b = after.Entries[i];
                Check(a.Name == b.Name && a.Id == b.Id && a.Flags == b.Flags, "Binder metadata preserved.");
                if (Equal(a.Bytes, b.Bytes)) continue;
                if (pair.Key.StartsWith("msg\\", StringComparison.Ordinal)) {
                    bool general = a.Name.EndsWith("\\Menu_general_text_.fmg"), quality = a.Name.EndsWith("\\System_message_win32_.fmg");
                    Check(general || quality || a.Name.EndsWith("\\Event_text_.fmg"), "Only bonfire, Free Points and Q glyph FMGs changed.");
                    var oldTexts = Texts(a.Bytes); var newTexts = Texts(b.Bytes);
                    if (general) Check(oldTexts.ContainsKey(201111) && oldTexts[201111] == "<?lvUpPcLuck?>", "All languages use the verified native numeric variable for the counter.");
                    int textId = quality ? RespecAssets.QIconTextId : general ? RespecAssets.FreePointsTextId : RespecAssets.TextId;
                    string language = pair.Key.Split('\\')[1];
                    string expected = quality ? "Q" : general ? RespecAssets.FreePointsTitle(language) : RespecAssets.Title(language);
                    Check(newTexts.Count == oldTexts.Count + 1 && newTexts[textId] == expected && !String.IsNullOrWhiteSpace(expected), "One correctly localized label added.");
                    foreach (var text in oldTexts) Check(newTexts[text.Key] == text.Value, "All native screen translations preserved.");
                    continue;
                }
                var original = new RespecAssets.Esd(a.Bytes); var patched = new RespecAssets.Esd(b.Bytes); int rows = 0;
                Check(original.Groups.Count == patched.Groups.Count, "No custom state groups added.");
                foreach (var group in original.Groups) {
                    var updated = patched.Groups[group.Key]; Check(group.Value.Count == updated.Count, "No custom bridge/editor states added.");
                    foreach (var state in group.Value.Values) {
                        var next = updated[state.Id];
                        Check(state.Enter.Count == next.Enter.Count && state.Exit.Count == next.Exit.Count && state.While.Count == next.While.Count && state.Conditions.Count == next.Conditions.Count, "All native command and transition counts preserved.");
                        for (int j = 0; j < state.Conditions.Count; j++) Check(Condition(state.Conditions[j]) == Condition(next.Conditions[j]), "Native screen opening, interruption, confirmation and saving transitions untouched.");
                        for (int j = 0; j < state.Exit.Count; j++) Check(Command(state.Exit[j]) == Command(next.Exit[j]), "Native exit commands untouched.");
                        for (int j = 0; j < state.While.Count; j++) Check(Command(state.While[j]) == Command(next.While[j]), "Native while commands untouched.");
                        for (int j = 0; j < state.Enter.Count; j++) {
                            var command = state.Enter[j]; var newer = next.Enter[j];
                            if (command.Bank == 1 && command.Id == 19 && N(command.Args[1]) == 15000100) {
                                Check(newer.Bank == 1 && newer.Id == 19 && newer.Args.Count == 3 && Equal(command.Args[0], newer.Args[0]) && N(newer.Args[1]) == RespecAssets.TextId && N(newer.Args[2]) == -1, "Same native menu slot, localized label and unconditional availability."); rows++;
                            } else Check(Command(command) == Command(newer), "All other native commands untouched.");
                        }
                    }
                }
                Check(rows == 1, "Exactly one native Level Up row replaced per bonfire.");
                bonfires += rows; if (pair.Key.Contains("m18_01")) tutorial += rows; if (pair.Key.Contains("m12_01")) dlc += rows;
            }
            if (pair.Key.StartsWith("msg\\", StringComparison.Ordinal)) languages++;
        }
        Check(bonfires == count && tutorial == 2 && dlc == 5 && languages == 13, "Both tutorial bonfires, DLC and all languages covered.");
        foreach (var pair in files) if (pair.Key.StartsWith("script\\", StringComparison.Ordinal)) {
            bool rejected = false; try { RespecAssets.PatchTalk(new RespecAssets.Binder(pair.Value)); } catch (InvalidDataException) { rejected = true; }
            Check(rejected, "Already-patched scripts reject duplicate edits."); break;
        }
        bool invalid = false; try { new RespecAssets.Esd(new byte[200]); } catch (InvalidDataException) { invalid = true; }
        Check(invalid, "Malformed ESD rejected.");
        Console.WriteLine("PASS: native Level Up transitions and save flow unchanged; 75 bonfires including both tutorial bonfires and 5 DLC; 13 languages; native Free Points row and original layout preservation; deterministic generation and safe collisions.");
    }
}
