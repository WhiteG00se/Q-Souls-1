using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

// DSR BND3, FMG v1 and 64-bit ESD layouts: JKAnderson/SoulsFormats.
// This narrow implementation adds only our menu entry and bridge states.
public static class RespecAssets
{
    public const int Request = 70000980, Done = 70000981, Committed = 70000982, Ready = 70000983;
    public const int TextId = 15000990, MenuIndex = 60, FirstState = 900000;
    const int LevelUpText = 15000100, Data = 108;
    static void Need(bool ok, string error) { if (!ok) throw new InvalidDataException(error); }
    static void Bounds(byte[] b, long p, long n) { Need(p >= 0 && n >= 0 && p <= b.Length - n, "Invalid Respec asset bounds."); }
    static int I(byte[] b, long p) { Bounds(b, p, 4); return BitConverter.ToInt32(b, (int)p); }
    static long Q(byte[] b, long p) { Bounds(b, p, 8); return BitConverter.ToInt64(b, (int)p); }
    static int Count(long n) { Need(n >= 0 && n <= 100000, "Invalid Respec asset count."); return (int)n; }
    static byte[] Slice(byte[] b, long p, long n) { Bounds(b, p, n); byte[] a = new byte[checked((int)n)]; Array.Copy(b, p, a, 0, n); return a; }
    static string Z(byte[] b, int p) { Bounds(b, p, 1); int end = p; while (end < b.Length && b[end] != 0) end++; Need(end < b.Length, "Unterminated archive path."); return Encoding.ASCII.GetString(b, p, end - p); }
    static bool Same(byte[] a, byte[] b) { if (a.Length != b.Length) return false; for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false; return true; }
    static byte[] Number(int value) { if (value >= -64 && value <= 63) return new byte[] { (byte)(value + 64), 0xa1 }; byte[] b = new byte[6]; b[0] = 0x82; Array.Copy(BitConverter.GetBytes(value), 0, b, 1, 4); b[5] = 0xa1; return b; }
    static int Integer(byte[] b) { if (b.Length == 2 && b[1] == 0xa1 && b[0] <= 0x7f) return b[0] - 64; Need(b.Length == 6 && b[0] == 0x82 && b[5] == 0xa1, "Expected constant bonfire menu argument."); return I(b, 1); }
    static byte[] Test(int function, int? argument, int equals)
    {
        var bytes = new List<byte>(); byte[] f = Number(function); bytes.AddRange(Slice(f, 0, f.Length - 1));
        if (argument.HasValue) { byte[] a = Number(argument.Value); bytes.AddRange(Slice(a, 0, a.Length - 1)); }
        bytes.Add(argument.HasValue ? (byte)0x85 : (byte)0x84);
        byte[] e = Number(equals); bytes.AddRange(Slice(e, 0, e.Length - 1)); bytes.Add(0x95); bytes.Add(0xa1); return bytes.ToArray();
    }
    sealed class Output : IDisposable
    {
        public MemoryStream Stream = new MemoryStream(); public BinaryWriter W;
        public Output() { W = new BinaryWriter(Stream); }
        public long Position { get { return Stream.Position; } }
        public long Reserve(int n) { long p = Position; W.Write(new byte[n]); return p; }
        public void FixI(long p, int v) { long old = Position; Stream.Position = p; W.Write(v); Stream.Position = old; }
        public void FixQ(long p, long v) { long old = Position; Stream.Position = p; W.Write(v); Stream.Position = old; }
        public void Align(int n) { while (Position % n != 0) W.Write((byte)0); }
        public byte[] Bytes() { return Stream.ToArray(); }
        public void Dispose() { W.Dispose(); Stream.Dispose(); }
    }
    public sealed class Entry { public string Name; public byte[] Bytes; public int Id, Flags; }
    public sealed class Binder
    {
        public byte[] Source, Header; public List<Entry> Entries = new List<Entry>();
        public Binder(byte[] source)
        {
            Source = source; byte[] b = StartingClasses.Unpack(source); Bounds(b, 0, 32);
            Need(Encoding.ASCII.GetString(b, 0, 4) == "BND3" && b[12] == 0x74 && b[13] == 0 && b[14] == 0 && b[15] == 0, "Unsupported Respec binder layout.");
            Header = Slice(b, 0, 32); int n = Count(I(b, 16)); Bounds(b, 32, n * 24);
            for (int i = 0; i < n; i++) {
                int p = 32 + i * 24, size = I(b, p + 4), offset = I(b, p + 8);
                Need(I(b, p) == 0x40 && size == I(b, p + 20), "Unsupported compressed binder entry.");
                Entries.Add(new Entry { Flags = I(b, p), Id = I(b, p + 12), Name = Z(b, I(b, p + 16)), Bytes = Slice(b, offset, size) });
            }
        }
        public byte[] Write()
        {
            using (var o = new Output()) {
                o.W.Write(Header); o.FixI(16, Entries.Count); o.Reserve(Entries.Count * 24);
                for (int i = 0; i < Entries.Count; i++) {
                    o.FixI(32 + i * 24 + 16, checked((int)o.Position)); o.W.Write(Encoding.ASCII.GetBytes(Entries[i].Name)); o.W.Write((byte)0);
                }
                o.Align(16); o.FixI(20, checked((int)o.Position));
                for (int i = 0; i < Entries.Count; i++) {
                    Entry e = Entries[i]; int p = 32 + i * 24; o.Align(16);
                    o.FixI(p, e.Flags); o.FixI(p + 4, e.Bytes.Length); o.FixI(p + 8, checked((int)o.Position));
                    o.FixI(p + 12, e.Id); o.FixI(p + 20, e.Bytes.Length); o.W.Write(e.Bytes);
                }
                return StartingClasses.Pack(Source, o.Bytes());
            }
        }
    }
    public sealed class Command
    {
        public int Bank, Id; public List<byte[]> Args = new List<byte[]>(); internal long Position;
        public Command(int id, params int[] args) { Bank = 1; Id = id; foreach (int a in args) Args.Add(Number(a)); }
    }
    public sealed class Condition
    {
        public long Target; public byte[] Eval; public List<Command> Pass = new List<Command>();
        public List<Condition> Sub = new List<Condition>(); internal long Position;
        public Condition(long target, byte[] eval) { Target = target; Eval = eval; }
    }
    public sealed class State
    {
        public long Id; public List<Condition> Conditions = new List<Condition>();
        public List<Command> Enter = new List<Command>(), Exit = new List<Command>(), While = new List<Command>(); internal long Position;
    }
    public sealed class Esd
    {
        byte[] header, dataHeader, name; public SortedDictionary<long, SortedDictionary<long, State>> Groups = new SortedDictionary<long, SortedDictionary<long, State>>();
        public Esd(byte[] b)
        {
            Bounds(b, 0, Data + 72);
            Need(Encoding.ASCII.GetString(b, 0, 4) == "fsSL" && I(b, 4) == 1 && I(b, 8) == 1 && I(b, 12) == 1 && I(b, 28) == 0x48 && I(b, 36) == 0x20 && I(b, 44) == 0x48 && I(b, 52) == 0x38, "Unsupported DSR talk ESD.");
            header = Slice(b, 0, Data); dataHeader = Slice(b, Data, 72);
            long np = Q(b, Data + 40), nn = Q(b, Data + 48); name = nn == 0 ? new byte[0] : Slice(b, Data + np, checked(nn * 2));
            int n = Count(Q(b, Data + 32)); long gp = Q(b, Data + 24); Bounds(b, Data + gp, n * 32);
            for (int g = 0; g < n; g++) {
                long p = Data + gp + g * 32, id = Q(b, p), start = Q(b, p + 8); int count = Count(Q(b, p + 16));
                Need(start == Q(b, p + 24), "Unexpected ESD state table."); Bounds(b, Data + start, count * 72);
                var states = new SortedDictionary<long, State>(); var byOffset = new Dictionary<long, State>();
                for (int j = 0; j < count; j++) {
                    long sp = start + j * 72; var s = new State { Id = Q(b, Data + sp) }; states.Add(s.Id, s); byOffset.Add(sp, s);
                    s.Enter = ReadCommands(b, Q(b, Data + sp + 24), Q(b, Data + sp + 32));
                    s.Exit = ReadCommands(b, Q(b, Data + sp + 40), Q(b, Data + sp + 48));
                    s.While = ReadCommands(b, Q(b, Data + sp + 56), Q(b, Data + sp + 64));
                }
                var conditions = new Dictionary<long, Condition>(); var visiting = new HashSet<long>();
                foreach (var pair in byOffset) pair.Value.Conditions = ReadConditions(b, Q(b, Data + pair.Key + 8), Q(b, Data + pair.Key + 16), byOffset, conditions, visiting, 0);
                Groups.Add(id, states);
            }
        }
        static List<Command> ReadCommands(byte[] b, long start, long count)
        {
            var list = new List<Command>(); int n = Count(count); if (n == 0) return list; Bounds(b, Data + start, n * 24);
            for (int i = 0; i < n; i++) {
                long p = Data + start + i * 24; var c = new Command(I(b, p + 4)); c.Bank = I(b, p);
                Need(c.Bank == 1 || c.Bank == 5 || c.Bank == 6 || c.Bank == 7, "Unsupported ESD command bank.");
                int an = Count(Q(b, p + 16)); long ap = Q(b, p + 8); if (an > 0) Bounds(b, Data + ap, an * 16);
                for (int j = 0; j < an; j++) c.Args.Add(Slice(b, Data + Q(b, Data + ap + j * 16), Q(b, Data + ap + j * 16 + 8)));
                list.Add(c);
            }
            return list;
        }
        static List<Condition> ReadConditions(byte[] b, long start, long count, Dictionary<long, State> states, Dictionary<long, Condition> cache, HashSet<long> visiting, int depth)
        {
            Need(depth < 64, "ESD conditions nested too deeply."); var list = new List<Condition>(); int n = Count(count); if (n == 0) return list; Bounds(b, Data + start, n * 8);
            for (int i = 0; i < n; i++) {
                long offset = Q(b, Data + start + i * 8); Need(!visiting.Contains(offset), "Cyclic ESD conditions."); Condition c;
                if (!cache.TryGetValue(offset, out c)) {
                    long p = Data + offset; Bounds(b, p, 56); long target = Q(b, p); Need(target == -1 || states.ContainsKey(target), "Missing ESD target state.");
                    c = new Condition(target == -1 ? -1 : states[target].Id, Slice(b, Data + Q(b, p + 40), Q(b, p + 48)));
                    cache.Add(offset, c); visiting.Add(offset); c.Pass = ReadCommands(b, Q(b, p + 8), Q(b, p + 16));
                    c.Sub = ReadConditions(b, Q(b, p + 24), Q(b, p + 32), states, cache, visiting, depth + 1); visiting.Remove(offset);
                }
                list.Add(c);
            }
            return list;
        }
        static void Gather(Condition c, List<Condition> list, HashSet<Condition> seen) { if (!seen.Add(c)) return; list.Add(c); foreach (var sub in c.Sub) Gather(sub, list, seen); }
        static void WriteCommands(Output o, long pointer, List<Command> list, List<Command> all)
        {
            o.FixQ(pointer, list.Count == 0 ? -1 : o.Position - Data);
            foreach (var c in list) { c.Position = o.Position; o.W.Write(c.Bank); o.W.Write(c.Id); o.W.Write((long)0); o.W.Write((long)c.Args.Count); all.Add(c); }
        }
        static int WriteConditions(Output o, long pointer, List<Condition> list)
        {
            o.FixQ(pointer, list.Count == 0 ? -1 : o.Position - Data); foreach (var c in list) o.W.Write(c.Position - Data); return list.Count;
        }
        public byte[] Write()
        {
            using (var o = new Output()) {
                o.W.Write(header); o.W.Write(dataHeader); o.FixI(40, Groups.Count); o.FixQ(Data + 24, o.Position - Data); o.FixQ(Data + 32, Groups.Count);
                var gp = new Dictionary<long, long>();
                foreach (var g in Groups) { gp[g.Key] = o.Position; o.W.Write(g.Key); o.W.Write((long)0); o.W.Write((long)g.Value.Count); o.W.Write((long)0); }
                int stateCount = 0; var dummy = new List<long[]>();
                foreach (var g in Groups) {
                    long first = o.Position; o.FixQ(gp[g.Key] + 8, first - Data); o.FixQ(gp[g.Key] + 24, first - Data);
                    foreach (var s in g.Value.Values) { s.Position = o.Position; o.W.Write(s.Id); o.W.Write((long)0); o.W.Write((long)s.Conditions.Count); o.W.Write((long)0); o.W.Write((long)s.Enter.Count); o.W.Write((long)0); o.W.Write((long)s.Exit.Count); o.W.Write((long)0); o.W.Write((long)s.While.Count); stateCount++; }
                    if (g.Value.Count > 1) { dummy.Add(new long[] { first, o.Reserve(72) }); stateCount++; }
                }
                o.FixI(48, stateCount); var allConditions = new List<Condition>();
                foreach (var g in Groups) {
                    var conditions = new List<Condition>(); var seen = new HashSet<Condition>(); foreach (var s in g.Value.Values) foreach (var c in s.Conditions) Gather(c, conditions, seen);
                    foreach (var c in conditions) { c.Position = o.Position; o.W.Write(c.Target < 0 ? -1L : g.Value[c.Target].Position - Data); o.W.Write((long)0); o.W.Write((long)c.Pass.Count); o.W.Write((long)0); o.W.Write((long)c.Sub.Count); o.W.Write((long)0); o.W.Write((long)c.Eval.Length); allConditions.Add(c); }
                }
                o.FixI(56, allConditions.Count); var commands = new List<Command>();
                foreach (var g in Groups) foreach (var s in g.Value.Values) { WriteCommands(o, s.Position + 24, s.Enter, commands); WriteCommands(o, s.Position + 40, s.Exit, commands); WriteCommands(o, s.Position + 56, s.While, commands); }
                foreach (var c in allConditions) WriteCommands(o, c.Position + 8, c.Pass, commands);
                o.FixI(64, commands.Count); int args = 0; var argPointers = new List<long>();
                foreach (var c in commands) { o.FixQ(c.Position + 8, c.Args.Count == 0 ? -1 : o.Position - Data); foreach (var a in c.Args) { argPointers.Add(o.Position); o.W.Write((long)0); o.W.Write((long)a.Length); args++; } }
                o.FixI(72, args); o.FixI(76, checked((int)(o.Position - Data))); int conditionOffsets = 0;
                foreach (var g in Groups) foreach (var s in g.Value.Values) conditionOffsets += WriteConditions(o, s.Position + 8, s.Conditions);
                foreach (var c in allConditions) conditionOffsets += WriteConditions(o, c.Position + 24, c.Sub);
                o.FixI(80, conditionOffsets);
                foreach (var c in allConditions) { o.FixQ(c.Position + 40, o.Position - Data); o.W.Write(c.Eval); }
                int ai = 0; foreach (var c in commands) foreach (var a in c.Args) { o.FixQ(argPointers[ai++], o.Position - Data); o.W.Write(a); }
                o.FixI(84, checked((int)(o.Position - Data))); o.Align(2); o.FixQ(Data + 40, name.Length == 0 ? -1 : o.Position - Data); o.W.Write(name);
                o.FixI(92, checked((int)(o.Position - Data))); o.FixI(100, checked((int)(o.Position - Data))); o.FixI(20, checked((int)(o.Position - Data))); o.Align(4);
                byte[] output = o.Bytes(); foreach (var d in dummy) Array.Copy(output, d[0], output, d[1], 72); return output;
            }
        }
    }
    public static int PatchTalk(Binder binder)
    {
        int patched = 0;
        foreach (var entry in binder.Entries) {
            var esd = new Esd(entry.Bytes); bool changed = false;
            foreach (var group in esd.Groups.Values) {
                var targets = new List<State>();
                foreach (var state in group.Values) foreach (var command in state.Enter)
                    if (command.Bank == 1 && command.Id == 19 && command.Args.Count == 3 && Integer(command.Args[1]) == LevelUpText) targets.Add(state);
                foreach (var menu in targets) {
                    Need(targets.Count == 1 && group.ContainsKey(20), "Unsupported bonfire state layout.");
                    for (int id = FirstState; id < FirstState + 5; id++) Need(!group.ContainsKey(id), "Respec ESD state collision.");
                    int index = -1; foreach (var c in menu.Enter) if (c.Bank == 1 && c.Id == 19) { Need(Integer(c.Args[0]) != MenuIndex && Integer(c.Args[1]) != TextId, "Respec menu identifier collision."); if (Integer(c.Args[1]) == LevelUpText) index = menu.Enter.IndexOf(c); }
                    Need(index >= 0, "Bonfire Level Up entry missing."); menu.Enter.Insert(index + 1, new Command(19, MenuIndex, TextId, Ready));
                    menu.Conditions.Add(new Condition(FirstState, Test(23, null, MenuIndex)));
                    var open = new State { Id = FirstState }; open.Enter.Add(new Command(67)); open.Enter.Add(new Command(11, Done, 0)); open.Enter.Add(new Command(11, Committed, 0)); open.Enter.Add(new Command(11, Request, 1)); open.Conditions.Add(new Condition(FirstState + 1, Number(1))); group.Add(open.Id, open);
                    var wait = new State { Id = FirstState + 1 };
                    // Keep the game's own bonfire death/range/network interruption rules.
                    foreach (var c in group[20].Conditions) if (c.Target != menu.Id) wait.Conditions.Add(c);
                    wait.Conditions.Add(new Condition(FirstState + 2, Test(15, Done, 1)));
                    wait.Conditions.Add(new Condition(FirstState + 3, Test(15, Ready, 0)));
                    wait.Exit.Add(new Command(11, Request, 0)); group.Add(wait.Id, wait);
                    var finish = new State { Id = FirstState + 2 }; finish.Conditions.Add(new Condition(FirstState + 4, Test(15, Committed, 1))); finish.Conditions.Add(new Condition(FirstState + 3, Number(1))); group.Add(finish.Id, finish);
                    var save = new State { Id = FirstState + 4 }; save.Enter.Add(new Command(50, 0)); save.Conditions.Add(new Condition(FirstState + 3, Number(1))); group.Add(save.Id, save);
                    var back = new State { Id = FirstState + 3 }; back.Enter.Add(new Command(11, Request, 0)); back.Enter.Add(new Command(11, Done, 0)); back.Enter.Add(new Command(11, Committed, 0)); back.Conditions.Add(new Condition(menu.Id, Number(1))); group.Add(back.Id, back);
                    // Reset stale bridge state whenever the normal bonfire menu is rebuilt.
                    menu.Enter.Insert(0, new Command(11, Request, 0)); menu.Enter.Insert(1, new Command(11, Done, 0)); menu.Enter.Insert(2, new Command(11, Committed, 0));
                    changed = true; patched++;
                }
            }
            if (changed) { byte[] output = esd.Write(); Need(Same(output, new Esd(output).Write()), "Respec ESD round-trip failed."); entry.Bytes = output; }
        }
        return patched;
    }
    public static byte[] PatchFmg(byte[] b)
    {
        Bounds(b, 0, 28); Need(b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0, "Unsupported Respec FMG.");
        int groups = Count(I(b, 12)), total = Count(I(b, 16)), offsets = I(b, 20); Bounds(b, 28, groups * 12); Bounds(b, offsets, total * 4);
        var texts = new SortedDictionary<int, byte[]>();
        for (int i = 0; i < groups; i++) {
            int p = 28 + i * 12, index = I(b, p), first = I(b, p + 4), last = I(b, p + 8); Need(last >= first && (long)last - first < total, "Invalid FMG group.");
            for (int id = first; id <= last; id++) {
                Need(index >= 0 && index < total, "Invalid FMG offset index."); int off = I(b, offsets + index++ * 4); byte[] text = null;
                if (off != 0) { int end = off; do { Bounds(b, end, 2); end += 2; } while (b[end - 2] != 0 || b[end - 1] != 0); text = Slice(b, off, end - off); }
                texts.Add(id, text);
            }
        }
        Need(!texts.ContainsKey(TextId), "Respec menu text identifier collision."); texts.Add(TextId, Encoding.Unicode.GetBytes("Respec\0"));
        using (var o = new Output()) {
            o.W.Write(Slice(b, 0, 28)); int n = 0, previous = -2, start = 0, firstId = 0, lastId = 0; var ranges = new List<int[]>();
            foreach (var pair in texts) { if (pair.Key != previous + 1 && n != 0) ranges.Add(new int[] { start, firstId, lastId }); if (pair.Key != previous + 1) { start = n; firstId = pair.Key; } lastId = pair.Key; previous = pair.Key; n++; }
            ranges.Add(new int[] { start, firstId, lastId }); o.FixI(12, ranges.Count); o.FixI(16, texts.Count);
            foreach (var range in ranges) foreach (int value in range) o.W.Write(value);
            o.FixI(20, checked((int)o.Position)); long table = o.Reserve(texts.Count * 4); int j = 0;
            foreach (var text in texts.Values) { if (text != null) { o.FixI(table + j * 4, checked((int)o.Position)); o.W.Write(text); } j++; }
            o.FixI(4, checked((int)o.Position)); return o.Bytes();
        }
    }
    static void NoFlagCollision(byte[] b)
    {
        // Conservative scan of decompressed scripts, including EZL's int32 operands.
        for (int p = 0; p <= b.Length - 4; p++) { int value = I(b, p); Need(value < Request || value > Ready, "Respec bridge event flag collision in installed scripts."); }
    }
    public static Dictionary<string, byte[]> Generate(string game, out int bonfires)
    {
        var output = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase); bonfires = 0;
        string talk = Path.Combine(game, "script", "talk"), events = Path.Combine(game, "event"), messages = Path.Combine(game, "msg");
        Need(Directory.Exists(talk) && Directory.Exists(events) && Directory.Exists(messages), "Missing required game components: script/talk, event or msg. Install the game or provide the correct path.");
        foreach (string file in Directory.GetFiles(events, "*.emevd*")) NoFlagCollision(StartingClasses.Unpack(File.ReadAllBytes(file)));
        foreach (string file in Directory.GetFiles(talk, "*.talkesdbnd.dcx")) {
            var binder = new Binder(File.ReadAllBytes(file)); foreach (var entry in binder.Entries) NoFlagCollision(entry.Bytes);
            int n = PatchTalk(binder); if (n > 0) output.Add("script\\talk\\" + Path.GetFileName(file), binder.Write()); bonfires += n;
        }
        Need(bonfires > 0, "No compatible bonfire scripts found.");
        foreach (string language in Directory.GetDirectories(messages)) {
            string file = Path.Combine(language, "menu.msgbnd.dcx"); Need(File.Exists(file), "Missing required game component: " + file); var binder = new Binder(File.ReadAllBytes(file)); int count = 0;
            foreach (var entry in binder.Entries) if (entry.Name.EndsWith("\\Event_text_.fmg", StringComparison.OrdinalIgnoreCase)) { entry.Bytes = PatchFmg(entry.Bytes); count++; }
            Need(count > 0, "Event menu text missing: " + file); output.Add("msg\\" + Path.GetFileName(language) + "\\menu.msgbnd.dcx", binder.Write());
        }
        return output;
    }
}
