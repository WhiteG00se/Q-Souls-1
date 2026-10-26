using System;
using System.Collections.Generic;

public static partial class RespecAssets
{
    public const int QSoulsTextId = 15000993;
    public const int QualityRequest=70000980, QualityDone=70000981, QualityReady=70000983;
    public const int QSoulsMenuIndex = 14;
    static Condition CopyQualityCondition(Condition source)
    {
        var result = new Condition(source.Target, (byte[])source.Eval.Clone());
        foreach (var c in source.Pass) {
            var copy = new Command(c.Id); copy.Bank = c.Bank;
            foreach (var arg in c.Args) copy.Args.Add((byte[])arg.Clone());
            result.Pass.Add(copy);
        }
        foreach (var child in source.Sub) result.Sub.Add(CopyQualityCondition(child));
        return result;
    }
    static Condition QualitySelection(long target, int index)
    {
        return new Condition(target, new byte[] { 0x57, 0x84, (byte)(64 + index), 0x95, 0xa1 });
    }
    static void PatchQualityBonfires(Esd esd)
    {
        foreach (var states in esd.Groups.Values) {
            var menus = new List<State>();
            foreach (var state in states.Values) foreach (var c in state.Enter)
                if (c.Bank == 1 && c.Id == 19 && Integer(c.Args[1]) == TextId) menus.Add(state);
            foreach (var menu in menus) {
                foreach (var c in menu.Enter) if (c.Bank == 1 && c.Id == 19)
                    Need(Integer(c.Args[0]) != QSoulsMenuIndex, "Q Souls bonfire menu index collision.");
                long id = 0; foreach (long key in states.Keys) if (key >= id) id = key + 1;
                Need(states.ContainsKey(20), "Native Level Up wait state missing.");
                var open=new State { Id=id };
                open.Enter.Add(new Command(67)); // Exactly the reference Respec flow: close the native list first.
                open.Enter.Add(new Command(11,QualityDone,0));
                open.Enter.Add(new Command(11,QualityRequest,1));
                open.Conditions.Add(new Condition(id+1,Number(1)));
                var wait=new State { Id=id+1 };
                foreach (var c in states[20].Conditions) if (c.Target!=menu.Id) wait.Conditions.Add(CopyQualityCondition(c));
                wait.Conditions.Add(new Condition(id+2,QualityFlagTest(QualityDone,1)));
                wait.Conditions.Add(new Condition(id+2,QualityFlagTest(QualityReady,0)));
                wait.Exit.Add(new Command(11,QualityRequest,0));
                var back=new State { Id=id+2 };
                back.Enter.Add(new Command(11,QualityRequest,0)); back.Enter.Add(new Command(11,QualityDone,0));
                back.Conditions.Add(new Condition(menu.Id,Number(1)));
                menu.Enter.Add(new Command(11,QualityRequest,0)); menu.Enter.Add(new Command(11,QualityDone,0));
                menu.Enter.Add(new Command(19,QSoulsMenuIndex,QSoulsTextId,QualityReady));
                menu.Conditions.Add(QualitySelection(open.Id,QSoulsMenuIndex));
                states.Add(open.Id,open); states.Add(wait.Id,wait); states.Add(back.Id,back);
            }
        }
    }
    static byte[] QualityFlagTest(int flag,int value)
    {
        var result=new List<byte>(); result.Add(0x4f);
        byte[] argument=Number(flag); for(int i=0;i<argument.Length-1;++i) result.Add(argument[i]);
        result.Add(0x85); result.Add((byte)(64+value)); result.Add(0x95); result.Add(0xa1);
        return result.ToArray();
    }
    public static void CheckQualityFlagCollision(byte[] bytes)
    {
        for(int i=0;i<=bytes.Length-4;++i) {
            int n=BitConverter.ToInt32(bytes,i);
            Need(n<QualityRequest || n>QualityReady,"Q Souls bridge flags collide with installed scripts.");
        }
    }
    static byte[] QualityTexts(byte[] bytes)
    {
        bytes = PatchFmg(bytes, "Q Souls", QSoulsTextId);
        return bytes;
    }
}
