// Isolated model, input and native-commit adapter checks. Never runs the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../runtime/TitleOverlay.h"
static void logMessage(const char *message) { (void)message; }
static void **findImport(const char *library, const char *name) { (void)library; (void)name; return NULL; }
static BOOL replaceImport(void **slot, void *replacement) { *slot = replacement; return TRUE; }
void setRespecOverlayCallbacks(BOOL (*snapshot)(char *, size_t), void (*frame)(HWND, BOOL)) { (void)snapshot; (void)frame; }
#include "../runtime/RespecRuntime.h"
#define CHECK(ok, message) do { if (!(ok)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)
static BYTE player[0x100];
static int commits, commitLevel, commitSouls, commitCost;
static BOOL loaded = TRUE;
static BOOL fakeLoaded(void) { return loaded; }
static const size_t fields[8] = {0x40,0x48,0x50,0x58,0x60,0x88,0x68,0x70};
static void put(size_t offset, int value) { memcpy(player + offset, &value, 4); }
static int get(size_t offset) { int v; memcpy(&v, player + offset, 4); return v; }
static void __stdcall fakeCommit(void *context, const int *stats)
{
    ++commits;
    memcpy(&commitLevel, (BYTE *)context + 0x294, 4); memcpy(&commitSouls, (BYTE *)context + 0x2a0, 4); memcpy(&commitCost, (BYTE *)context + 0x29c, 4);
    for (int i = 0; i < 8; ++i) put(fields[i], stats[i]);
    put(0x90, commitLevel); put(0x94, commitSouls - commitCost); put(0x80, stats[8]); put(0x84, stats[8]);
}
static SHORT WINAPI fakeKey(int key) { (void)key; return (SHORT)0x8000; }
static BOOL WINAPI fakeKeys(PBYTE keys) { memset(keys, 0x80, 256); return TRUE; }
static DWORD WINAPI fakePad(DWORD user, XINPUT_STATE *state) { (void)user; memset(state, 0x12, sizeof(*state)); return ERROR_SUCCESS; }
static HRESULT __stdcall fakeDevice(void *device, DWORD size, void *state) { (void)device; memset(state, 0x34, size); return S_OK; }
static HRESULT __stdcall fakeEvents(void *device, DWORD size, void *data, DWORD *count, DWORD flags)
{
    (void)device; (void)flags;
    if (data) {
        memset(data, 0, size * 3);
        DWORD offsets[] = {0xcb,0x58,0x58}, values[] = {0x80,0x80,0};
        for (int i = 0; i < 3; ++i) { memcpy((BYTE *)data + i * size, &offsets[i], 4); memcpy((BYTE *)data + i * size + 4, &values[i], 4); }
    }
    *count = 3; return S_OK;
}
static UINT messageKey = VK_F12;
static BOOL WINAPI fakePeek(LPMSG message, HWND window, UINT min, UINT max, UINT remove)
{
    (void)window; (void)min; (void)max; (void)remove;
    memset(message, 0, sizeof(*message)); message->message = WM_KEYDOWN; message->wParam = messageKey; return TRUE;
}

int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 2, "arguments: installed EXE (mapped as data only)");
    HMODULE image = LoadLibraryExW(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(image && !respecResolve((BYTE *)image), "unique native assignment, refresh and event-flag signatures match installed EXE");
    CHECK(respecDataGlobal > (uintptr_t)image && respecWorldGlobal > (uintptr_t)image && respecFlagsGlobal > (uintptr_t)image, "resolved globals belong to mapped game image");
    BYTE *native = (BYTE *)respecNativeCommit;
    BYTE saved = native[0x1c2]; DWORD previous, ignored;
    CHECK(VirtualProtect(native + 0x1c2, 1, PAGE_READWRITE, &previous), "protect data-only signature fixture");
    native[0x1c2] ^= 1;
    CHECK(respecResolve((BYTE *)image) == ERROR_REVISION_MISMATCH, "reject changed native refresh tail");
    native[0x1c2] = saved; VirtualProtect(native + 0x1c2, 1, previous, &ignored);
    FreeLibrary(image);

    int base[] = {12,10,10,10,10,10,10,10}; RespecDraft d; char text[1024];
    CHECK(respecBegin(&d, base, 1) && !d.freePoints && !d.selected, "starts at current stats, zero free points, Vitality");
    respecText(&d, NULL, text, sizeof(text));
    CHECK(strstr(text, "Vitality < 12 >\nAttunement 10") && strstr(text, "Faith 10\n\nSoul Level: 1\nFree Points: 0"), "row order, spacing, labels and selected-only arrows");
    CHECK(!respecAdjust(&d, 1), "cannot raise without free points");
    CHECK(respecAdjust(&d, -1) && d.values[0] == 11 && d.freePoints == 1, "lowering refunds one point");
    CHECK(!respecCanCommit(&d, base, 1), "unspent points block commit");
    respecMove(&d, -1); CHECK(d.selected == 7, "up from Vitality wraps to Faith");
    respecText(&d, NULL, text, sizeof(text)); CHECK(strstr(text, "Vitality 11\n") && strstr(text, "Faith < 10 >"), "arrows follow cursor");
    CHECK(!respecAdjust(&d, -1), "minimum ten enforced");
    CHECK(respecAdjust(&d, 1) && respecCanCommit(&d, base, 1), "point conservation after redistribution");
    respecMove(&d, 1); CHECK(d.selected == 0, "down from Faith wraps to Vitality");
    CHECK(!respecCanCommit(&d, base, 2), "reject concurrent Soul Level change");
    base[1]++; CHECK(!respecCanCommit(&d, base, 1), "reject concurrent base-stat change"); base[1]--;
    base[2] = 9; CHECK(!respecBegin(&d, base, 1), "reject legacy stat below ten"); base[2] = 100; CHECK(!respecBegin(&d, base, 1), "reject invalid stat above 99"); base[2] = 10;
    base[0] = 99; base[1] = 11; CHECK(respecBegin(&d, base, 1), "valid upper boundary"); respecMove(&d, 1); respecAdjust(&d, -1); respecMove(&d, -1); CHECK(!respecAdjust(&d, 1) && d.freePoints == 1, "cap does not consume a point");

    memset(player, 0x5a, sizeof(player)); base[0] = 12; base[1] = 10;
    for (int i = 0; i < 8; ++i) { put(fields[i], base[i]); if (i != 5) put(fields[i] + 4, base[i] + 30); }
    put(0x90, 1); put(0x94, 123456); put(0x80, 77); put(0x84, 3);
    int readBase[8], level; CHECK(respecBase((uintptr_t)player, readBase, &level) && !memcmp(readBase, base, sizeof(base)), "reads only base fields; neighboring effect/bonus fields ignored");
    CHECK(respecBegin(&respecDraft, readBase, level), "begin native adapter session");
    respecOwner = (uintptr_t)player; respecCharacter = 123; memcpy(respecIdentity, player + 0xa0, sizeof(respecIdentity)); respecNativeCommit = fakeCommit;
    respecAdjust(&respecDraft, -1); CHECK(!respecApply((uintptr_t)player, 123) && !commits, "no native call with unspent points");
    respecMove(&respecDraft, 1); respecAdjust(&respecDraft, 1);
    CHECK(!respecApply((uintptr_t)player, 124) && !commits, "reject changed character instance");
    player[0xa0] ^= 1; CHECK(!respecApply((uintptr_t)player, 123) && !commits, "reject changed saved identity"); player[0xa0] ^= 1;
    CHECK(respecApply((uintptr_t)player, 123) && commits == 1 && get(0x40) == 11 && get(0x48) == 11, "all attributes sent in one native commit");
    CHECK(commitLevel == 1 && commitSouls == 123456 && !commitCost && get(0x90) == 1 && get(0x94) == 123456 && get(0x80) == 77 && get(0x84) == 3, "preserve level, souls and humanity");
    CHECK(respecBase((uintptr_t)player, readBase, &level) && respecBegin(&respecDraft, readBase, level) && !respecDraft.freePoints, "reopening uses committed base stats");
    CHECK(respecApply((uintptr_t)player, 123) && commits == 1, "unchanged draft needs no native call");
    respecEditing = TRUE; respecAdjust(&respecDraft, -1); respecEnd(100);
    CHECK(!respecEditing && get(0x40) == 11 && commits == 1, "interruption discards draft without applying");

    LONG flags = 0x12340001;
    respecSetFlag(&flags, RESPEC_REQUEST, TRUE); respecSetFlag(&flags, RESPEC_READY, TRUE);
    CHECK(respecFlag(&flags, RESPEC_REQUEST) && respecFlag(&flags, RESPEC_READY), "bridge masks address expected flags");
    respecSetFlag(&flags, RESPEC_REQUEST, FALSE); respecSetFlag(&flags, RESPEC_READY, FALSE);
    CHECK(flags == 0x12340001, "atomic flag updates preserve unrelated event flags");

    respecRealKey = fakeKey; respecRealKeys = fakeKeys; respecRealXInput = fakePad;
    BYTE keys[256]; XINPUT_STATE pad;
    CHECK(respecKey(VK_LEFT) && respecKeys(keys) && keys[0] && !respecXInput(0, &pad) && pad.Gamepad.wButtons, "input forwarded while inactive");
    InterlockedExchange(&respecCapturing, 1);
    CHECK(!respecKey(VK_LEFT) && respecKeys(keys) && !keys[0] && !respecXInput(0, &pad) && !pad.Gamepad.wButtons && !pad.Gamepad.sThumbLX, "keyboard and controller neutralized while editing");
    CHECK(respecKey(VK_F12) == fakeKey(VK_F12) && keys[VK_F12] == 0x80, "F12 is preserved in asynchronous and keyboard-state paths even while editing");
    void *table[11] = {0}; void **device = table; table[9] = (void *)fakeDevice; table[10] = (void *)fakeEvents;
    CHECK(respecHookInput(table + 9, (void *)respecDeviceState) && respecHookInput(table + 10, (void *)respecDeviceData), "DirectInput hooks install");
    DWORD events = 3; BYTE state[256], data[48];
    CHECK(SUCCEEDED(respecDeviceState(&device, sizeof(state), state)) && !state[0] && state[0x58] == 0x34, "DirectInput captures game keys while preserving F12 state");
    CHECK(SUCCEEDED(respecDeviceData(&device, 16, data, &events, 0)) && events == 2 && data[0] == 0x58 && data[4] == 0x80 && data[16] == 0x58 && !data[20], "buffered F12 press/release preserved while game-key events are consumed");
    respecRealPeek = fakePeek; MSG message;
    CHECK(respecPeek(&message, NULL, 0, 0, PM_REMOVE) && message.message == WM_KEYDOWN && message.wParam == VK_F12, "F12 message passes through Respec capture");
    messageKey = VK_LEFT;
    CHECK(respecPeek(&message, NULL, 0, 0, PM_REMOVE) && message.message == WM_NULL, "game navigation message remains captured");
    InterlockedExchange(&respecCapturing, 0);
    CHECK(SUCCEEDED(respecDeviceState(&device, sizeof(state), state)) && state[0] == 0x34, "DirectInput forwarding restored");

    // Exercise the real bridge lifecycle against private memory and a hidden test
    // window. No game process, actual input or game routines participate.
    HWND window = CreateWindowExW(0, L"STATIC", L"Isolated Respec bridge", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window, "hidden bridge window");
    BYTE managerBytes[0x20] = {0}, worldBytes[0x80] = {0}, characterBytes[0x400] = {0};
    static BYTE flagBytes[0x11400];
    uintptr_t manager = (uintptr_t)managerBytes, world = (uintptr_t)worldBytes, flagData = (uintptr_t)flagBytes, flagHolder = (uintptr_t)&flagData;
    uintptr_t ownerPointer = (uintptr_t)player, characterPointer = (uintptr_t)characterBytes;
    memcpy(managerBytes + 0x10, &ownerPointer, sizeof(ownerPointer)); memcpy(worldBytes + 0x68, &characterPointer, sizeof(characterPointer));
    int health = 600; memcpy(characterBytes + 0x3e8, &health, 4);
    respecDataGlobal = (uintptr_t)&manager; respecWorldGlobal = (uintptr_t)&world; respecFlagsGlobal = (uintptr_t)&flagHolder;
    respecIsLoaded = fakeLoaded; respecPresented(window, TRUE); respecTick();
    volatile LONG *bridge = respecFlagWord(); CHECK(bridge && respecFlag(bridge, RESPEC_READY), "runtime publishes readiness for live character");
    respecSetFlag(bridge, RESPEC_REQUEST, TRUE); Sleep(15); respecTick();
    CHECK(respecEditing && !respecDraft.freePoints && respecCapture(), "bonfire request opens captured draft");
    respecAdjust(&respecDraft, -1); loaded = FALSE; Sleep(15); respecTick();
    CHECK(!respecEditing && !respecFlag(bridge, RESPEC_READY) && respecFlag(bridge, RESPEC_DONE) && !respecFlag(bridge, RESPEC_COMMITTED) && commits == 1, "loading cancels draft and releases script without commit");
    loaded = TRUE; respecSetFlag(bridge, RESPEC_REQUEST, FALSE); respecSetFlag(bridge, RESPEC_DONE, FALSE); Sleep(15); respecTick();
    respecSetFlag(bridge, RESPEC_REQUEST, TRUE); Sleep(15); respecTick(); CHECK(respecEditing, "subsequent request opens fresh draft");
    respecSetFlag(bridge, RESPEC_REQUEST, FALSE); Sleep(15); respecTick(); CHECK(!respecEditing && commits == 1, "ESD interruption clears editor without changing saved stats");
    respecSetFlag(bridge, RESPEC_REQUEST, TRUE); Sleep(15); respecTick(); CHECK(respecEditing, "request after interruption");
    health = 0; memcpy(characterBytes + 0x3e8, &health, 4); Sleep(15); respecTick();
    CHECK(!respecEditing && respecFlag(bridge, RESPEC_DONE) && !respecFlag(bridge, RESPEC_COMMITTED), "death discards draft");
    DestroyWindow(window);
    (void)initializeRespec;
    puts("PASS: Respec signatures, draft rules, base-only stats, native adapter, interruption, bridge flags and input capture.");
    return 0;
}
