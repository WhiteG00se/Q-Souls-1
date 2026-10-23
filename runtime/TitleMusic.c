#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static HMODULE self;
static HANDLE changed, ready;
static volatile LONG active;
static uintptr_t cameraGlobal;

static void *titleMovie;
static wchar_t trackPath[MAX_PATH], logPath[MAX_PATH];
static void * (__stdcall *realOpen)(const char *, uint32_t);
static void (__stdcall *realClose)(void *);
static int (__stdcall *soundOnOff)(void *, int);

static void logMessage(const char *message)
{
    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, message, (DWORD)strlen(message), &written, NULL);
        WriteFile(file, "\r\n", 2, &written, NULL);
        CloseHandle(file);
    }
}

// Camera pointer layout reference: JKAnderson/DSR-Gadget, DSROffsets.cs.
// Scan the installed executable instead of hard-coding a version-specific address.
static uintptr_t findCameraGlobal(void)
{
    const BYTE pattern[] = {0x48,0x8b,0x0d,0,0,0,0,0xe8,0,0,0,0,
        0x48,0x8b,0x4e,0x68,0x48,0x8b,0x05,0,0,0,0,0x48,0x89,0x48,0x60};
    const char mask[] = "xxx????x????xxxxxxx????xxxx";
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *data = base + section->VirtualAddress;
        DWORD size = section->Misc.VirtualSize;
        for (size_t i = 0; i + sizeof(pattern) <= size; ++i) {
            size_t j = 0;
            for (; j < sizeof(pattern); ++j)
                if (mask[j] != '?' && data[i+j] != pattern[j]) break;
            if (j != sizeof(pattern)) continue;
            if (found) return 0; // Refuse ambiguous layouts.
            int32_t relative;
            memcpy(&relative, data + i + 3, sizeof(relative));
            found = (uintptr_t)(data + i + 7) + relative;
        }
    }
    return found;
}

static BOOL gameplayLoaded(void)
{
    uintptr_t pointer = cameraGlobal;
    const size_t offsets[] = {0, 0x60, 0x60};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
        SIZE_T read;
        uintptr_t next = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)(pointer + offsets[i]),
                               &next, sizeof(next), &read) || read != sizeof(next) || !next)
            return FALSE;
        pointer = next;
    }
    return TRUE;
}

// Keep playing across the short title video's end, until gameplay actually loads.
static DWORD WINAPI musicThread(void *unused)
{
    (void)unused;
    SetEvent(ready);
    BOOL playing = FALSE;
    for (;;) {
        DWORD wait = WaitForSingleObject(changed, 250);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) break;
        if (InterlockedCompareExchange(&active, 0, 0) && gameplayLoaded()) {
            InterlockedExchange(&active, 0);
            logMessage("Gameplay loaded.");
        }
        BOOL wanted = InterlockedCompareExchange(&active, 0, 0) != 0;
        if (wanted && !playing) {
            if (PlaySoundW(trackPath, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT)) {
                playing = TRUE;
                logMessage("Title music playing.");
            } else logMessage("Title music playback failed.");
        } else if (!wanted && playing) {
            PlaySoundW(NULL, NULL, 0);
            playing = FALSE;
            logMessage("Title music stopped.");
        }
    }
    PlaySoundW(NULL, NULL, 0);
    return 0;
}
static void * __stdcall openMovie(const char *name, uint32_t flags)
{
    void *movie = realOpen(name, flags);
    const char *filename = strrchr(name, '\\');
    if (!filename) filename = strrchr(name, '/');
    filename = filename ? filename + 1 : name;
    if (movie && _stricmp(filename, "frpg_title.bik") == 0) {
        titleMovie = movie;
        // Leave all other movies, including the opening cinematic, untouched.
        soundOnOff(movie, 0);
        InterlockedExchange(&active, 1);
        SetEvent(changed);
        logMessage("Title movie opened.");
    }
    if (movie && _stricmp(filename, "frpg_title.bik") != 0) {
        InterlockedExchange(&active, 0);
        SetEvent(changed);
    }
    return movie;
}

static void __stdcall closeMovie(void *movie)
{
    if (movie == titleMovie) {
        titleMovie = NULL;
        logMessage("Title movie closed.");
    }
    realClose(movie);
}

static void **findImport(const char *name)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; desc->Name; ++desc) {
        if (_stricmp((char *)(base + desc->Name), "binkw64.dll") != 0) continue;
        if (!desc->OriginalFirstThunk) return NULL;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *entry = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)entry->Name, name) == 0) return (void **)&slots->u1.Function;
        }
    }
    return NULL;
}

static BOOL replaceImport(void **slot, void *replacement)
{
    DWORD oldProtect, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) return FALSE;
    InterlockedExchangePointer(slot, replacement);
    return VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
}

// Explicit initialization lets the launcher report a failure before starting the game.
__declspec(dllexport) DWORD WINAPI InitializeQSouls(void *unused)
{
    (void)unused;
    wchar_t root[MAX_PATH];
    DWORD length = GetModuleFileNameW(self, root, MAX_PATH);
    if (!length || length >= MAX_PATH) return ERROR_BUFFER_OVERFLOW;
    wchar_t *slash = wcsrchr(root, L'\\');
    if (!slash) return ERROR_BAD_PATHNAME;
    *slash = 0;
    slash = wcsrchr(root, L'\\');
    if (!slash) return ERROR_BAD_PATHNAME;
    *slash = 0;
    if (swprintf(trackPath, MAX_PATH, L"%ls\\assets\\audio\\title-theme.wav", root) < 0 ||
        swprintf(logPath, MAX_PATH, L"%ls\\.local\\title-music.log", root) < 0)
        return ERROR_BUFFER_OVERFLOW;
    cameraGlobal = findCameraGlobal();
    if (!cameraGlobal) return ERROR_REVISION_MISMATCH;
    void **openSlot = findImport("BinkOpen"), **closeSlot = findImport("BinkClose");
    HMODULE bink = GetModuleHandleW(L"binkw64.dll");
    soundOnOff = (void *)GetProcAddress(bink, "BinkSetSoundOnOff");
    if (!openSlot || !closeSlot || !soundOnOff) return ERROR_PROC_NOT_FOUND;
    realOpen = (void *)*openSlot;
    realClose = (void *)*closeSlot;
    changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!changed || !ready) return GetLastError();
    HANDLE worker = CreateThread(NULL, 0, musicThread, NULL, 0, NULL);
    if (!worker) return GetLastError();
    CloseHandle(worker);
    if (WaitForSingleObject(ready, 15000) != WAIT_OBJECT_0) return ERROR_TIMEOUT;
    CloseHandle(ready);

    if (!replaceImport(openSlot, (void *)openMovie) ||
        !replaceImport(closeSlot, (void *)closeMovie)) return ERROR_WRITE_FAULT;
    logMessage("Title music ready.");
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
