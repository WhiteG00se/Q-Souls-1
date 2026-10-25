// Execute only the native bonfire-ID arithmetic in a private isolated fixture.
// The installed game EXE is mapped as data; the game is never started.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x, message) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 2, "game executable argument");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE, "read installed executable");
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, NULL);
    CHECK(mapping, "map executable as non-executable data");
    BYTE *image = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(image, "map read-only executable image");
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    const BYTE gate[] = {0x8d,0x88,0x62,0xf8,0xff,0xff,0x83,0xf9,0x13,0x41,0x0f,0x46,0xd9};
    const BYTE begin[] = {0x44,0x8b,0xc0,0x44,0x8d,0x88,0xe8,0x03,0,0};
    BYTE *found = NULL; unsigned matches = 0;
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) || sections[i].Misc.VirtualSize < 60) continue;
        BYTE *section = image + sections[i].VirtualAddress;
        for (DWORD p = 44; p + sizeof(gate) + 2 <= sections[i].Misc.VirtualSize; ++p) {
            if (!memcmp(section + p, gate, sizeof(gate))) { found = section + p - 44; ++matches; }
        }
    }
    CHECK(matches == 1 && found, "unique native bonfire gate");
    CHECK(!memcmp(found, begin, sizeof(begin)) && found[57] == 0x8b && found[58] == 0xc3,
          "expected self-contained native integer arithmetic");
    // Preserve RBX and adapt the native block's EAX input to the Windows RCX ABI.
    const BYTE prefix[] = {0x53,0x8b,0xc1,0x83,0xcb,0xff};
    BYTE *code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(code, "allocate isolated arithmetic fixture");
    memcpy(code, prefix, sizeof(prefix)); memcpy(code + sizeof(prefix), found, 59);
    code[sizeof(prefix) + 59] = 0x5b; code[sizeof(prefix) + 60] = 0xc3;
    DWORD old;
    CHECK(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old) && FlushInstructionCache(GetCurrentProcess(), code, 67),
          "prepare arithmetic-only fixture");
    typedef int (*Lookup)(int);
    Lookup lookup = (Lookup)(void *)code;
    CHECK(lookup(1410970) == -1, "old Hell actor is rejected by the game's native lookup");
    CHECK(lookup(1410965) == 1411965, "corrected Hell actor resolves to its bonfire object");
    CHECK(lookup(-1) == -1, "invalid actor remains rejected");
    for (int suffix = 1940; suffix < 1980; ++suffix) {
        int object = 1410000 + suffix, expected = suffix >= 1950 && suffix <= 1969 ? object : -1;
        CHECK(lookup(object - 1000) == expected, "exact native bonfire slot boundaries");
    }
    VirtualFree(code, 0, MEM_RELEASE); UnmapViewOfFile(image); CloseHandle(mapping); CloseHandle(file);
    puts("PASS: native bonfire arithmetic rejects the old 1970 slot, accepts the corrected 1965 slot, and enforces the exact 1950..1969 bounds. Game not launched.");
    return 0;
}
