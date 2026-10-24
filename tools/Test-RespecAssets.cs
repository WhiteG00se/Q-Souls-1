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
        int count, secondCount; var files = RespecAssets.Generate(game, out count); var second = RespecAssets.Generate(game, out secondCount);
        Check(count == 75 && secondCount == count && files.Count == 31, "All installed bonfire scripts and language archives must be covered.");
        int bonfires = 0, dlc = 0, languages = 0;
        foreach (var pair in files) {
            Check(Equal(pair.Value, second[pair.Key]), "Repeat generation must be deterministic.");
            var before = new RespecAssets.Binder(File.ReadAllBytes(Path.Combine(game, pair.Key))); var after = new RespecAssets.Binder(pair.Value);
            Check(before.Entries.Count == after.Entries.Count, "Binder entry count preserved.");
            for (int i = 0; i < before.Entries.Count; i++) {
                var a = before.Entries[i]; var b = after.Entries[i]; Check(a.Name == b.Name && a.Id == b.Id && a.Flags == b.Flags, "Binder metadata preserved.");
                if (Equal(a.Bytes, b.Bytes)) continue;
                if (pair.Key.StartsWith("msg\\", StringComparison.Ordinal)) {
                    Check(a.Name.EndsWith("\\Event_text_.fmg"), "Only event text FMGs changed."); var oldText = Texts(a.Bytes); var newText = Texts(b.Bytes);
                    Check(newText.Count == oldText.Count + 1 && newText[RespecAssets.TextId] == "Respec", "Exactly one Respec text entry added.");
                    foreach (var text in oldText) Check(newText[text.Key] == text.Value, "Existing translations preserved."); continue;
                }
                var oldEsd = new RespecAssets.Esd(a.Bytes); var newEsd = new RespecAssets.Esd(b.Bytes);
                foreach (var group in oldEsd.Groups) {
                    var newer = newEsd.Groups[group.Key]; Check(newer.Count == group.Value.Count + 5, "Exactly five Respec states added.");
                    foreach (var state in group.Value.Values) {
                        var updated = newer[state.Id]; bool menu = false;
                        foreach (var c in state.Enter) if (c.Bank == 1 && c.Id == 19 && N(c.Args[1]) == 15000100) menu = true;
                        Check(updated.Exit.Count == state.Exit.Count && updated.While.Count == state.While.Count, "Original command counts preserved.");
                        for (int j = 0; j < state.Exit.Count; j++) Check(Command(state.Exit[j]) == Command(updated.Exit[j]), "Original exit command preserved.");
                        for (int j = 0; j < state.While.Count; j++) Check(Command(state.While[j]) == Command(updated.While[j]), "Original while command preserved.");
                        Check(updated.Conditions.Count == state.Conditions.Count + (menu ? 1 : 0), "Only menu selection transition added.");
                        for (int j = 0; j < state.Conditions.Count; j++) Check(Condition(state.Conditions[j]) == Condition(updated.Conditions[j]), "Original conditions preserved.");
                        if (!menu) {
                            Check(state.Enter.Count == updated.Enter.Count, "Unrelated state commands preserved.");
                            for (int j = 0; j < state.Enter.Count; j++) Check(Command(state.Enter[j]) == Command(updated.Enter[j]), "Unrelated entry commands preserved.");
                        } else {
                            Check(updated.Enter.Count == state.Enter.Count + 4, "One menu row and three bridge resets added.");
                            int levelUp = -1, respec = -1;
                            for (int j = 0; j < updated.Enter.Count; j++) if (updated.Enter[j].Id == 19) { int text = N(updated.Enter[j].Args[1]); if (text == 15000100) levelUp = j; if (text == RespecAssets.TextId) respec = j; }
                            Check(respec == levelUp + 1 && N(updated.Enter[respec].Args[0]) == RespecAssets.MenuIndex && N(updated.Enter[respec].Args[2]) == RespecAssets.Ready, "Respec appears directly after Level Up and requires runtime readiness.");
                        }
                    }
                    var wait = newer[RespecAssets.FirstState + 1]; int interruptions = 0;
                    foreach (var c in group.Value[20].Conditions) if (c.Target != 4) { Check(Condition(c) == Condition(wait.Conditions[interruptions]), "Bonfire interruption condition copied."); interruptions++; }
                    Check(interruptions > 0 && wait.Exit[0].Id == 11 && N(wait.Exit[0].Args[0]) == RespecAssets.Request && N(wait.Exit[0].Args[1]) == 0, "Interruption clears request.");
                    var finish = newer[RespecAssets.FirstState + 2]; Check(finish.Conditions[0].Target == RespecAssets.FirstState + 4, "Only committed draft enters save state.");
                    Check(newer[RespecAssets.FirstState + 4].Enter[0].Id == 50, "Save uses normal RequestSave.");
                }
                bonfires++; if (pair.Key.Contains("m12_01")) dlc++;
            }
            if (pair.Key.StartsWith("msg\\", StringComparison.Ordinal)) languages++;
        }
        Check(bonfires == count && dlc == 5 && languages == 13, "Bonfire/DLC/language coverage.");
        // Patching already patched data must fail safely rather than duplicate entries.
        foreach (var pair in files) if (pair.Key.StartsWith("script\\", StringComparison.Ordinal)) {
            bool rejected = false; try { RespecAssets.PatchTalk(new RespecAssets.Binder(pair.Value)); } catch (InvalidDataException) { rejected = true; }
            Check(rejected, "Existing state/menu identifiers must reject collisions."); break;
        }
        bool invalid = false; try { new RespecAssets.Esd(new byte[200]); } catch (InvalidDataException) { invalid = true; } Check(invalid, "Malformed ESD rejected.");
        Console.WriteLine("PASS: 75 bonfire scripts (including 5 DLC), 13 languages, existing content preserved, state transitions, normal save request, round trips, repeat generation and identifier collisions.");
    }
}
