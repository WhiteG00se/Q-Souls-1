// Fixture-only COM and input tests. Never launches or controls the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
static ULONGLONG now = 2000;
static bool active = true;
static ULONGLONG WINAPI fakeTicks() { return now; }
static HWND WINAPI fakeForeground() { return reinterpret_cast<HWND>(1); }
static DWORD WINAPI fakeWindowProcess(HWND, DWORD *pid) { *pid = active ? GetCurrentProcessId() : 0; return 1; }
#define GetTickCount64 fakeTicks
#define GetForegroundWindow fakeForeground
#define GetWindowThreadProcessId fakeWindowProcess
#include "../runtime/ControllerHelper.cpp"
#undef GetTickCount64
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

struct Fake {
    void **table;
    ULONG refs = 1;
    DWORD type = DI8DEVTYPE_GAMEPAD;
    HRESULT readResult = DIERR_INPUTLOST, acquireResult = S_OK, retryResult = S_OK;
    unsigned reads = 0, acquisitions = 0;
    DWORD lastCount = 0;
};
static ULONG WINAPI addRef(void *p) { return ++static_cast<Fake *>(p)->refs; }
static ULONG WINAPI freeRef(void *p) { return --static_cast<Fake *>(p)->refs; }
static HRESULT WINAPI identity(void *p, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IDirectInputDevice8A) &&
        !IsEqualIID(iid, IID_IDirectInputDevice8W) && !IsEqualIID(iid, IID_IDirectInput8A) &&
        !IsEqualIID(iid, IID_IDirectInput8W)) return E_NOINTERFACE;
    addRef(p); *out = p; return S_OK;
}
static HRESULT WINAPI capabilities(void *p, DIDEVCAPS *caps) {
    caps->dwDevType = static_cast<Fake *>(p)->type; return S_OK;
}
static HRESULT WINAPI acquireDevice(void *p) {
    Fake *f = static_cast<Fake *>(p); ++f->acquisitions;
    if (SUCCEEDED(f->acquireResult)) f->readResult = f->retryResult;
    return f->acquireResult;
}
static HRESULT WINAPI readDevice(void *p, DWORD size, void *data) {
    Fake *f = static_cast<Fake *>(p); ++f->reads;
    if (SUCCEEDED(f->readResult) && size >= sizeof(DWORD) && data) *static_cast<DWORD *>(data) = 0x12345678;
    return f->readResult;
}
static HRESULT WINAPI dataDevice(void *p, DWORD, DIDEVICEOBJECTDATA *, DWORD *count, DWORD) {
    Fake *f = static_cast<Fake *>(p); ++f->reads;
    f->lastCount = count ? *count : 0;
    if (count) *count = SUCCEEDED(f->readResult) ? 1 : 0;
    return f->readResult;
}
static HRESULT WINAPI pollDevice(void *p) { Fake *f = static_cast<Fake *>(p); ++f->reads; return f->readResult; }
static Fake *created;
static HRESULT WINAPI makeDevice(void *, REFGUID, void **out, IUnknown *) { *out = created; return S_OK; }
static unsigned enables, states;
static DWORD seenIndex;
static void WINAPI enableFixture(BOOL on) { if (on) ++enables; }
static DWORD WINAPI stateFixture(DWORD index, XINPUT_STATE *state) {
    ++states; seenIndex = index;
    if (!state) return ERROR_BAD_ARGUMENTS;
    state->dwPacketNumber = 42; state->Gamepad.wButtons = XINPUT_GAMEPAD_A;
    return index < 4 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 3);
    wchar_t logFile[MAX_PATH];
    CHECK(swprintf(logFile, MAX_PATH, L"%ls\\.local\\controller-helper.log", argv[2]) > 0);
    SetEnvironmentVariableW(L"QSOULS_CONTROLLER_HELPER", nullptr);
    initializeControllerHelper(argv[2]);
    CHECK(!objects && !helperLog[0] && !nextState && !nextCreate);
    CHECK(GetFileAttributesW(logFile) == INVALID_FILE_ATTRIBUTES);
    SetEnvironmentVariableW(L"QSOULS_CONTROLLER_HELPER", L"0");
    initializeControllerHelper(argv[2]);
    CHECK(!objects && !helperLog[0]);
    nextState = stateFixture; enableInput = enableFixture;
    XINPUT_STATE xs = {};
    CHECK(controllerState(2, &xs) == ERROR_SUCCESS);
    CHECK(enables == 1 && states == 1 && seenIndex == 2 && xs.dwPacketNumber == 42 && xs.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(controllerState(8, &xs) == ERROR_DEVICE_NOT_CONNECTED && enables == 1);
    active = false; CHECK(controllerState(1, &xs) == ERROR_SUCCESS && enables == 1);
    active = true; CHECK(controllerState(1, &xs) == ERROR_SUCCESS && enables == 2);
    // The observer detects focus changes even if the game does not poll in the background.
    observeFocus(FALSE); observeFocus(TRUE);
    CHECK(controllerState(0, &xs) == ERROR_SUCCESS && enables == 3);
    enableInput = nullptr; CHECK(controllerState(0, nullptr) == ERROR_BAD_ARGUMENTS);

    void *table[32] = {};
    table[0] = reinterpret_cast<void *>(identity); table[1] = reinterpret_cast<void *>(addRef);
    table[2] = reinterpret_cast<void *>(freeRef); table[3] = reinterpret_cast<void *>(capabilities);
    table[7] = reinterpret_cast<void *>(acquireDevice); table[9] = reinterpret_cast<void *>(readDevice);
    table[10] = reinterpret_cast<void *>(dataDevice); table[25] = reinterpret_cast<void *>(pollDevice);
    Fake pad = {table};
    CHECK(attach(&pad, true));
    CHECK(pad.table != table && pad.table[1] == table[1] && pad.table[7] == table[7]);
    CHECK(attach(&pad, true) && objects->next == nullptr);
    void *unknown = nullptr, *wide = nullptr;
    CHECK(query(&pad, IID_IUnknown, &unknown) == S_OK && unknown == &pad && pad.refs == 2);
    CHECK(query(&pad, IID_IDirectInputDevice8W, &wide) == S_OK && wide == unknown && pad.refs == 3);
    CHECK(release(wide) == 2 && release(unknown) == 1 && lookupLocked(&pad));
    CHECK(query(&pad, IID_IDirectInputEffect, &wide) == E_NOINTERFACE && !wide && pad.refs == 1);
    DWORD value = 0;
    CHECK(readState(&pad, sizeof(value), &value) == S_OK && pad.acquisitions == 1 && pad.reads == 2 && value == 0x12345678);
    CHECK(readState(&pad, sizeof(value), &value) == S_OK && pad.acquisitions == 1);
    pad.readResult = DIERR_NOTACQUIRED;
    CHECK(readState(&pad, sizeof(value), &value) == DIERR_NOTACQUIRED && pad.acquisitions == 1);
    now += 1000; pad.acquireResult = DIERR_OTHERAPPHASPRIO;
    unsigned before = pad.reads;
    CHECK(poll(&pad) == DIERR_NOTACQUIRED && pad.acquisitions == 2 && pad.reads == before + 1);
    now += 1000; active = false;
    CHECK(poll(&pad) == DIERR_NOTACQUIRED && pad.acquisitions == 2);
    active = true; pad.acquireResult = S_OK;
    CHECK(poll(&pad) == S_OK && pad.acquisitions == 3);
    now += 1000; pad.readResult = DIERR_INPUTLOST;
    DWORD count = 7;
    CHECK(readData(&pad, sizeof(DIDEVICEOBJECTDATA), nullptr, &count, 0) == S_OK && pad.lastCount == 7 && count == 1);
    now += 1000; pad.readResult = DIERR_INPUTLOST; pad.retryResult = DIERR_INPUTLOST;
    before = pad.reads;
    CHECK(readState(&pad, 4, &value) == DIERR_INPUTLOST && pad.reads == before + 2);
    now += 1000; pad.readResult = E_INVALIDARG; before = pad.acquisitions;
    CHECK(readState(&pad, 4, &value) == E_INVALIDARG && pad.acquisitions == before);

    void *inputTable[11] = {};
    inputTable[0] = table[0]; inputTable[1] = table[1]; inputTable[2] = table[2];
    inputTable[3] = reinterpret_cast<void *>(makeDevice);
    Fake input = {inputTable};
    CHECK(attach(&input, false));
    Fake keyboard = {table}; keyboard.type = DI8DEVTYPE_KEYBOARD;
    Fake mouse = {table}; mouse.type = DI8DEVTYPE_MOUSE;
    Fake otherPad = {table};
    void *out = nullptr;
    created = &keyboard; CHECK(createDevice(&input, GUID_SysKeyboard, &out, nullptr) == S_OK && out == &keyboard && keyboard.table == table);
    created = &mouse; CHECK(createDevice(&input, GUID_SysMouse, &out, nullptr) == S_OK && mouse.table == table);
    created = &otherPad;
    CHECK(createDevice(&input, GUID_Joystick, &out, reinterpret_cast<IUnknown *>(1)) == S_OK && otherPad.table == table);
    CHECK(createDevice(&input, GUID_Joystick, &out, nullptr) == S_OK && out == &otherPad && otherPad.table != table);
    CHECK(release(&otherPad) == 0 && !lookupLocked(&otherPad));
    CHECK(release(&pad) == 0 && !lookupLocked(&pad));
    CHECK(release(&input) == 0 && objects == nullptr);
    // Missing imports are nonfatal; PE import absence is handled without mutation.
    BYTE image[1024] = {};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image); dos->e_lfanew = 128;
    CHECK(!importSlot(image, "missing.dll", "missing"));
    void *slot = reinterpret_cast<void *>(stateFixture);
    CHECK(patch(&slot, reinterpret_cast<void *>(controllerState)) && slot == reinterpret_cast<void *>(controllerState));
    CHECK(!patch(nullptr, nullptr));
    // Map the installed executable as data; never run its entrypoint or load its dependencies.
    HMODULE xinput = LoadLibraryExW(L"XINPUT1_3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(xinput);
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
    CHECK(mapping);
    BYTE *mapped = static_cast<BYTE *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    CHECK(mapped);
    CHECK(importSlot(mapped, "XINPUT1_3.dll", "XInputGetState"));
    CHECK(importSlot(mapped, "DINPUT8.dll", "DirectInput8Create"));
    CHECK(GetProcAddress(xinput, "XInputEnable"));
    UnmapViewOfFile(mapped); CloseHandle(mapping); CloseHandle(file); FreeLibrary(xinput);
    SetEnvironmentVariableW(L"QSOULS_CONTROLLER_HELPER", L"1");
    initializeControllerHelper(argv[2]);
    CHECK(GetFileAttributesW(logFile) != INVALID_FILE_ATTRIBUTES && !objects);
    puts("PASS: disabled initialization, focus transitions, XInput values/results, COM identity/lifetime, controller classification, bounded foreground recovery, failed Acquire/retry, buffered counts, missing imports, hook chaining.");
    return 0;
}
