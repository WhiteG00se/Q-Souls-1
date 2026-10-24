using System;
using System.IO;
using System.IO.Compression;
using System.Text;

// Narrow reader for the Steam DSR DCX/DFLT + little-endian BND3 layout.
// Format references: soulsmods/SoulsFormats; stat offsets: Grimrukh/soulstruct CHARACTER_INIT_PARAM.
public static class StartingClasses
{
    static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidDataException(message);
    }
    static void Range(byte[] data, int offset, int size)
    {
        Require(offset >= 0 && size >= 0 && offset <= data.Length - size, "Invalid parameter bounds.");
    }
    static int Int32(byte[] data, int offset)
    {
        Range(data, offset, 4);
        return BitConverter.ToInt32(data, offset);
    }
    static int Big32(byte[] data, int offset)
    {
        Range(data, offset, 4);
        return checked((int)(((uint)data[offset] << 24) | ((uint)data[offset + 1] << 16) |
            ((uint)data[offset + 2] << 8) | data[offset + 3]));
    }
    static void WriteBig32(byte[] data, int offset, int value)
    {
        for (int i = 3; i >= 0; i--) { data[offset + i] = (byte)value; value >>= 8; }
    }
    static uint Adler(byte[] data)
    {
        uint a = 1, b = 0;
        foreach (byte value in data) { a = (a + value) % 65521; b = (b + a) % 65521; }
        return (b << 16) | a;
    }
    static string Text(byte[] data, int offset, int limit)
    {
        Range(data, offset, limit);
        int end = offset;
        while (end < offset + limit && data[end] != 0) end++;
        return Encoding.ASCII.GetString(data, offset, end - offset);
    }
    public static byte[] Unpack(byte[] source)
    {
        Range(source, 0, 82);
        Require(Text(source, 0, 4) == "DCX" && Big32(source, 4) == 0x10000 &&
            Big32(source, 8) == 0x18 && Big32(source, 12) == 0x24 &&
            Big32(source, 16) == 0x24 && Big32(source, 20) == 0x2c &&
            Text(source, 24, 4) == "DCS" && Text(source, 36, 4) == "DCP" &&
            Text(source, 40, 4) == "DFLT" && Big32(source, 44) == 0x20 &&
            Big32(source, 48) == 0x09000000 && Big32(source, 52) == 0 &&
            Big32(source, 56) == 0 && Big32(source, 60) == 0 &&
            Big32(source, 64) == 0x10100 && Text(source, 68, 4) == "DCA" &&
            Big32(source, 72) == 8, "Unsupported DSR parameter compression.");
        int length = Big32(source, 28), compressed = Big32(source, 32);
        Require(length > 0 && length <= 64 * 1024 * 1024 && compressed == source.Length - 76 &&
            source[76] == 0x78 && ((source[76] << 8) + source[77]) % 31 == 0 &&
            (source[77] & 0x20) == 0, "Invalid DSR compressed sizes or zlib header.");
        byte[] data = new byte[length];
        using (var input = new MemoryStream(source, 78, compressed - 6))
        using (var inflate = new DeflateStream(input, CompressionMode.Decompress))
        {
            int position = 0, read;
            while (position < length && (read = inflate.Read(data, position, length - position)) > 0)
                position += read;
            Require(position == length && inflate.ReadByte() == -1, "Invalid decompressed size.");
        }
        uint checksum = ((uint)source[source.Length - 4] << 24) | ((uint)source[source.Length - 3] << 16) |
            ((uint)source[source.Length - 2] << 8) | source[source.Length - 1];
        Require(Adler(data) == checksum, "Invalid parameter checksum.");
        return data;
    }
    public static byte[] Pack(byte[] header, byte[] data)
    {
        using (var output = new MemoryStream())
        {
            output.Write(header, 0, 76);
            output.WriteByte(0x78); output.WriteByte(0x9c);
            using (var deflate = new DeflateStream(output, CompressionLevel.Optimal, true))
                deflate.Write(data, 0, data.Length);
            uint checksum = Adler(data);
            for (int i = 3; i >= 0; i--) output.WriteByte((byte)(checksum >> (8 * i)));
            byte[] packed = output.ToArray();
            WriteBig32(packed, 28, data.Length);
            WriteBig32(packed, 32, packed.Length - 76);
            return packed;
        }
    }
    public static byte[] Patch(byte[] source)
    {
        byte[] data = Unpack(source);
        Range(data, 0, 32);
        Require(Text(data, 0, 4) == "BND3" && data[12] == 0x74 && data[13] == 0 &&
            data[14] == 0 && data[15] == 0, "Unsupported DSR parameter binder.");
        int files = Int32(data, 16), param = -1, paramSize = 0;
        Require(files > 0 && files <= 1000, "Invalid binder file count.");
        Range(data, 32, files * 24);
        for (int i = 0; i < files; i++)
        {
            int entry = 32 + 24 * i, name = Int32(data, entry + 16);
            Range(data, name, 1);
            string path = Text(data, name, data.Length - name);
            if (!path.EndsWith("\\CharaInitParam.param", StringComparison.Ordinal)) continue;
            Require(param == -1 && Int32(data, entry) == 0x40, "Unsupported character parameter entry.");
            param = Int32(data, entry + 8); paramSize = Int32(data, entry + 4);
            Require(paramSize == Int32(data, entry + 20), "Compressed character parameter entry.");
            Range(data, param, paramSize);
        }
        Require(param >= 0 && paramSize >= 48, "Character parameters missing.");
        Require(Text(data, param + 12, 32) == "CHARACTER_INIT_PARAM" &&
            BitConverter.ToInt16(data, param + 8) == 2 && Int32(data, param + 44) == 0x200,
            "Unsupported character parameter layout.");
        int count = BitConverter.ToUInt16(data, param + 10);
        Require(48 + count * 12 <= paramSize, "Invalid character row table.");
        bool[] found = new bool[20];
        for (int i = 0; i < count; i++)
        {
            int row = param + 48 + i * 12, id = Int32(data, row);
            // 2000..2009 initialize the character; 3000..3009 supply creation-menu previews.
            int index = id >= 2000 && id <= 2009 ? id - 2000 :
                id >= 3000 && id <= 3009 ? id - 3000 + 10 : -1;
            if (index < 0) continue;
            Require(!found[index], "Duplicate starting class.");
            int relative = Int32(data, row + 4);
            Require(relative >= 48 + count * 12 && relative <= paramSize - 240,
                "Invalid starting-class row.");
            int start = param + relative;
            // soulLv at 0xc0; seven main stats at 0xc2..0xc8.
            // Resistance is baseDurability (0xcb), NOT the unused baseLuc (0xc9).
            data[start + 192] = 1; data[start + 193] = 0;
            data[start + 194] = 12;
            for (int stat = 195; stat <= 200; stat++) data[start + stat] = 10;
            data[start + 203] = 10;
            found[index] = true;
        }
        foreach (bool present in found) Require(present, "A starting class is missing.");
        return Pack(source, data);
    }
}
