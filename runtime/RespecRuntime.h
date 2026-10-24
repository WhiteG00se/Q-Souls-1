#pragma once
#include <xinput.h>
#include "RespecModel.h"

// All editing/commit work runs on the game's window thread, via its message pump.
// The presentation thread only reads a locked snapshot and reports render health.
static uintptr_t respecDataGlobal, respecFlagsGlobal, respecWorldGlobal;
static void (__stdcall *respecNativeCommit)(void *, const int *);
static BOOL (*respecIsLoaded)(void);
static SRWLOCK respecLock = SRWLOCK_INIT;
static RespecDraft respecDraft;
static BOOL respecEditing, respecInvalid;
static volatile LONG respecCapturing;
static HWND respecWindow;
static ULONGLONG respecLastFrame, respecCaptureUntil;
static BOOL respecRenderOK;
static uintptr_t respecOwner, respecCharacter, respecLastOwner, respecLastCharacter, respecLastFlags;
static BYTE respecIdentity[48];
static char respecMessage[160];
static BOOL (WINAPI *respecRealPeek)(LPMSG, HWND, UINT, UINT, UINT);
static SHORT (WINAPI *respecRealKey)(int);
static BOOL (WINAPI *respecRealKeys)(PBYTE);
static DWORD (WINAPI *respecRealXInput)(DWORD, XINPUT_STATE *);
static HRESULT (WINAPI *respecRealDInput)(HINSTANCE, DWORD, const GUID *, void **, void *);

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
    const BYTE flags[] = {0x48,0x8b,0x0d,0,0,0,0,0x99,0x33,0xc2,0x45,0x33,0xc0,0x2b,0xc2,0x8d,0x50,0xf6};
    const char flagsMask[] = "xxx????xxxxxxxxxxx";
    _Static_assert(sizeof(flagsMask) == sizeof(flags) + 1, "Respec flags mask");
    BYTE *native = respecFind(image, commit, commitMask, sizeof(commit));
    BYTE *flagRef = respecFind(image, flags, flagsMask, sizeof(flags));
    if (!native || !flagRef || memcmp(native + 0x1c2, tail, sizeof(tail)) ||
        memcmp(native + 0xdb, "\x48\x8b\x05", 3) || memcmp(native + 0x1ee, "\x48\x8b\x74\x24\x48\x48\x83\xc4\x20\x5f\xc3", 11)) return ERROR_REVISION_MISMATCH;
    respecDataGlobal = respecRelative(native + 10); respecWorldGlobal = respecRelative(native + 0xdb);
    respecFlagsGlobal = respecRelative(flagRef); respecNativeCommit = (void *)native;
    return ERROR_SUCCESS;
}
static volatile LONG *respecFlagWord(void)
{
    uintptr_t flags = respecPointer(respecPointer(respecFlagsGlobal));
    uintptr_t address = flags ? flags + 0x11378 : 0; // group 7, area 000, word for 980..983
    return address && respecWritable(address, sizeof(LONG)) ? (volatile LONG *)address : NULL;
}
static LONG respecFlagMask(int flag) { return (LONG)(0x80000000u >> (flag % 32)); }
static BOOL respecFlag(volatile LONG *word, int flag) { return (InterlockedCompareExchange(word, 0, 0) & respecFlagMask(flag)) != 0; }
static void respecSetFlag(volatile LONG *word, int flag, BOOL value)
{
    if (value) InterlockedOr(word, respecFlagMask(flag)); else InterlockedAnd(word, ~respecFlagMask(flag));
}
static BOOL respecBase(uintptr_t owner, int base[8], int *level)
{
    // PlayerGameData fields used by native Level Up, never the effective ChrIns stats.
    static const size_t offsets[] = {0x40,0x48,0x50,0x58,0x60,0x88,0x68,0x70};
    for (int i = 0; i < 8; ++i) if (!respecRead(owner + offsets[i], &base[i], 4)) return FALSE;
    return respecRead(owner + 0x90, level, 4);
}
static BOOL respecCapture(void)
{
    return InterlockedCompareExchange(&respecCapturing, 0, 0) != 0;
}
static BOOL respecSnapshotText(char *text, size_t size)
{
    AcquireSRWLockShared(&respecLock);
    BOOL show = respecEditing;
    if (show) respecText(&respecDraft, respecMessage, text, size);
    ReleaseSRWLockShared(&respecLock);
    return show;
}
static void respecPresented(HWND window, BOOL success)
{
    AcquireSRWLockExclusive(&respecLock);
    respecWindow = window; respecLastFrame = GetTickCount64(); respecRenderOK = success;
    ReleaseSRWLockExclusive(&respecLock);
}
static SHORT WINAPI respecKey(int key) { return respecCapture() && key != VK_F12 ? 0 : respecRealKey(key); }
static BOOL WINAPI respecKeys(PBYTE keys)
{
    BOOL result = respecRealKeys(keys);
    if (result && keys && respecCapture()) { BYTE f12 = keys[VK_F12]; memset(keys, 0, 256); keys[VK_F12] = f12; }
    return result;
}
static DWORD WINAPI respecXInput(DWORD user, XINPUT_STATE *state)
{
    DWORD result = respecRealXInput(user, state);
    if (!result && state && respecCapture()) memset(&state->Gamepad, 0, sizeof(state->Gamepad));
    return result;
}

typedef struct { void **slot; void *original; } RespecInputHook;
static RespecInputHook respecInputHooks[16]; static size_t respecInputHookCount;
static volatile LONG respecInputOK = 1;
static SRWLOCK respecInputLock = SRWLOCK_INIT;
static BOOL respecHookInput(void **slot, void *replacement)
{
    AcquireSRWLockExclusive(&respecInputLock);
    for (size_t i = 0; i < respecInputHookCount; ++i) if (respecInputHooks[i].slot == slot) { ReleaseSRWLockExclusive(&respecInputLock); return TRUE; }
    BOOL result = respecInputHookCount < 16;
    if (result) {
        void *previous = *slot;
        result = replaceImport(slot, replacement);
        if (result) { respecInputHooks[respecInputHookCount].slot = slot; respecInputHooks[respecInputHookCount++].original = previous; }
    }
    ReleaseSRWLockExclusive(&respecInputLock); return result;
}
static void *respecInputOriginal(void *object, size_t index)
{
    void **slot = *(void ***)object + index; void *result = NULL;
    AcquireSRWLockShared(&respecInputLock);
    for (size_t i = 0; i < respecInputHookCount; ++i) if (respecInputHooks[i].slot == slot) { result = respecInputHooks[i].original; break; }
    ReleaseSRWLockShared(&respecInputLock); return result;
}
static HRESULT __stdcall respecDeviceState(void *device, DWORD size, void *state)
{
    HRESULT (__stdcall *next)(void *, DWORD, void *) = (void *)respecInputOriginal(device, 9);
    if (!next) return E_FAIL;
    HRESULT result = next(device, size, state);
    if (SUCCEEDED(result) && state && respecCapture()) {
        BYTE f12 = size == 256 ? ((BYTE *)state)[0x58] : 0;
        memset(state, 0, size); if (size == 256) ((BYTE *)state)[0x58] = f12;
    }
    return result;
}
static HRESULT __stdcall respecDeviceData(void *device, DWORD size, void *data, DWORD *count, DWORD flags)
{
    HRESULT (__stdcall *next)(void *, DWORD, void *, DWORD *, DWORD) = (void *)respecInputOriginal(device, 10);
    if (!next) return E_FAIL;
    HRESULT result = next(device, size, data, count, flags);
    if (SUCCEEDED(result) && data && count && respecCapture()) {
        DWORD keep = 0;
        if (size >= 16 && size <= 64 && *count <= 4096) {
            for (DWORD i = 0; i < *count; ++i) {
                BYTE *event = (BYTE *)data + (size_t)i * size; DWORD offset;
                memcpy(&offset, event, 4);
                if (offset == 0x58) { memmove((BYTE *)data + (size_t)keep * size, event, size); ++keep; }
            }
        }
        *count = keep;
    }
    return result;
}
static HRESULT __stdcall respecCreateDevice(void *input, const GUID *guid, void **device, void *outer)
{
    HRESULT (__stdcall *next)(void *, const GUID *, void **, void *) = (void *)respecInputOriginal(input, 3);
    if (!next) return E_FAIL;
    HRESULT result = next(input, guid, device, outer);
    if (SUCCEEDED(result) && device && *device) {
        if (!respecHookInput(*(void ***)*device + 9, (void *)respecDeviceState) ||
            !respecHookInput(*(void ***)*device + 10, (void *)respecDeviceData)) { InterlockedExchange(&respecInputOK, 0); logMessage("Respec input capture could not be installed."); }
    }
    return result;
}
static HRESULT WINAPI respecCreateInput(HINSTANCE instance, DWORD version, const GUID *iid, void **input, void *outer)
{
    HRESULT result = respecRealDInput(instance, version, iid, input, outer);
    if (SUCCEEDED(result) && input && *input && !respecHookInput(*(void ***)*input + 3, (void *)respecCreateDevice)) {
        InterlockedExchange(&respecInputOK, 0); logMessage("Respec DirectInput capture could not be installed.");
    }
    return result;
}
static unsigned respecButtons(void)
{
    unsigned buttons = 0;
    if (respecRealKey(VK_UP) & 0x8000) buttons |= 1;
    if (respecRealKey(VK_DOWN) & 0x8000) buttons |= 2;
    if (respecRealKey(VK_LEFT) & 0x8000) buttons |= 4;
    if (respecRealKey(VK_RIGHT) & 0x8000) buttons |= 8;
    if ((respecRealKey(VK_ESCAPE) | respecRealKey(VK_BACK) | respecRealKey('Q')) & 0x8000) buttons |= 16;
    for (DWORD user = 0; user < 4; ++user) {
        XINPUT_STATE state;
        if (respecRealXInput(user, &state)) continue;
        WORD b = state.Gamepad.wButtons;
        if ((b & XINPUT_GAMEPAD_DPAD_UP) || state.Gamepad.sThumbLY > 20000) buttons |= 1;
        if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || state.Gamepad.sThumbLY < -20000) buttons |= 2;
        if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || state.Gamepad.sThumbLX < -20000) buttons |= 4;
        if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || state.Gamepad.sThumbLX > 20000) buttons |= 8;
        if (b & XINPUT_GAMEPAD_B) buttons |= 16;
    }
    return buttons;
}
static void respecEnd(ULONGLONG now)
{
    respecEditing = FALSE; respecInvalid = FALSE; respecCaptureUntil = now + 150;
}
static BOOL respecApply(uintptr_t owner, uintptr_t character)
{
    int base[8], level, souls, humanity, otherHumanity;
    BYTE identity[sizeof(respecIdentity)];
    if (owner != respecOwner || character != respecCharacter || !respecBase(owner, base, &level) ||
        !respecRead(owner + 0xa0, identity, sizeof(identity)) || memcmp(identity, respecIdentity, sizeof(identity)) ||
        !respecCanCommit(&respecDraft, base, level) || !respecRead(owner + 0x94, &souls, 4) ||
        !respecRead(owner + 0x84, &humanity, 4) || !respecRead(owner + 0x80, &otherHumanity, 4) ||
        !respecWritable(owner, 0xd0)) return FALSE;
    if (!memcmp(base, respecDraft.values, sizeof(base))) return TRUE;
    // Native Level Up consumes a small context and nine stats. A zero cost and
    // unchanged level preserve currency; the normal routine refreshes HP/stamina,
    // attunement, equipment requirements and derived defenses after assignment.
    BYTE context[0x2a8] = {0}; int values[9];
    memcpy(context + 0x294, &level, 4); memcpy(context + 0x2a0, &souls, 4);
    memcpy(values, respecDraft.values, sizeof(base)); values[8] = humanity;
    respecNativeCommit(context, values);
    memcpy((void *)(owner + 0x80), &otherHumanity, 4);
    logMessage("Respec applied through native Level Up; Soul Level and souls preserved.");
    return TRUE;
}
static void respecTick(void)
{
    static ULONGLONG lastTick, repeatAt; static unsigned previousButtons;
    ULONGLONG now = GetTickCount64();
    if (now - lastTick < 10) return;
    AcquireSRWLockExclusive(&respecLock);
    if (!respecWindow || GetWindowThreadProcessId(respecWindow, NULL) != GetCurrentThreadId()) { ReleaseSRWLockExclusive(&respecLock); return; }
    lastTick = now;
    volatile LONG *word = respecFlagWord();
    uintptr_t owner = respecPointer(respecPointer(respecDataGlobal) + 0x10);
    uintptr_t character = respecPointer(respecPointer(respecWorldGlobal) + 0x68);
    int hp = 0;
    BOOL alive = owner && character && (!respecIsLoaded || respecIsLoaded()) && respecRead(character + 0x3e8, &hp, 4) && hp > 0;
    BOOL rendering = respecRenderOK && now - respecLastFrame < 3000 && InterlockedCompareExchange(&respecInputOK, 0, 0);
    if (word && ((uintptr_t)word != respecLastFlags || owner != respecLastOwner || character != respecLastCharacter)) {
        respecSetFlag(word, RESPEC_REQUEST, FALSE); respecSetFlag(word, RESPEC_DONE, FALSE); respecSetFlag(word, RESPEC_COMMITTED, FALSE);
        respecLastFlags = (uintptr_t)word; respecLastOwner = owner; respecLastCharacter = character;
        if (respecEditing) respecEnd(now);
    }
    if (word) respecSetFlag(word, RESPEC_READY, rendering && alive);
    BOOL request = word && respecFlag(word, RESPEC_REQUEST);
    if (respecEditing && (!request || !alive || owner != respecOwner || character != respecCharacter ||
        (!rendering && GetForegroundWindow() == respecWindow))) {
        respecEnd(now);
        if (word && request) { respecSetFlag(word, RESPEC_COMMITTED, FALSE); respecSetFlag(word, RESPEC_DONE, TRUE); }
    }
    if (!respecEditing && request && !respecFlag(word, RESPEC_DONE) && alive && rendering) {
        int base[8], level;
        if (respecBase(owner, base, &level) && respecRead(owner + 0xa0, respecIdentity, sizeof(respecIdentity))) {
            respecInvalid = !respecBegin(&respecDraft, base, level);
            if (respecInvalid) { memset(&respecDraft, 0, sizeof(respecDraft)); memcpy(respecDraft.values, base, sizeof(base)); respecDraft.level = level; }
            snprintf(respecMessage, sizeof(respecMessage), "%s", respecInvalid ? "Respec requires base stats between 10 and 99. Press Back to return." : "");
            respecOwner = owner; respecCharacter = character; respecEditing = TRUE;
            previousButtons = respecButtons(); repeatAt = now + 350;
        } else { respecSetFlag(word, RESPEC_DONE, TRUE); }
    }
    if (respecEditing && GetForegroundWindow() == respecWindow) {
        unsigned buttons = respecButtons(), pressed = buttons & ~previousButtons;
        if ((buttons & 15) && now >= repeatAt) { pressed |= buttons & 15; repeatAt = now + 100; }
        if (buttons != previousButtons) repeatAt = now + 350;
        if (!respecInvalid) {
            if ((pressed & 3) == 1) respecMove(&respecDraft, -1);
            if ((pressed & 3) == 2) respecMove(&respecDraft, 1);
            if ((pressed & 12) == 4) { respecAdjust(&respecDraft, -1); respecMessage[0] = 0; }
            if ((pressed & 12) == 8) { respecAdjust(&respecDraft, 1); respecMessage[0] = 0; }
        }
        if (pressed & 16) {
            if (respecInvalid) { respecSetFlag(word, RESPEC_DONE, TRUE); respecEnd(now); }
            else if (respecDraft.freePoints) snprintf(respecMessage, sizeof(respecMessage), "Spend all Free Points before leaving.");
            else {
                BOOL applied = respecApply(owner, character);
                respecSetFlag(word, RESPEC_COMMITTED, applied); respecSetFlag(word, RESPEC_DONE, TRUE); respecEnd(now);
                if (!applied) logMessage("Respec draft discarded: the source character or base stats changed.");
            }
        }
        previousButtons = buttons;
    }
    InterlockedExchange(&respecCapturing, respecEditing || now < respecCaptureUntil);
    ReleaseSRWLockExclusive(&respecLock);
}
static BOOL WINAPI respecPeek(LPMSG message, HWND window, UINT min, UINT max, UINT remove)
{
    respecTick();
    BOOL result = respecRealPeek(message, window, min, max, remove);
    if (result && message && respecCapture() &&
        ((message->message >= WM_KEYFIRST && message->message <= WM_KEYLAST && message->wParam != VK_F12) ||
         (message->message >= WM_MOUSEFIRST && message->message <= WM_MOUSELAST) || message->message == WM_INPUT)) {
        // DefWindowProc must still release foreground raw-input packets.
        if (message->message == WM_INPUT && (remove & PM_REMOVE)) DefWindowProcW(message->hwnd, message->message, message->wParam, message->lParam);
        message->message = WM_NULL;
    }
    return result;
}
static void **respecXInputSlot(void)
{
    BYTE *image = (BYTE *)GetModuleHandleW(NULL); IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(image + rva);
    for (; desc->Name; ++desc) {
        if (_stricmp((char *)image + desc->Name, "xinput1_3.dll") || !desc->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(image + desc->OriginalFirstThunk), *slots = (IMAGE_THUNK_DATA64 *)(image + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal) && IMAGE_ORDINAL64(names->u1.Ordinal) == 2) return (void **)&slots->u1.Function;
    }
    return NULL;
}
static DWORD initializeRespec(BOOL (*loaded)(void))
{
    DWORD error = respecResolve((BYTE *)GetModuleHandleW(NULL));
    if (error) return error;
    respecIsLoaded = loaded;
    void **peek = findImport("user32.dll", "PeekMessageW"), **key = findImport("user32.dll", "GetAsyncKeyState"),
         **keys = findImport("user32.dll", "GetKeyboardState"), **input = findImport("dinput8.dll", "DirectInput8Create"), **pad = respecXInputSlot();
    if (!peek || !key || !keys || !input || !pad) return ERROR_PROC_NOT_FOUND;
    respecRealPeek = (void *)*peek; respecRealKey = (void *)*key; respecRealKeys = (void *)*keys;
    respecRealDInput = (void *)*input; respecRealXInput = (void *)*pad;
    if (!replaceImport(peek, (void *)respecPeek) || !replaceImport(key, (void *)respecKey) ||
        !replaceImport(keys, (void *)respecKeys) || !replaceImport(input, (void *)respecCreateInput) ||
        !replaceImport(pad, (void *)respecXInput)) return ERROR_WRITE_FAULT;
    setRespecOverlayCallbacks(respecSnapshotText, respecPresented);
    logMessage("Bonfire Respec ready; base-stat drafts commit through native Level Up.");
    return ERROR_SUCCESS;
}
