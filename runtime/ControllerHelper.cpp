// Optional input recovery. No window activation, synthetic input, or device resets.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <stdio.h>
#include <wchar.h>
#include "ControllerHelper.h"

namespace {
SRWLOCK objectLock = SRWLOCK_INIT, logLock = SRWLOCK_INIT;
wchar_t helperLog[MAX_PATH];
volatile LONG foreground = 0, focusEpoch = 0, enabledEpoch = -1;
DWORD (WINAPI *nextState)(DWORD, XINPUT_STATE *);
void (WINAPI *enableInput)(BOOL);
HRESULT (WINAPI *nextCreate)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);

void logResult(const char *action, HRESULT result) {
    if (!helperLog[0]) return;
    char line[256];
    int n = snprintf(line, sizeof(line), "%llu %s: 0x%08lx\r\n",
        GetTickCount64(), action, (unsigned long)result);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    AcquireSRWLockExclusive(&logLock);
    HANDLE file = CreateFileW(helperLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, line, (DWORD)n, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&logLock);
}
BOOL gameForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
void observeFocus(BOOL active) {
    // Serialize transitions so an old observation cannot overwrite a newer epoch.
    AcquireSRWLockExclusive(&objectLock);
    if (active && !foreground) InterlockedIncrement(&focusEpoch);
    InterlockedExchange(&foreground, active);
    ReleaseSRWLockExclusive(&objectLock);
}
DWORD WINAPI focusObserver(void *) {
    for (;;) { observeFocus(gameForeground()); Sleep(50); }
    return 0;
}
DWORD WINAPI controllerState(DWORD index, XINPUT_STATE *state) {
    BOOL active = gameForeground();
    observeFocus(active);
    LONG epoch = InterlockedCompareExchange(&focusEpoch, 0, 0);
    if (active && enableInput) {
        LONG previous = InterlockedCompareExchange(&enabledEpoch, 0, 0);
        if (previous != epoch && InterlockedCompareExchange(&enabledEpoch, epoch, previous) == previous) {
            enableInput(TRUE);
            logResult("XInput foreground reporting enabled", S_OK);
        }
    }
    return nextState(index, state);
}

// Shadow each interface's vtable, retaining the original object, identity, and refcounts.
// Unmodified methods (including AddRef) remain the existing implementation's pointers.
struct Object {
    Object *next;
    void *instance;
    bool device;
    void *original[32], *shadow[32];
    ULONGLONG nextAttempt, nextLog;
};
Object *objects;
Object *lookup(void *instance) {
    for (Object *p = objects; p; p = p->next) if (p->instance == instance) return p;
    return nullptr;
}
Object *lookupLocked(void *instance) {
    AcquireSRWLockShared(&objectLock);
    Object *p = lookup(instance);
    ReleaseSRWLockShared(&objectLock);
    return p; // A caller must own a COM reference for the duration of the call.
}
bool attach(void *instance, bool device);
HRESULT WINAPI query(void *instance, REFIID iid, void **output) {
    Object *p = lookupLocked(instance);
    auto original = reinterpret_cast<HRESULT (WINAPI *)(void *, REFIID, void **)>(p->original[0]);
    HRESULT hr = original(instance, iid, output);
    bool known = p->device
        ? (IsEqualIID(iid, IID_IDirectInputDevice8A) || IsEqualIID(iid, IID_IDirectInputDevice8W))
        : (IsEqualIID(iid, IID_IDirectInput8A) || IsEqualIID(iid, IID_IDirectInput8W));
    if (SUCCEEDED(hr) && output && *output && known) attach(*output, p->device);
    return hr;
}
ULONG WINAPI release(void *instance) {
    Object *p = lookupLocked(instance);
    auto original = reinterpret_cast<ULONG (WINAPI *)(void *)>(p->original[2]);
    ULONG count = original(instance);
    if (!count) {
        AcquireSRWLockExclusive(&objectLock);
        Object **link = &objects;
        while (*link && *link != p) link = &(*link)->next;
        if (*link) *link = p->next;
        ReleaseSRWLockExclusive(&objectLock);
        HeapFree(GetProcessHeap(), 0, p);
    }
    return count;
}
bool recover(Object *p, HRESULT hr, bool &report) {
    report = false;
    if ((hr != DIERR_INPUTLOST && hr != DIERR_NOTACQUIRED) || !gameForeground()) return false;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&objectLock);
    bool attempt = now >= p->nextAttempt;
    report = now >= p->nextLog;
    if (attempt) p->nextAttempt = now + 1000;
    if (attempt && report) p->nextLog = now + 30000;
    ReleaseSRWLockExclusive(&objectLock);
    if (!attempt) return false;
    auto acquire = reinterpret_cast<HRESULT (WINAPI *)(void *)>(p->original[7]);
    HRESULT acquired = acquire(p->instance);
    if (report) {
        logResult("DirectInput lost/unacquired", hr);
        logResult("DirectInput Acquire", acquired);
    }
    return SUCCEEDED(acquired);
}
void reportRetry(HRESULT hr, bool report) {
    // Failed attempts are already throttled by recover; log successful recoveries too.
    if (report || SUCCEEDED(hr)) logResult("DirectInput read retry", hr);
}
HRESULT WINAPI readState(void *instance, DWORD size, void *data) {
    Object *p = lookupLocked(instance);
    auto read = reinterpret_cast<HRESULT (WINAPI *)(void *, DWORD, void *)>(p->original[9]);
    HRESULT hr = read(instance, size, data);
    bool report;
    if (recover(p, hr, report)) { hr = read(instance, size, data); reportRetry(hr, report); }
    return hr;
}
HRESULT WINAPI readData(void *instance, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count, DWORD flags) {
    Object *p = lookupLocked(instance);
    auto read = reinterpret_cast<HRESULT (WINAPI *)(void *, DWORD, DIDEVICEOBJECTDATA *, DWORD *, DWORD)>(p->original[10]);
    DWORD requested = count ? *count : 0;
    HRESULT hr = read(instance, size, data, count, flags);
    bool report;
    if (recover(p, hr, report)) {
        if (count) *count = requested;
        hr = read(instance, size, data, count, flags); reportRetry(hr, report);
    }
    return hr;
}
HRESULT WINAPI poll(void *instance) {
    Object *p = lookupLocked(instance);
    auto read = reinterpret_cast<HRESULT (WINAPI *)(void *)>(p->original[25]);
    HRESULT hr = read(instance);
    bool report;
    if (recover(p, hr, report)) { hr = read(instance); reportRetry(hr, report); }
    return hr;
}
HRESULT WINAPI createDevice(void *instance, REFGUID guid, void **output, IUnknown *outer) {
    Object *p = lookupLocked(instance);
    auto create = reinterpret_cast<HRESULT (WINAPI *)(void *, REFGUID, void **, IUnknown *)>(p->original[3]);
    HRESULT hr = create(instance, guid, output, outer);
    if (SUCCEEDED(hr) && output && *output && !outer) {
        DIDEVCAPS caps = {}; caps.dwSize = sizeof(caps);
        void **table = *reinterpret_cast<void ***>(*output);
        auto getCaps = reinterpret_cast<HRESULT (WINAPI *)(void *, DIDEVCAPS *)>(table[3]);
        if (SUCCEEDED(getCaps(*output, &caps))) {
            DWORD type = GET_DIDEVICE_TYPE(caps.dwDevType);
            if (type >= DI8DEVTYPE_JOYSTICK && type <= DI8DEVTYPE_SUPPLEMENTAL) attach(*output, true);
        } else logResult("DirectInput device classification unavailable", E_FAIL);
    }
    return hr;
}
bool attach(void *instance, bool device) {
    AcquireSRWLockExclusive(&objectLock);
    if (lookup(instance)) { ReleaseSRWLockExclusive(&objectLock); return true; }
    Object *p = static_cast<Object *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Object)));
    if (!p) { ReleaseSRWLockExclusive(&objectLock); logResult("DirectInput hook allocation failed", E_OUTOFMEMORY); return false; }
    p->instance = instance; p->device = device;
    void **table = *reinterpret_cast<void ***>(instance);
    size_t bytes = (device ? 32 : 11) * sizeof(void *);
    memcpy(p->original, table, bytes); memcpy(p->shadow, table, bytes);
    p->shadow[0] = reinterpret_cast<void *>(query); p->shadow[2] = reinterpret_cast<void *>(release);
    if (device) {
        p->shadow[9] = reinterpret_cast<void *>(readState);
        p->shadow[10] = reinterpret_cast<void *>(readData);
        p->shadow[25] = reinterpret_cast<void *>(poll);
    } else p->shadow[3] = reinterpret_cast<void *>(createDevice);
    p->next = objects; objects = p;
    void *previous = InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(instance), p->shadow, table);
    if (previous != table) {
        objects = p->next;
        HeapFree(GetProcessHeap(), 0, p);
        ReleaseSRWLockExclusive(&objectLock);
        logResult("DirectInput interface changed; hook skipped", E_FAIL);
        return false;
    }
    ReleaseSRWLockExclusive(&objectLock);
    logResult(device ? "DirectInput controller attached" : "DirectInput interface attached", S_OK);
    return true;
}
HRESULT WINAPI directInputCreate(HINSTANCE module, DWORD version, REFIID iid, void **output, IUnknown *outer) {
    HRESULT hr = nextCreate(module, version, iid, output, outer);
    if (SUCCEEDED(hr) && output && *output && !outer &&
        (IsEqualIID(iid, IID_IDirectInput8A) || IsEqualIID(iid, IID_IDirectInput8W))) attach(*output, false);
    return hr;
}
void **importSlot(BYTE *image, const char *library, const char *name) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return nullptr;
    auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(image + rva);
    for (; desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<char *>(image + desc->Name), library) || !desc->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(image + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA64 *>(image + desc->FirstThunk);
        HMODULE dll = GetModuleHandleA(library);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                FARPROC byOrdinal = GetProcAddress(dll, MAKEINTRESOURCEA(IMAGE_ORDINAL64(names->u1.Ordinal)));
                if (byOrdinal && byOrdinal == GetProcAddress(dll, name)) return reinterpret_cast<void **>(&slots->u1.Function);
            } else {
                auto entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(image + names->u1.AddressOfData);
                if (!strcmp(reinterpret_cast<char *>(entry->Name), name)) return reinterpret_cast<void **>(&slots->u1.Function);
            }
        }
    }
    return nullptr;
}
bool patch(void **slot, void *hook) {
    if (!slot) return false;
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, hook);
    if (!VirtualProtect(slot, sizeof(void *), old, &ignored)) logResult("Import protection restoration failed", HRESULT_FROM_WIN32(GetLastError()));
    return true;
}
}
extern "C" void initializeControllerHelper(const wchar_t *root) {
    wchar_t setting[4];
    if (GetEnvironmentVariableW(L"QSOULS_CONTROLLER_HELPER", setting, 4) != 1 || setting[0] != L'1') return;
    if (swprintf(helperLog, MAX_PATH, L"%ls\\.local\\controller-helper.log", root) < 0) helperLog[0] = 0;
    logResult("Controller helper initialized (experimental)", S_OK);
    BYTE *image = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
    void **stateSlot = importSlot(image, "XINPUT1_3.dll", "XInputGetState");
    if (stateSlot) {
        nextState = reinterpret_cast<decltype(nextState)>(*stateSlot);
        enableInput = reinterpret_cast<decltype(enableInput)>(GetProcAddress(GetModuleHandleW(L"XINPUT1_3.dll"), "XInputEnable"));
        logResult("XInput polling hook", patch(stateSlot, reinterpret_cast<void *>(controllerState)) ? S_OK : E_FAIL);
        if (!enableInput) logResult("XInputEnable unavailable", E_NOINTERFACE);
    } else logResult("XInput polling import unavailable", E_NOINTERFACE);
    void **createSlot = importSlot(image, "DINPUT8.dll", "DirectInput8Create");
    if (createSlot) nextCreate = reinterpret_cast<decltype(nextCreate)>(*createSlot);
    logResult("DirectInput creation hook", patch(createSlot, reinterpret_cast<void *>(directInputCreate)) ? S_OK : E_NOINTERFACE);
    if (stateSlot) {
        HANDLE thread = CreateThread(nullptr, 0, focusObserver, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        else logResult("Focus observer unavailable", HRESULT_FROM_WIN32(GetLastError()));
    }
}
