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
    public static void Run(string game)
    {
        int count, repeatedCount;
        var files = RespecAssets.Generate(game, out count); var repeated = RespecAssets.Generate(game, out repeatedCount);
        Check(count == 75 && repeatedCount == count && files.Count == 31, "All bonfire scripts and language archives covered.");
        int bonfires = 0, tutorial = 0, dlc = 0, languages = 0;
        foreach (var pair in files) {
            Check(Equal(pair.Value, repeated[pair.Key]), "Repeat generation deterministic.");
            var before = new RespecAssets.Binder(File.ReadAllBytes(Path.Combine(game, pair.Key)));
            var after = new RespecAssets.Binder(pair.Value);
            Check(before.Entries.Count == after.Entries.Count, "Binder entry count preserved.");
            for (int i = 0; i < before.Entries.Count; i++) {
                var a = before.Entries[i]; var b = after.Entries[i];
                Check(a.Name == b.Name && a.Id == b.Id && a.Flags == b.Flags, "Binder metadata preserved.");
                if (Equal(a.Bytes, b.Bytes)) continue;
                if (pair.Key.StartsWith("msg\\", StringComparison.Ordinal)) {
                    Check(a.Name.EndsWith("\\Event_text_.fmg"), "Only native bonfire label FMG changed.");
                    var oldTexts = Texts(a.Bytes); var newTexts = Texts(b.Bytes);
                    Check(newTexts.Count == oldTexts.Count + 1 && newTexts[RespecAssets.TextId] == RespecAssets.Title(pair.Key.Split('\\')[1]), "One localized combined bonfire label added.");
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
        Console.WriteLine("PASS: native Level Up transitions and save flow unchanged; 75 bonfires including both tutorial bonfires and 5 DLC; 13 localized labels; deterministic generation and safe collisions.");
    }
}
