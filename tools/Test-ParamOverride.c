// Isolated import-redirection fixture. The installed EXE is mapped as data only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#define CHECK(ok, label) do { if (!(ok)) { fprintf(stderr, "FAIL: %s\n", label); return 1; } } while (0)
static BYTE *image;
static unsigned logs;
static void logMessage(const char *message) { (void)message; ++logs; SetLastError(12345); }
static void **findImport(const char *library, const char *name)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(image + rva);
    for (; desc->Name; ++desc) {
        if (_stricmp((char *)image + desc->Name, library) || !desc->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(image + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(image + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *entry = (IMAGE_IMPORT_BY_NAME *)(image + names->u1.AddressOfData);
            if (!strcmp((char *)entry->Name, name)) return (void **)&slots->u1.Function;
        }
    }
    return NULL;
}
static BOOL replaceImport(void **slot, void *replacement)
{
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return FALSE;
    *slot = replacement;
    return VirtualProtect(slot, sizeof(*slot), old, &ignored);
}
#include "../runtime/ParamOverride.h"
static BOOL marker(HANDLE file)
{
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    char data[4]; DWORD read = 0;
    BOOL success = ReadFile(file, data, sizeof(data), &read, NULL);
    CloseHandle(file);
    return success && read == 4 && !memcmp(data, "TEST", 4);
}
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 3, "installed EXE and temporary fixture root arguments");
    HANDLE exe = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(exe != INVALID_HANDLE_VALUE, "read installed EXE");
    HANDLE mapping = CreateFileMappingW(exe, NULL, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, NULL);
    CHECK(mapping, "map installed EXE without execution");
    BYTE *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(view, "read installed executable image");
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)view;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(view + dos->e_lfanew);
    image = VirtualAlloc(NULL, nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image, "allocate private import fixture");
    memcpy(image, view, nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        memcpy(image + section->VirtualAddress, view + section->VirtualAddress, section->Misc.VirtualSize);
    UnmapViewOfFile(view);
    void **wide = findImport("KERNEL32.dll", "CreateFileW");
    void **ansi = findImport("KERNEL32.dll", "CreateFileA");
    void **info = findImport("KERNEL32.dll", "GetFileAttributesW");
    CHECK(wide && ansi && info, "all required imports exist in installed EXE");
    CHECK(replaceImport(wide, (void *)CreateFileW) && replaceImport(ansi, (void *)CreateFileA) &&
        replaceImport(info, (void *)GetFileAttributesW), "bind private fixture to Windows file APIs");
    CHECK(initializeParamOverride(argv[2]) == ERROR_SUCCESS, "install redirection in isolated fixture");
    CHECK(*wide == (void *)openParameterW && *ansi == (void *)openParameterA &&
        *info == (void *)attributesParameterW, "three imports redirected");
    typedef HANDLE (WINAPI *OpenW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    typedef HANDLE (WINAPI *OpenA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    typedef DWORD (WINAPI *AttrsW)(LPCWSTR);
    OpenW callW = (OpenW)*wide; OpenA callA = (OpenA)*ansi; AttrsW callInfo = (AttrsW)*info;
    CHECK(loadRespecOverrides(argv[2]) == ERROR_SUCCESS && respecOverrideCount == 22, "load allowlisted external Respec assets");
    wchar_t mapped[MAX_PATH];
    CHECK(!respecAssetName(L"script\\talk\\..\\archive.talkesdbnd.dcx") && !respecAssetName(L"D:\\game\\archive.talkesdbnd.dcx"), "manifest path traversal and absolute paths rejected");
    const wchar_t *arenaNames[] = {
        L"msg\\ENGLISH\\item.msgbnd.dcx",
        L"map\\MapStudio\\m10_02_00_00.msb", L"map\\MapStudio\\m14_01_00_00.msb",
        L"map\\m14_01_00_00\\m9900B1A14.flver.dcx",
        L"map\\m14_01_00_00\\m9901B1A14.flver.dcx", L"map\\m14\\m14_9999.tpf.dcx",
        L"map\\m14\\GI_EnvM_m14.tpfbhd", L"map\\m14\\GI_EnvM_m14.tpfbdt",
        L"param\\DrawParam\\a14_DrawParam.parambnd.dcx",
        L"map\\m14_01_00_00\\m14_01_00_00.nvmbnd.dcx",
        L"map\\m14_01_00_00\\m14_01_00_00.mcp", L"map\\m14_01_00_00\\m14_01_00_00.mcg",
        L"map\\m14_01_00_00\\h14_01_00_00.hkxbhd", L"map\\m14_01_00_00\\h14_01_00_00.hkxbdt",
        L"map\\m14_01_00_00\\l14_01_00_00.hkxbhd", L"map\\m14_01_00_00\\l14_01_00_00.hkxbdt",
        L"event\\m10_02_00_00.emevd.dcx", L"event\\m14_01_00_00.emevd.dcx"
    };
    for (size_t i = 0; i < sizeof(arenaNames) / sizeof(*arenaNames); ++i) {
        CHECK(respecAssetName(arenaNames[i]), "exact arena asset allowlist");
        CHECK(mapRespecAssetW(arenaNames[i], mapped), "arena assets redirect through the manifest");
        CHECK(marker(callW(arenaNames[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "arena asset reads use external files");
        CHECK(callInfo(arenaNames[i]) != INVALID_FILE_ATTRIBUTES, "new arena assets report external attributes");
    }
    CHECK(!respecAssetName(L"map\\MapStudio\\m14_00_00_00.msb") &&
          !respecAssetName(L"map\\m14_01_00_00\\m9902B1A14.flver.dcx") &&
          !respecAssetName(L"event\\common.emevd.dcx"), "unrelated map/event assets rejected");
    CHECK(!mapRespecAssetW(NULL, mapped) && !mapRespecAssetA(NULL, mapped), "null asset paths pass through");
    CHECK(mapRespecAssetW(L"script/talk/m10_00_00_00.talkesdbnd.dcx", mapped), "relative talk archive mapping");
    CHECK(!mapRespecAssetW(L"othermsg/ENGLISH/menu.msgbnd.dcx", mapped) && !mapRespecAssetW(L"msg/ENGLISH/menu.msgbnd.dcx.bak", mapped), "asset path boundaries enforced");
    CHECK(marker(callW(L"Z:\\unavailable\\script\\talk\\m10_00_00_00.talkesdbnd.dcx", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "absolute talk request redirects outside installed game");
    CHECK(marker(callA("Z:/unavailable/MSG/english/MENU.MSGBND.DCX", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "ANSI absolute menu request with case/slash normalization");
    CHECK(callInfo(L"Z:\\unavailable\\msg\\ENGLISH\\menu.msgbnd.dcx") != INVALID_FILE_ATTRIBUTES, "Respec attribute lookup redirects");
    CHECK(callW(L"Z:\\unavailable\\msg\\ENGLISH\\menu.msgbnd.dcx", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL) == INVALID_HANDLE_VALUE, "Respec writes never redirected");
    CHECK(respecAssetName(L"menu\\menu.drb") && respecAssetName(L"menu\\menu.drb.dcx") && !respecAssetName(L"menu\\other.drb.dcx"), "only the two native menu layouts are allowlisted");
    CHECK(marker(callW(L"Z:\\unavailable\\menu\\menu.drb.dcx", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "compressed native menu layout redirects externally");
    CHECK(marker(callA("Z:/unavailable/menu/menu.drb", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "plain native menu layout redirects externally");
    CHECK(callW(L"Z:\\unavailable\\menu\\menu.drb.dcx", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL) == INVALID_HANDLE_VALUE, "native menu writes never redirect");
    CHECK(parameterNameW(L"param/GameParam/GameParam.parambnd.dcx"), "relative path");
    CHECK(parameterNameW(L"C:\\original\\PARAM\\GAMEPARAM\\GAMEPARAM.PARAMBND.DCX"), "absolute path and case");
    CHECK(!parameterNameW(L"C:\\otherparam\\GameParam\\GameParam.parambnd.dcx"), "directory boundary");
    CHECK(!parameterNameW(L"param\\GameParam\\GameParam.parambnd.dcx.bak"), "filename boundary");
    CHECK(!parameterNameW(NULL) && !parameterNameA(NULL), "null path matching");
    CHECK(marker(callW(L"Z:\\unavailable\\param\\GameParam\\GameParam.parambnd.dcx", GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "absolute Unicode request reads external archive");
    CHECK(marker(callA("Z:/unavailable/PARAM/GAMEPARAM/GAMEPARAM.PARAMBND.DCX", GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)), "ANSI and forward-slash request reads external archive");
    CHECK(callInfo(L"Z:\\unavailable\\param\\GameParam\\GameParam.parambnd.dcx") != INVALID_FILE_ATTRIBUTES,
        "attribute lookup redirects before file open");
    CHECK(parameterOpened == 1 && logs == 2, "first successful parameter open is logged once");
    HANDLE unrelated = callW(L"Z:\\unavailable\\other.param", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(unrelated == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PATH_NOT_FOUND, "unrelated request passes through");
    HANDLE write = callW(L"Z:\\unavailable\\param\\GameParam\\GameParam.parambnd.dcx", GENERIC_WRITE,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(write == INVALID_HANDLE_VALUE, "write request is not redirected");
    CHECK(DeleteFileW(parameterPath), "remove only temporary fixture archive");
    HANDLE missing = callW(L"Z:\\unavailable\\param\\GameParam\\GameParam.parambnd.dcx", GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(missing == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND,
        "missing override fails and preserves Windows error despite logging");
    CHECK(initializeParamOverride(argv[2]) == ERROR_FILE_NOT_FOUND, "missing override rejected at initialization");
    VirtualFree(image, 0, MEM_RELEASE); CloseHandle(mapping); CloseHandle(exe);
    puts("PASS: installed import compatibility; isolated IAT hooks; absolute/relative/ANSI/Unicode paths; attributes; non-target/write passthrough; logging; error preservation; missing override rejection.");
    return 0;
}
