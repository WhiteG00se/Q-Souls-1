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
#include "../runtime/SaveExtension.h"
static BOOL content(const wchar_t *path, const char *expected)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    char buffer[32] = {0}; DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL);
    CloseHandle(file); return ok && !strcmp(buffer, expected);
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
    const char *names[] = {"CreateFileW", "CreateFileA", "GetFileAttributesW", "DeleteFileW", "DeleteFileA", "MoveFileW", "MoveFileA", "FindFirstFileW"};
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    for (size_t i = 0; i < 8; ++i) {
        void **slot = findImport("KERNEL32.dll", names[i]);
        CHECK(slot, "save I/O imports exist in installed game");
        CHECK(replaceImport(slot, (void *)GetProcAddress(kernel, names[i])), "bind isolated imports to real APIs");
    }
    SetEnvironmentVariableW(L"QSOULS_SAVE_EXTENSION", L"sl2");
    CHECK(initializeSaveExtension() == ERROR_SUCCESS, "sl2 default needs no hooks");
    CHECK(*findImport("KERNEL32.dll", "CreateFileW") == (void *)CreateFileW, "default leaves save imports untouched");
    SetEnvironmentVariableW(L"QSOULS_SAVE_EXTENSION", L"../bad");
    CHECK(initializeSaveExtension() == ERROR_INVALID_PARAMETER, "unsafe extension rejected");
    SetEnvironmentVariableW(L"QSOULS_SAVE_EXTENSION", L"co2");
    SetEnvironmentVariableW(L"QSOULS_SAVE_ROOT", argv[2]);
    CHECK(initializeSaveExtension() == ERROR_SUCCESS, "install save hooks on private game image");
    CHECK(logs == 1, "save routing initialization logged once");
    wchar_t original[SAVE_PATH_CAPACITY], selected[SAVE_PATH_CAPACITY], backup[SAVE_PATH_CAPACITY], mapped[SAVE_PATH_CAPACITY];
    swprintf(original, SAVE_PATH_CAPACITY, L"%ls\\DRAKS0005.sl2", argv[2]);
    swprintf(selected, SAVE_PATH_CAPACITY, L"%ls\\DRAKS0005.co2", argv[2]);
    swprintf(backup, SAVE_PATH_CAPACITY, L"%ls\\DRAKS0005.sl2.bak", argv[2]);
    HANDLE file = CreateFileW(original, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE, "create vanilla fixture only");
    DWORD written;
    CHECK(WriteFile(file, "VANILLA", 7, &written, NULL), "write vanilla marker"); CloseHandle(file);
    CHECK(mapSaveW(original, mapped) == 1 && !wcscmp(mapped, selected), "selected filename mapping");
    CHECK(mapSaveW(L"Z:\\not-the-save-root\\DRAKS0005.sl2", mapped) == 0, "outside-root paths untouched");
    CHECK(mapSaveW(L"DRAKS0005.sl2evil", mapped) == 0 && mapSaveW(NULL, mapped) == 0, "filename boundary and null path");
    file = saveOpenW(original, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "missing selected save never falls back to vanilla");
    file = saveOpenW(original, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE, "write request creates selected extension");
    CHECK(WriteFile(file, "MODDED", 6, &written, NULL), "write selected marker"); CloseHandle(file);
    CHECK(content(original, "VANILLA") && content(selected, "MODDED"), "vanilla and selected fixtures remain separate");
    CHECK(saveInfoW(original) != INVALID_FILE_ATTRIBUTES, "attributes use selected extension");
    WIN32_FIND_DATAW data;
    HANDLE search = saveSearchW(original, &data);
    CHECK(search != INVALID_HANDLE_VALUE && !_wcsicmp(data.cFileName, L"DRAKS0005.co2"), "enumeration uses selected extension"); FindClose(search);
    char ansi[SAVE_PATH_CAPACITY];
    CHECK(WideCharToMultiByte(CP_ACP, 0, original, -1, ansi, SAVE_PATH_CAPACITY, NULL, NULL), "encode ANSI path");
    file = saveOpenA(ansi, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE, "ANSI open uses selected save"); CloseHandle(file);
    CHECK(saveRenameW(original, backup), "rename selected file to selected backup");
    CHECK(content(original, "VANILLA") && GetFileAttributesW(selected) == INVALID_FILE_ATTRIBUTES, "rename did not touch vanilla");
    CHECK(saveRemoveW(backup), "delete selected backup");
    CHECK(content(original, "VANILLA"), "delete did not touch vanilla");
    wcscpy(selectedSaveExtension, L"mySave9");
    CHECK(mapSaveW(original, mapped) == 1 && wcsstr(mapped, L"DRAKS0005.mySave9"), "custom extension mapping");
    CHECK(DeleteFileW(original), "remove vanilla test fixture only");
    VirtualFree(image, 0, MEM_RELEASE); CloseHandle(mapping); CloseHandle(exe);
    puts("PASS: game import compatibility; sl2 default untouched; invalid extensions rejected; scoped read/write/attributes/enumeration/ANSI/rename/delete; missing-save isolation; custom extension mapping. Temporary save fixtures only; game never executed.");
    return 0;
}