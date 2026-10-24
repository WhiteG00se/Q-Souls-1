#pragma once

// The game's own Present call, after its render-context flush and before its
// backbuffer-index update. DXGI methods and RTSS's probe are never modified.
static inline void presentationCandidates(BYTE *data, size_t size, BYTE **found, unsigned *count)
{
    const BYTE pattern[] = {
        0x48,0x8b,0x8d,0xd8,0x01,0,0,0xe8,0,0,0,0,0x84,0xdb,0x75,0x25,
        0x48,0x8b,0x4d,0x30,0x45,0x33,0xc0,0x0f,0xb6,0x95,0xc0,0x01,0,0,
        0x48,0x8b,0x01,0xff,0x50,0x40,0x8b,0x85,0x70,0x01,0,0,0xff,0xc8,
        0x83,0xe0,0x01,0x89,0x85,0x70,0x01,0,0
    };
    if (size < sizeof(pattern)) return;
    for (size_t i = 0; i <= size - sizeof(pattern); ++i) {
        size_t j = 0;
        for (; j < sizeof(pattern); ++j) if ((j < 8 || j >= 12) && data[i+j] != pattern[j]) break;
        if (j == sizeof(pattern)) { *found = data + i + 30; ++*count; }
    }
}
static inline BYTE *findGamePresentation(BYTE *image)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    BYTE *found = NULL; unsigned count = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            presentationCandidates(image + section->VirtualAddress, section->Misc.VirtualSize, &found, &count);
    return count == 1 ? found : NULL;
}
static inline void **allocatePresentationSlot(BYTE *site)
{
    SYSTEM_INFO info; GetSystemInfo(&info);
    uintptr_t step = info.dwAllocationGranularity;
    uintptr_t anchor = (uintptr_t)site & ~(step - 1);
    uintptr_t minimum = (uintptr_t)info.lpMinimumApplicationAddress, maximum = (uintptr_t)info.lpMaximumApplicationAddress;
    for (uintptr_t distance = 0; distance < 0x7fff0000u; distance += step) {
        uintptr_t candidates[2] = {anchor >= distance ? anchor - distance : 0, anchor <= maximum - distance ? anchor + distance : 0};
        for (int i = 0; i < 2; ++i) {
            uintptr_t address = candidates[i];
            int64_t relative = (int64_t)address - (int64_t)((uintptr_t)site + 6);
            if (address < minimum || address > maximum - info.dwPageSize || relative < INT32_MIN || relative > INT32_MAX) continue;
            void *slot = VirtualAlloc((void *)address, info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (slot) return (void **)slot;
        }
    }
    return NULL;
}
static inline DWORD patchGamePresentation(BYTE *site, void *callback, void ***allocation)
{
    const BYTE expected[] = {0x48,0x8b,0x01,0xff,0x50,0x40};
    if (!site || !callback || !allocation || memcmp(site, expected, sizeof(expected)) || site[-23] != 0xe8) return ERROR_REVISION_MISMATCH;
    if (*allocation) return ERROR_ALREADY_INITIALIZED;
    BYTE *call = site - 23;
    int32_t relative; memcpy(&relative, call + 1, 4);
    void *flush = call + 5 + relative;
    BYTE *bridge = (BYTE *)allocatePresentationSlot(call);
    if (!bridge) return ERROR_NOT_ENOUGH_MEMORY;
    // Preserve the real game's Present instruction and its original return
    // address. Draw after its context flush, then return before Present runs.
    BYTE code[] = {0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,
        0x84,0xdb,0x75,0x10,0x48,0x8b,0x4d,0x30,0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    memcpy(code + 6, &flush, 8); memcpy(code + 26, &callback, 8);
    memcpy(bridge, code, sizeof(code));
    // Valid x64 unwind metadata for the bridge's 40-byte shadow-space allocation.
    const BYTE unwind[] = {1,4,1,0,4,0x42,0,0};
    memcpy(bridge + 48, unwind, sizeof(unwind));
    RUNTIME_FUNCTION *function = (RUNTIME_FUNCTION *)(bridge + 64);
    function->BeginAddress = 0; function->EndAddress = sizeof(code); function->UnwindData = 48;
    if (!RtlAddFunctionTable(function, 1, (DWORD64)bridge)) { VirtualFree(bridge, 0, MEM_RELEASE); return ERROR_INVALID_FUNCTION; }
    DWORD previous, ignored;
    if (!VirtualProtect(bridge, 0x1000, PAGE_EXECUTE_READ, &previous)) {
        DWORD error = GetLastError(); RtlDeleteFunctionTable(function); VirtualFree(bridge, 0, MEM_RELEASE); return error;
    }
    BYTE patch[] = {0xe8,0,0,0,0};
    relative = (int32_t)((intptr_t)bridge - ((intptr_t)call + 5)); memcpy(patch + 1, &relative, 4);
    if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &previous)) {
        DWORD error = GetLastError(); RtlDeleteFunctionTable(function); VirtualFree(bridge, 0, MEM_RELEASE); return error;
    }
    memcpy(call, patch, sizeof(patch));
    DWORD error = FlushInstructionCache(GetCurrentProcess(), bridge, sizeof(code)) && FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch)) ? ERROR_SUCCESS : GetLastError();
    if (!VirtualProtect(call, sizeof(patch), previous, &ignored) && !error) error = GetLastError();
    *allocation = (void **)bridge;
    return error;
}
