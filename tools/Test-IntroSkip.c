// Read the installed executable as data only; never launch or load the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/IntroSkip.h"
#define CHECK(ok, label) do { if (!(ok)) { fprintf(stderr, "FAIL: %s\n", label); return 1; } } while (0)

int wmain(int argc, wchar_t **argv)
{
    (void)skipIntro;
    CHECK(argc == 2, "installed executable argument");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE, "open executable read-only");
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, NULL);
    CHECK(mapping, "map executable without execution");
    BYTE *base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(base, "read executable image");
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    BYTE *found = NULL;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *candidate = findIntroUpdate(base + section->VirtualAddress, section->Misc.VirtualSize);
        if (!candidate) continue;
        CHECK(!found, "unique logo update across executable sections");
        found = candidate;
    }
    CHECK(found, "installed game logo update and normal completion setter recognized");
    printf("Logo update RVA: 0x%llx\n", (unsigned long long)(found - base));

    BYTE *fixture = VirtualAlloc(NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(fixture, "isolated fixture allocation");
    memcpy(fixture + 0x100, found, 0x347);
    int32_t relative;
    memcpy(&relative, found + 0x343, sizeof(relative));
    memcpy(fixture + 0x1000, found + 0x347 + relative, 32);
    relative = 0x1000 - (0x100 + 0x347);
    memcpy(fixture + 0x443, &relative, sizeof(relative));
    CHECK(findIntroUpdate(fixture, 0x2000) == fixture + 0x100, "relocated signature recognized");
    CHECK(!findIntroUpdate(fixture, 0x446), "truncated completion rejected");
    fixture[0x432] ^= 1;
    CHECK(!findIntroUpdate(fixture, 0x2000), "changed completion rejected");
    fixture[0x432] ^= 1;
    fixture[0x1000] ^= 1;
    CHECK(!findIntroUpdate(fixture, 0x2000), "changed setter rejected");
    fixture[0x1000] ^= 1;
    memcpy(fixture + 0x500, fixture + 0x100, 0x347);
    relative = 0x1000 - (0x500 + 0x347);
    memcpy(fixture + 0x843, &relative, sizeof(relative));
    CHECK(!findIntroUpdate(fixture, 0x2000), "ambiguous signatures rejected");
    memset(fixture + 0x500, 0, 0x347);
    DWORD previous;
    CHECK(VirtualProtect(fixture, 0x2000, PAGE_READONLY, &previous), "read-only fixture");
    CHECK(patchIntroUpdate(fixture + 0x100) == ERROR_SUCCESS, "isolated patch installation");
    const BYTE expected[] = {0x33,0xd2,0x8d,0x4a,0x3c,0xe9};
    CHECK(!memcmp(fixture + 0x100, expected, sizeof(expected)), "normal completion arguments and tail jump");
    memcpy(&relative, fixture + 0x106, sizeof(relative));
    CHECK(fixture + 0x10a + relative == fixture + 0x1000, "jump preserves relocated completion destination");
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(fixture, &info, sizeof(info)) && info.Protect == PAGE_READONLY, "page protection restored");
    VirtualFree(fixture, 0, MEM_RELEASE);
    UnmapViewOfFile(base); CloseHandle(mapping); CloseHandle(file);
    puts("PASS: installed logo signature, relocation, mismatch rejection, ambiguity, completion patch, protection.");
    return 0;
}
