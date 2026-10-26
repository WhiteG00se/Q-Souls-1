#pragma once
#include <xinput.h>
// Input capture from the original Respec panel. All hooks pass through when closed.
BOOL qualityCapture(void);
void qualityTick(void);
static BOOL (WINAPI *qualityRealPeek)(LPMSG, HWND, UINT, UINT, UINT);
static SHORT (WINAPI *qualityRealKey)(int);
static BOOL (WINAPI *qualityRealKeys)(PBYTE);
static DWORD (WINAPI *qualityRealXInput)(DWORD, XINPUT_STATE *);
static HRESULT (WINAPI *qualityRealDInput)(HINSTANCE, DWORD, const GUID *, void **, void *);
static SHORT WINAPI qualityKey(int key) { return qualityCapture() && key != VK_F12 ? 0 : qualityRealKey(key); }
static BOOL WINAPI qualityKeys(PBYTE keys)
{
    BOOL result = qualityRealKeys(keys);
    if (result && keys && qualityCapture()) { BYTE f12 = keys[VK_F12]; memset(keys, 0, 256); keys[VK_F12] = f12; }
    return result;
}
static DWORD WINAPI qualityXInput(DWORD user, XINPUT_STATE *state)
{
    DWORD result = qualityRealXInput(user, state);
    if (!result && state && qualityCapture()) {
        SHORT cameraX=state->Gamepad.sThumbRX, cameraY=state->Gamepad.sThumbRY;
        memset(&state->Gamepad,0,sizeof(state->Gamepad));
        state->Gamepad.sThumbRX=cameraX; state->Gamepad.sThumbRY=cameraY;
    }
    return result;
}

typedef struct { void **slot; void *original; } QualityInputHook;
static QualityInputHook qualityInputHooks[16]; static size_t qualityInputHookCount;
static volatile LONG qualityInputOK = 1;
static SRWLOCK qualityInputLock = SRWLOCK_INIT;
static BOOL qualityHookInput(void **slot, void *replacement)
{
    AcquireSRWLockExclusive(&qualityInputLock);
    for (size_t i = 0; i < qualityInputHookCount; ++i) if (qualityInputHooks[i].slot == slot) { ReleaseSRWLockExclusive(&qualityInputLock); return TRUE; }
    BOOL result = qualityInputHookCount < 16;
    if (result) {
        void *previous = *slot;
        result = replaceImport(slot, replacement);
        if (result) { qualityInputHooks[qualityInputHookCount].slot = slot; qualityInputHooks[qualityInputHookCount++].original = previous; }
    }
    ReleaseSRWLockExclusive(&qualityInputLock); return result;
}
static void *qualityInputOriginal(void *object, size_t index)
{
    void **slot = *(void ***)object + index; void *result = NULL;
    AcquireSRWLockShared(&qualityInputLock);
    for (size_t i = 0; i < qualityInputHookCount; ++i) if (qualityInputHooks[i].slot == slot) { result = qualityInputHooks[i].original; break; }
    ReleaseSRWLockShared(&qualityInputLock); return result;
}
static HRESULT __stdcall qualityDeviceState(void *device, DWORD size, void *state)
{
    HRESULT (__stdcall *next)(void *, DWORD, void *) = (void *)qualityInputOriginal(device, 9);
    if (!next) return E_FAIL;
    HRESULT result = next(device, size, state);
    if (SUCCEEDED(result) && state && qualityCapture()) {
        BYTE f12 = size == 256 ? ((BYTE *)state)[0x58] : 0;
        memset(state, 0, size); if (size == 256) ((BYTE *)state)[0x58] = f12;
    }
    return result;
}
static HRESULT __stdcall qualityDeviceData(void *device, DWORD size, void *data, DWORD *count, DWORD flags)
{
    HRESULT (__stdcall *next)(void *, DWORD, void *, DWORD *, DWORD) = (void *)qualityInputOriginal(device, 10);
    if (!next) return E_FAIL;
    HRESULT result = next(device, size, data, count, flags);
    if (SUCCEEDED(result) && data && count && qualityCapture()) {
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
static HRESULT __stdcall qualityCreateDevice(void *input, const GUID *guid, void **device, void *outer)
{
    HRESULT (__stdcall *next)(void *, const GUID *, void **, void *) = (void *)qualityInputOriginal(input, 3);
    if (!next) return E_FAIL;
    HRESULT result = next(input, guid, device, outer);
    if (SUCCEEDED(result) && device && *device) {
        if (!qualityHookInput(*(void ***)*device + 9, (void *)qualityDeviceState) ||
            !qualityHookInput(*(void ***)*device + 10, (void *)qualityDeviceData)) { InterlockedExchange(&qualityInputOK, 0); logMessage("Q Souls input capture could not be installed."); }
    }
    return result;
}
static HRESULT WINAPI qualityCreateInput(HINSTANCE instance, DWORD version, const GUID *iid, void **input, void *outer)
{
    HRESULT result = qualityRealDInput(instance, version, iid, input, outer);
    if (SUCCEEDED(result) && input && *input && !qualityHookInput(*(void ***)*input + 3, (void *)qualityCreateDevice)) {
        InterlockedExchange(&qualityInputOK, 0); logMessage("Q Souls DirectInput capture could not be installed.");
    }
    return result;
}
unsigned qualityButtons(void)
{
    unsigned buttons = 0;
    if (qualityRealKey(VK_UP) & 0x8000) buttons |= 1;
    if (qualityRealKey(VK_DOWN) & 0x8000) buttons |= 2;
    if (qualityRealKey(VK_LEFT) & 0x8000) buttons |= 4;
    if (qualityRealKey(VK_RIGHT) & 0x8000) buttons |= 8;
    if ((qualityRealKey(VK_ESCAPE) | qualityRealKey(VK_BACK) | qualityRealKey('Q')) & 0x8000) buttons |= 16;
    for (DWORD user = 0; user < 4; ++user) {
        XINPUT_STATE state;
        if (qualityRealXInput(user, &state)) continue;
        WORD b = state.Gamepad.wButtons;
        if ((b & XINPUT_GAMEPAD_DPAD_UP) || state.Gamepad.sThumbLY > 20000) buttons |= 1;
        if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || state.Gamepad.sThumbLY < -20000) buttons |= 2;
        if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || state.Gamepad.sThumbLX < -20000) buttons |= 4;
        if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || state.Gamepad.sThumbLX > 20000) buttons |= 8;
        if (b & XINPUT_GAMEPAD_B) buttons |= 16;
    }
    return buttons;
}
static BOOL WINAPI qualityPeek(LPMSG message, HWND window, UINT min, UINT max, UINT remove)
{
    qualityTick();
    BOOL result = qualityRealPeek(message, window, min, max, remove);
    if (result && message && qualityCapture() &&
        ((message->message >= WM_KEYFIRST && message->message <= WM_KEYLAST && message->wParam != VK_F12) ||
         (message->message >= WM_MOUSEFIRST && message->message <= WM_MOUSELAST) || message->message == WM_INPUT)) {
        // DefWindowProc must still release foreground raw-input packets.
        if (message->message == WM_INPUT && (remove & PM_REMOVE)) DefWindowProcW(message->hwnd, message->message, message->wParam, message->lParam);
        message->message = WM_NULL;
    }
    return result;
}
static void **qualityXInputSlot(void)
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

BOOL qualityInputHealthy(void) { return InterlockedCompareExchange(&qualityInputOK,0,0) != 0; }
DWORD initializeQualityInput(void)
{
    void **slots[] = {findImport("user32.dll","PeekMessageW"),findImport("user32.dll","GetAsyncKeyState"),
        findImport("user32.dll","GetKeyboardState"),findImport("dinput8.dll","DirectInput8Create"),qualityXInputSlot()};
    for (unsigned i=0;i<5;++i) if (!slots[i]) return ERROR_PROC_NOT_FOUND;
    qualityRealPeek=(void *)*slots[0]; qualityRealKey=(void *)*slots[1]; qualityRealKeys=(void *)*slots[2];
    qualityRealDInput=(void *)*slots[3]; qualityRealXInput=(void *)*slots[4];
    void *hooks[]={(void *)qualityPeek,(void *)qualityKey,(void *)qualityKeys,(void *)qualityCreateInput,(void *)qualityXInput};
    for (unsigned i=0;i<5;++i) if (!replaceImport(slots[i],hooks[i])) {
        void *originals[]={(void *)qualityRealPeek,(void *)qualityRealKey,(void *)qualityRealKeys,(void *)qualityRealDInput,(void *)qualityRealXInput};
        for (unsigned j=0;j<=i;++j) replaceImport(slots[j],originals[j]);
        return ERROR_WRITE_FAULT;
    }
    return ERROR_SUCCESS;
}
