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
        var before=Layout(source); var after=Layout(output);
        foreach (var pair in before) {
            string key=pair.Key;
            if (key=="STR" || key=="SHAP" || key=="SHPR" || key=="CTRL" || key=="DLGO" || key=="DLG") continue;
            Check(Equal(pair.Value.Bytes,after[key].Bytes) && pair.Value.Count==after[key].Count,"Native textures, navigation and animation unchanged.");
        }
        foreach (string key in new[]{"SHAP","CTRL"})
            Check(Equal(Part(before[key].Bytes,0,before[key].Count*8),Part(after[key].Bytes,0,before[key].Count*8)),"Existing shape/control references preserved.");
        Check(after["DLGO"].Count==before["DLGO"].Count+2,"Only the existing Free Points row adds widgets.");
        for (int g=0;g<before["DLG"].Count;++g) {
            int at=g*64; string name=LayoutName(before["STR"].Bytes,At(before["DLG"].Bytes,at));
            if (name!="R00" && name!="R00-00") continue;
            Check(Equal(Part(before["DLG"].Bytes,at,36),Part(after["DLG"].Bytes,at,36)) && Equal(Part(before["DLG"].Bytes,at+40,24),Part(after["DLG"].Bytes,at+40,24)),"Start category group unchanged apart from relocated object table.");
            int count=At(before["DLG"].Bytes,at+32), start=At(before["DLG"].Bytes,at+36);
            Check(Equal(Part(before["DLGO"].Bytes,start,count*32),Part(after["DLGO"].Bytes,At(after["DLG"].Bytes,at+36),count*32)),"Every native Start category unchanged.");
        }
        int centered=0;
        for(int g=0;g<before["DLG"].Count;++g) {
            int at=g*64;
            if(LayoutName(before["STR"].Bytes,At(before["DLG"].Bytes,at))!="F04-01") continue;
            int shape=At(before["DLG"].Bytes,at+4), data=At(before["SHAP"].Bytes,shape+4);
            Check(BitConverter.ToInt16(before["SHPR"].Bytes,data)==0 && BitConverter.ToInt16(after["SHPR"].Bytes,data)==358 &&
                BitConverter.ToInt16(after["SHPR"].Bytes,data+4)==1638,"Native list parent translated so its background center is x=640.");
            int start=At(before["DLG"].Bytes,at+36), count=At(before["DLG"].Bytes,at+32), next=At(after["DLG"].Bytes,at+36);
            Check(Equal(Part(before["DLGO"].Bytes,start,count*32),Part(after["DLGO"].Bytes,next,count*32)),"Native rows, cursor and hitboxes retain their common parent.");
            centered++;
        }
        Check(centered==1,"Exactly one native list parent centered.");
    }
    static void CheckQualityMenu(SortedDictionary<long,RespecAssets.State> states, RespecAssets.State menu)
    {
        var entry=menu.Enter[menu.Enter.Count-1];
        Check(entry.Id==19 && N(entry.Args[0])==14 && N(entry.Args[1])==RespecAssets.QSoulsTextId && N(entry.Args[2])==RespecAssets.QualityReady,"Q Souls available only with a healthy custom panel.");
        var branch=menu.Conditions[menu.Conditions.Count-1];
        Check(Equal(branch.Eval,new byte[]{0x57,0x84,0x4e,0x95,0xa1}),"Native result 14 opens Q Souls.");
        var open=states[branch.Target]; var wait=states[open.Conditions[0].Target];
        Check(open.Enter.Count==3 && open.Enter[0].Id==67,"Native bonfire list is force-closed before the custom panel.");
        Check(open.Enter[1].Id==11 && N(open.Enter[1].Args[0])==RespecAssets.QualityDone && N(open.Enter[1].Args[1])==0,"Stale completion cleared.");
        Check(open.Enter[2].Id==11 && N(open.Enter[2].Args[0])==RespecAssets.QualityRequest && N(open.Enter[2].Args[1])==1,"Panel opened through the reference request bridge.");
        Check(wait.Enter.Count==0 && wait.Exit.Count==1 && wait.Exit[0].Id==11 && N(wait.Exit[0].Args[0])==RespecAssets.QualityRequest && N(wait.Exit[0].Args[1])==0,"Every wait exit releases panel ownership.");
        var back=states[wait.Conditions[wait.Conditions.Count-1].Target];
        Check(back.Enter.Count==2 && back.Conditions[0].Target==menu.Id,"Back clears bridge state before rebuilding bonfire.");
        int i=0; foreach(var c in states[20].Conditions) if(c.Target!=menu.Id) Check(Condition(wait.Conditions[i++])==Condition(c),"Native death/range/network interruptions preserved.");
        Check(wait.Conditions.Count==i+2,"Only done and renderer-loss exits added.");
        foreach(var state in new[]{open,wait,back}) foreach(var c in state.Enter) Check(c.Id!=10 && c.Id!=19 && c.Id!=20,"No native submenu or hidden list rows exist.");
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
                    bool general = a.Name.EndsWith("\\Menu_general_text_.fmg");
                    Check(general || a.Name.EndsWith("\\Event_text_.fmg"),"Only bonfire and Free Points labels changed.");
                    var oldTexts=Texts(a.Bytes); var newTexts=Texts(b.Bytes);
                    string language=pair.Key.Split('\\')[1];
                    Check(newTexts.Count==oldTexts.Count+(general?1:2),"Expected added label count.");
                    Check(newTexts[general?RespecAssets.FreePointsTextId:RespecAssets.TextId]==(general?RespecAssets.FreePointsTitle(language):RespecAssets.Title(language)),"Existing localized feature label preserved.");
                    if (!general) {
                        Check(newTexts[RespecAssets.QSoulsTextId]=="Q Souls","Q Souls is identical in every language.");
                    }
                    foreach (var text in oldTexts) Check(newTexts[text.Key] == text.Value, "All native screen translations preserved.");
                    continue;
                }
                var original = new RespecAssets.Esd(a.Bytes); var patched = new RespecAssets.Esd(b.Bytes); int rows = 0;
                Check(original.Groups.Count == patched.Groups.Count, "No custom state groups added.");
                foreach (var group in original.Groups) {
                    var updated = patched.Groups[group.Key]; int added=0; foreach(var state in group.Value.Values) foreach(var c in state.Enter) if(c.Bank==1 && c.Id==19 && N(c.Args[1])==15000100) added++;
                    Check(updated.Count==group.Value.Count+added*3,"Only Q Souls build/return states added.");
                    foreach (var state in group.Value.Values) {
                        var next = updated[state.Id];
                        bool menu=false; foreach(var c in state.Enter) if(c.Bank==1 && c.Id==19 && N(c.Args[1])==15000100) menu=true;
                        Check(state.Enter.Count+(menu?3:0)==next.Enter.Count && state.Exit.Count==next.Exit.Count && state.While.Count==next.While.Count && state.Conditions.Count+(menu?1:0)==next.Conditions.Count,"Native counts preserved apart from the bonfire entry.");
                        if(menu) CheckQualityMenu(updated,next);
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
        Console.WriteLine("PASS: native Level Up transitions preserved; Q Souls/FPS bonfire pages; 75 bonfires including both tutorial bonfires and 5 DLC; 13 languages; native Free Points row and original layout preservation; deterministic generation and safe collisions.");
    }
}
