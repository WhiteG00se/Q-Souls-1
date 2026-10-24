#pragma once
#include "RespecModel.h"
#include "NativeCalls.h"

// Only native Level Up call sites are changed. No rendering or input hooks.
static uintptr_t respecDataGlobal, respecWorldGlobal;
static void (*respecNativeCommit)(void *, const int *);
static int (*respecNativePrice)(int);
static void (*respecOriginalAdjust)(void *, unsigned char);
static void (*respecOriginalArrows)(void *, unsigned char *, unsigned char *);
static unsigned char (*respecOriginalChanged)(void *);
static void (*respecOriginalReset)(void *);
static unsigned char (*respecIsLevelUp)(void);
static int (*respecRegister)(int);
static void (*respecSound)(int);
static BOOL (*respecIsLoaded)(void);
static NativeCalls respecHooks[5];
static struct { void *context; uintptr_t owner, character; BYTE identity[48]; BOOL valid; } respecSession;

static BOOL respecRead(uintptr_t address, void *output, size_t size)
{
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), (void *)address, output, size, &read) && read == size;
}
static uintptr_t respecPointer(uintptr_t address)
{
    uintptr_t value = 0;
    return respecRead(address, &value, sizeof(value)) ? value : 0;
}
static BOOL respecWritable(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return FALSE;
    return address >= (uintptr_t)info.BaseAddress && size <= info.RegionSize - (address - (uintptr_t)info.BaseAddress);
}
static BYTE *respecFind(BYTE *image, const BYTE *pattern, const char *mask, size_t n)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt); BYTE *found = NULL;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < n) continue;
        BYTE *data = image + section->VirtualAddress;
        for (size_t i = 0; i <= section->Misc.VirtualSize - n; ++i) {
            size_t j = 0; for (; j < n; ++j) if (mask[j] != '?' && data[i+j] != pattern[j]) break;
            if (j != n) continue;
            if (found) return NULL;
            found = data + i;
        }
    }
    return found;
}
static uintptr_t respecRelative(BYTE *code)
{
    int32_t offset; memcpy(&offset, code + 3, 4); return (uintptr_t)(code + 7) + offset;
}
static DWORD respecResolve(BYTE *image)
{
    // Match the native eight-attribute assignment and its derived-stat refresh tail.
    const BYTE commit[] = {0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0,0,0,0,
        0x4c,0x8b,0xc1,0xc7,0x44,0x24,0x30,0,0,0,0,0x48,0x8b,0x78,0x10,
        0x8b,0x02,0x89,0x47,0x40,0x8b,0x02,0x89,0x47,0x40,0x8b,0x42,0x04,0x89,0x47,0x48};
    const char commitMask[] = "xxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    _Static_assert(sizeof(commitMask) == sizeof(commit) + 1, "Respec commit mask");
    const BYTE tail[] = {0x80,0x8e,0x49,0x09,0,0,0x08,0x48,0x8b,0xce,0xe8};
    BYTE *native = respecFind(image, commit, commitMask, sizeof(commit));
    if (!native || memcmp(native + 0x1c2, tail, sizeof(tail)) ||
        memcmp(native + 0xdb, "\x48\x8b\x05", 3) || memcmp(native + 0x1ee, "\x48\x8b\x74\x24\x48\x48\x83\xc4\x20\x5f\xc3", 11)) return ERROR_REVISION_MISMATCH;
    // Native cost wrapper called by Level Up: ECX is the destination level.
    const BYTE priceSignature[] = {0x40,0x53,0x48,0x83,0xec,0x50,0x8b,0xd9,0x81,0xf9,0x90,0x01,0,0,0x0f,0x8e,0x0c,0x02,0,0,0x81,0xf9,0xbc,0x02,0,0,0x7c,0x5d};
    BYTE *price = respecFind(image, priceSignature, "xxxxxxxxxxxxxxxxxxxxxxxxxxxx", sizeof(priceSignature));
    const BYTE costCall[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x41,0x8b,0xf8,0x8b,0xca,0x8b,0xea,0xe8};
    BYTE *wrapper = respecFind(image, costCall, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", sizeof(costCall));
    int32_t displacement = 0; if (wrapper) memcpy(&displacement, wrapper + 31, 4);
    if (!price || !wrapper || wrapper + 35 + displacement != price || price[0x2f4] != 0xc3) return ERROR_REVISION_MISMATCH;
    respecNativePrice = (void *)price;
    respecDataGlobal = respecRelative(native + 10); respecWorldGlobal = respecRelative(native + 0xdb);
    respecNativeCommit = (void *)native;
    const BYTE adjust[] = {0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x0f,0xb6,0xf2,0xb9,0x55,0,0,0,0xe8};
    const BYTE arrows[] = {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x49,0x8b,0xf8,0xb9,0x55,0,0,0,0x48,0x8b,0xf2,0xe8};
    const BYTE changed[] = {0x8b,0x81,0x4c,0x02,0,0,0x48,0x8b,0xd1,0x39,0x81,0x70,0x02,0,0,0x7f,0x65};
    const BYTE reset[] = {0x40,0x55,0x56,0x48,0x83,0xec,0x28,0x48,0x8b,0x05,0,0,0,0,0x48,0x8b,0xe9,0x48,0x8b,0x70,0x10,0x48,0x85,0xf6};
    BYTE *adjustCode = respecFind(image, adjust, "xxxxxxxxxxxxxxxxxxxx", sizeof(adjust));
    BYTE *arrowCode = respecFind(image, arrows, "xxxxxxxxxxxxxxxxxxxxxxxxxxx", sizeof(arrows));
    BYTE *changedCode = respecFind(image, changed, "xxxxxxxxxxxxxxxxx", sizeof(changed));
    BYTE *resetCode = respecFind(image, reset, "xxxxxxxxxx????xxxxxxxxxx", sizeof(reset));
    if (!adjustCode || !arrowCode || !changedCode || !resetCode ||
        adjustCode[0x38] != 0xe8 || adjustCode[0x1f4] != 0xe8 || resetCode[0x12a] != 0xc3 ||
        respecRelative(resetCode + 7) != respecDataGlobal) return ERROR_REVISION_MISMATCH;
    respecOriginalAdjust = (void *)adjustCode; respecOriginalArrows = (void *)arrowCode;
    respecOriginalChanged = (void *)changedCode; respecOriginalReset = (void *)resetCode;
    int32_t relative;
    memcpy(&relative, adjustCode + 0x14, 4); respecRegister = (void *)(adjustCode + 0x18 + relative);
    memcpy(&relative, adjustCode + 0x39, 4); respecIsLevelUp = (void *)(adjustCode + 0x3d + relative);
    memcpy(&relative, adjustCode + 0x1f5, 4); respecSound = (void *)(adjustCode + 0x1f9 + relative);
    return ERROR_SUCCESS;
}

static BOOL respecBase(uintptr_t owner, int base[8], int *level)
{
    static const size_t offsets[] = {0x40,0x48,0x50,0x58,0x60,0x88,0x68,0x70};
    for (int i = 0; i < 8; ++i) if (!respecRead(owner + offsets[i], &base[i], 4)) return FALSE;
    return respecRead(owner + 0x90, level, 4);
}
static void respecReset(void *context)
{
    respecOriginalReset(context);
    respecSession.context = context;
    respecSession.owner = respecPointer(respecPointer(respecDataGlobal) + 0x10);
    respecSession.character = respecPointer(respecPointer(respecWorldGlobal) + 0x68);
    respecSession.valid = respecSession.owner && respecSession.character &&
        respecRead(respecSession.owner + 0xa0, respecSession.identity, sizeof(respecSession.identity));
}
static BOOL respecDraftFromNative(void *context, RespecDraft *draft)
{
    int original[8], level, souls;
    uintptr_t address = (uintptr_t)context;
    if (!respecRead(address + 0x24c, original, sizeof(original)) || !respecRead(address + 0x298, &level, 4) ||
        !respecRead(address + 0x2a0, &souls, 4) || souls < 0 || !respecBegin(draft, original, level) ||
        !respecRead(address + 0x270, draft->values, sizeof(draft->values))) return FALSE;
    draft->souls = souls; draft->price = respecNativePrice;
    int net = 0;
    for (int i = 0; i < 8; ++i) {
        if (draft->values[i] < RESPEC_MIN || draft->values[i] > RESPEC_MAX) return FALSE;
        net += draft->values[i] - original[i];
    }
    draft->purchased = net > 0 ? net : 0; draft->freePoints = net < 0 ? -net : 0;
    if (level > 713 - draft->purchased) return FALSE;
    for (int i = 1; i <= draft->purchased; ++i) {
        int price = draft->price(level + i);
        if (price <= 0 || draft->cost > INT_MAX - price) return FALSE;
        draft->cost += price;
    }
    return draft->cost <= souls;
}
static BOOL respecSourceUnchanged(void *context, const RespecDraft *draft)
{
    uintptr_t owner = respecPointer(respecPointer(respecDataGlobal) + 0x10);
    uintptr_t character = respecPointer(respecPointer(respecWorldGlobal) + 0x68);
    int base[8], level, souls, hp; BYTE identity[48];
    return respecSession.valid && context == respecSession.context && owner == respecSession.owner &&
        character == respecSession.character && (!respecIsLoaded || respecIsLoaded()) &&
        respecRead(character + 0x3e8, &hp, 4) && hp > 0 && respecBase(owner, base, &level) &&
        level == draft->level && !memcmp(base, draft->original, sizeof(base)) &&
        respecRead(owner + 0x94, &souls, 4) && souls == draft->souls &&
        respecRead(owner + 0xa0, identity, sizeof(identity)) && !memcmp(identity, respecSession.identity, sizeof(identity));
}
static BOOL respecLoadNative(void *context, RespecDraft *draft)
{
    return respecDraftFromNative(context, draft) && respecSourceUnchanged(context, draft);
}
static void respecStoreNative(void *context, const RespecDraft *draft)
{
    BYTE *data = (BYTE *)context;
    int target = draft->level + draft->purchased;
    memcpy(data + 0x270, draft->values, sizeof(draft->values));
    memcpy(data + 0x294, &target, 4); memcpy(data + 0x29c, &draft->cost, 4);
}
static void respecAdjustNative(void *context, unsigned char increase)
{
    if (!respecIsLevelUp()) { respecOriginalAdjust(context, increase); return; }
    RespecDraft draft; int selected = respecRegister(0x55);
    BOOL adjusted = FALSE;
    if (selected >= 0 && selected < 8 && respecLoadNative(context, &draft) && respecWritable((uintptr_t)context, 0x2a4)) {
        draft.selected = selected;
        if (respecAdjust(&draft, increase ? 1 : -1)) { respecStoreNative(context, &draft); adjusted = TRUE; }
    }
    respecSound(adjusted ? 0 : 200); // Existing native adjustment / rejection sounds.
}
static void respecArrowsNative(void *context, unsigned char *decrease, unsigned char *increase)
{
    if (!respecIsLevelUp()) { respecOriginalArrows(context, decrease, increase); return; }
    *decrease = *increase = 0;
    RespecDraft draft; int selected = respecRegister(0x55);
    if (selected < 0 || selected >= 8 || !respecLoadNative(context, &draft)) return;
    draft.selected = selected; RespecDraft copy = draft;
    *decrease = (unsigned char)respecAdjust(&copy, -1);
    copy = draft; *increase = (unsigned char)respecAdjust(&copy, 1);
}
static unsigned char respecChangedNative(void *context)
{
    if (!respecIsLevelUp()) return respecOriginalChanged(context);
    RespecDraft draft;
    return respecLoadNative(context, &draft) && !draft.freePoints &&
        memcmp(draft.original, draft.values, sizeof(draft.values)) != 0;
}
static void respecCommitNative(void *context, const int *stats)
{
    if (!respecIsLevelUp()) { respecNativeCommit(context, stats); return; }
    RespecDraft draft;
    if (!respecLoadNative(context, &draft) || !respecCanCommit(&draft, draft.original, draft.level) ||
        stats != (int *)((BYTE *)context + 0x270) || !respecWritable((uintptr_t)context, 0x2a4)) {
        logMessage("Native Respec confirmation rejected: incomplete or changed character data."); return;
    }
    int humanity, otherHumanity;
    if (!respecWritable(respecSession.owner, 0xd0) || !respecRead(respecSession.owner + 0x84, &humanity, 4) || !respecRead(respecSession.owner + 0x80, &otherHumanity, 4)) return;
    // Recompute final cost, preserve both humanity fields, and let the native routine refresh all derived stats.
    respecStoreNative(context, &draft); memcpy((BYTE *)context + 0x290, &humanity, 4);
    respecNativeCommit(context, stats);
    memcpy((void *)(respecSession.owner + 0x80), &otherHumanity, 4);
    logMessage("Level Up & Respec committed through the native Level Up screen.");
}
static DWORD initializeRespec(BOOL (*loaded)(void))
{
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    DWORD error = respecResolve(image); if (error) return error;
    respecIsLoaded = loaded;
    BYTE *targets[] = {(BYTE *)respecOriginalAdjust, (BYTE *)respecOriginalArrows, (BYTE *)respecOriginalChanged,
        (BYTE *)respecNativeCommit, (BYTE *)respecOriginalReset};
    void *callbacks[] = {(void *)respecAdjustNative, (void *)respecArrowsNative, (void *)respecChangedNative,
        (void *)respecCommitNative, (void *)respecReset};
    const unsigned counts[] = {3,1,3,2,5};
    for (unsigned i = 0; i < 5; ++i) {
        respecHooks[i].target = targets[i]; respecHooks[i].callback = callbacks[i];
        error = nativePrepareCalls(image, &respecHooks[i], counts[i]); if (error) goto failed;
    }
    for (unsigned i = 0; i < 5; ++i) { error = nativeApplyCalls(&respecHooks[i]); if (error) goto failed; }
    logMessage("Native Level Up extended with Respec; original controls, previews and save flow retained.");
    return ERROR_SUCCESS;
failed:
    for (unsigned i = 5; i > 0; --i) {
        DWORD restore = nativeRestoreCalls(&respecHooks[i - 1]); if (restore) return restore;
    }
    return error;
}
