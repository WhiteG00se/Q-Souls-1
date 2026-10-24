#pragma once

// Match the logo dialog update and its normal final-logo completion branch.
// Keep both signatures: skipping an unknown menu routine would be unsafe.
static BOOL introPattern(const BYTE *code, const BYTE *pattern, const char *mask, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        if (mask[i] != '?' && code[i] != pattern[i]) return FALSE;
    return TRUE;
}

static BYTE *findIntroUpdate(BYTE *image, size_t size)
{
    const BYTE update[] = {
        0x40,0x53,0x48,0x83,0xec,0x30,0x0f,0x29,0x74,0x24,0x20,
        0x48,0x8b,0xd9,0x0f,0x28,0xf1,0xe8,0,0,0,0,0x84,0xc0,
        0x0f,0x84,0xc4,0,0,0,0x0f,0x28,0xce,0x48,0x8b,0xcb,0xe8,0,0,0,0
    };
    const char mask[] = "xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????";
    _Static_assert(sizeof(mask) == sizeof(update) + 1, "Intro signature mask length");
    const BYTE finish[] = {
        0x8b,0x87,0x14,0x02,0,0,0x83,0xf8,0x03,0x75,0x0f,
        0x33,0xd2,0x8d,0x4a,0x3c,0xe8
    };
    const BYTE setter[] = {0x81,0xf9,0xf3,0x01,0,0,0x77,0x0e,0x48,0x8b,0x05};
    const size_t finishOffset = 0x332, required = finishOffset + sizeof(finish) + 4;
    BYTE *found = NULL;
    if (size < required) return NULL;
    for (size_t i = 0; i <= size - required; ++i) {
        if (!introPattern(image + i, update, mask, sizeof(update)) ||
            memcmp(image + i + finishOffset, finish, sizeof(finish))) continue;
        int32_t relative;
        memcpy(&relative, image + i + finishOffset + sizeof(finish), sizeof(relative));
        int64_t target = (int64_t)i + (int64_t)required + relative;
        if (target < 0 || (uint64_t)target > size - sizeof(setter) ||
            memcmp(image + target, setter, sizeof(setter))) continue;
        if (found) return NULL;
        found = image + i;
    }
    return found;
}

static DWORD patchIntroUpdate(BYTE *update)
{
    // Tail-call the game's own completion setter (menu event 60, value 0).
    // No synthesized input: the title prompt remains waiting for the player.
    int32_t relative;
    memcpy(&relative, update + 0x343, sizeof(relative));
    BYTE *target = update + 0x347 + relative;
    BYTE patch[] = {0x33,0xd2,0x8d,0x4a,0x3c,0xe9,0,0,0,0,0x90,0x90};
    relative = (int32_t)(target - (update + 10));
    memcpy(patch + 6, &relative, sizeof(relative));
    DWORD previous, ignored;
    if (!VirtualProtect(update, sizeof(patch), PAGE_EXECUTE_READWRITE, &previous)) return GetLastError();
    memcpy(update, patch, sizeof(patch));
    BOOL flushed = FlushInstructionCache(GetCurrentProcess(), update, sizeof(patch));
    DWORD error = flushed ? ERROR_SUCCESS : GetLastError();
    if (!VirtualProtect(update, sizeof(patch), previous, &ignored) && !error) error = GetLastError();
    return error;
}

static DWORD skipIntro(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    BYTE *found = NULL;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *update = findIntroUpdate(base + section->VirtualAddress, section->Misc.VirtualSize);
        if (!update) continue;
        if (found) return ERROR_REVISION_MISMATCH;
        found = update;
    }
    return found ? patchIntroUpdate(found) : ERROR_REVISION_MISMATCH;
}
