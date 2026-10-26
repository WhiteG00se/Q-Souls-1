using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static partial class RespecAssets
{
    public const int QIconTextId = 30004990;
    public static byte[] PatchQualityLayout(byte[] source)
    {
        // The uncompressed file is the unused PTDE-era layout. Only the active
        // Remastered layout has the current four-category Start bar.
        if (Encoding.ASCII.GetString(source, 0, 4) != "DCX\0") return source;
        byte[] b = StartingClasses.Unpack(source);
        Need(Encoding.ASCII.GetString(b, 0, 4) == "DRB\0", "Unsupported quality menu layout.");
        var blocks = new List<LayoutBlock>(); var names = new Dictionary<string, LayoutBlock>();
        for (int p = 16; p < b.Length;) {
            Bounds(b, p, 16); string name = Encoding.ASCII.GetString(b, p, 4);
            int size = I(b, p + 4), count = Count(I(b, p + 8));
            Need(size >= 0 && size % 16 == 0 && I(b, p + 12) == 0, "Invalid quality DRB section.");
            var block = new LayoutBlock { Name = name, Count = count, Bytes = Slice(b, p + 16, size) };
            blocks.Add(block); names.Add(name.TrimEnd('\0'), block); p = checked(p + 16 + size);
        }
        var strings = names["STR"]; var shapes = names["SHAP"]; var data = names["SHPR"];
        var controls = names["CTRL"]; var objects = names["DLGO"]; var groups = names["DLG"];
        Need(objects.Count * 32 == objects.Bytes.Length && groups.Count * 64 == groups.Bytes.Length, "Invalid quality DRB records.");
        int used = 0;
        for (int i = 0; i < strings.Count; ++i) {
            string text = LayoutString(strings.Bytes, used);
            used += Encoding.Unicode.GetByteCount(text) + 2;
        }
        int root = -1, cursor = -1, button = -1, rightAnchor = -1, icons = 0;
        for (int g = 0; g < groups.Count; ++g) {
            int at = g * 64; string groupName = LayoutString(strings.Bytes, I(groups.Bytes, at));
            int count = Count(I(groups.Bytes, at + 32)), start = I(groups.Bytes, at + 36);
            Bounds(objects.Bytes, start, count * 32);
            if (groupName == "R00-00") { Need(root < 0, "Duplicate Start bar."); root = at; }
            for (int j = 0; j < count; ++j) {
                int obj = start + j * 32; string name = LayoutString(strings.Bytes, I(objects.Bytes, obj));
                if (groupName == "I06-00" && name == "button0_0") button = obj;
                if (groupName != "R00-00") continue;
                Need(name != "icon_q_souls" && name != "cursor0_4", "Q category already patched.");
                if (name.StartsWith("cursor0_", StringComparison.Ordinal)) icons++;
                if (name == "cursor0_3") cursor = obj;
                if (name == "category_right") rightAnchor = obj;
            }
        }
        Need(root >= 0 && cursor >= 0 && button >= 0 && rightAnchor >= 0 && icons == 4, "Expected four native Start categories.");
        int cursorData = I(shapes.Bytes, I(objects.Bytes, cursor + 4) + 4);
        int newLeft = BitConverter.ToInt16(data.Bytes, cursorData) + 50;
        int newTop = BitConverter.ToInt16(data.Bytes, cursorData + 2);
        int newRight = BitConverter.ToInt16(data.Bytes, cursorData + 4) + 50;
        int newBottom = BitConverter.ToInt16(data.Bytes, cursorData + 6);
        // Leave the four original icons and their hit boxes in place. Move only
        // the right shoulder-button guide to make room for the fifth category.
        int rightData = I(shapes.Bytes, I(objects.Bytes, rightAnchor + 4) + 4);
        PutShort(data.Bytes, rightData, BitConverter.ToInt16(data.Bytes, rightData) + 50);
        PutShort(data.Bytes, rightData + 4, BitConverter.ToInt16(data.Bytes, rightData + 4) + 50);
        byte[] textData = Slice(strings.Bytes, 0, used); var added = new List<byte>();
        int[] templates = { cursor, button }; string[] labels = { "cursor0_4", "icon_q_souls" };
        for (int j = 0; j < 2; ++j) {
            byte[] obj = Slice(objects.Bytes, templates[j], 32);
            byte[] shape = Slice(shapes.Bytes, I(obj, 4), 8);
            string kind = LayoutString(strings.Bytes, I(shape, 0));
            Need(kind == (j == 0 ? "Null" : "Text"), "Unexpected quality row template.");
            byte[] parameter = Slice(data.Bytes, I(shape, 4), j == 0 ? 8 : 50);
            PutShort(parameter, 0, newLeft + (j == 1 ? 6 : 0));
            PutShort(parameter, 2, newTop + (j == 1 ? 4 : 0));
            PutShort(parameter, 4, newRight); PutShort(parameter, 6, newBottom);
            if (j == 1) {
                Need(I(parameter, 40) == 30004000, "Unexpected text template.");
                Put(parameter, 32, 32); Put(parameter, 40, QIconTextId);
            }
            Put(shape, 4, data.Bytes.Length); data.Bytes = Append(data.Bytes, parameter);
            Put(obj, 0, textData.Length); textData = Append(textData, Encoding.Unicode.GetBytes(labels[j] + "\0"));
            Put(obj, 4, shapes.Count * 8); shapes.Bytes = Append(Slice(shapes.Bytes, 0, shapes.Count * 8), shape); shapes.Count++;
            int control = I(obj, 8); Put(obj, 8, controls.Count * 8);
            controls.Bytes = Append(Slice(controls.Bytes, 0, controls.Count * 8), Slice(controls.Bytes, control, 8)); controls.Count++;
            added.AddRange(obj);
        }
        strings.Bytes = textData; strings.Count += 2;
        int insertion = I(groups.Bytes, root + 36) + I(groups.Bytes, root + 32) * 32;
        objects.Bytes = Append(Append(Slice(objects.Bytes, 0, insertion), added.ToArray()), Slice(objects.Bytes, insertion, objects.Bytes.Length - insertion)); objects.Count += 2;
        for (int g = 0; g < groups.Count; ++g) {
            int at = g * 64;
            if (at == root) Put(groups.Bytes, at + 32, I(groups.Bytes, at + 32) + 2);
            else if (I(groups.Bytes, at + 36) >= insertion) Put(groups.Bytes, at + 36, I(groups.Bytes, at + 36) + 64);
        }
        using (var output = new Output()) {
            output.W.Write(Slice(b, 0, 16));
            foreach (var block in blocks) {
                output.W.Write(Encoding.ASCII.GetBytes(block.Name)); output.W.Write((block.Bytes.Length + 15) & ~15);
                output.W.Write(block.Count); output.W.Write(0); output.W.Write(block.Bytes); output.Align(16);
            }
            return StartingClasses.Pack(source, output.Bytes());
        }
    }
}
