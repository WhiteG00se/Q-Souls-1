// Native UI adapters and hooks are exercised in private fixtures only. Never executes game code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void logMessage(const char *message) { (void)message; }
#include "../runtime/RespecRuntime.h"
#define CHECK(ok, message) do { if (!(ok)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)
static BYTE player[0x100], context[0x2c0], manager[0x20], world[0x80], character[0x400];
static uintptr_t managerPointer = (uintptr_t)manager, worldPointer = (uintptr_t)world;
static const size_t fields[] = {0x40,0x48,0x50,0x58,0x60,0x88,0x68,0x70};
static int selected, lastSound, commits, resets, fallbackAdjust, fallbackArrows, fallbackChanged;
static unsigned char mode = 1;
static BOOL loaded = TRUE;
static int readInt(const BYTE *data, size_t offset) { int result; memcpy(&result, data + offset, 4); return result; }
static void put(BYTE *data, size_t offset, int value) { memcpy(data + offset, &value, 4); }
static int price(int destination) { return destination * 100; }
static int excessivePrice(int destination) { (void)destination; return INT_MAX; }
static unsigned char isLevelUp(void) { return mode; }
static BOOL isLoaded(void) { return loaded; }
static int uiRegister(int index) { return index == 0x55 ? selected : -1; }
static void sound(int value) { lastSound = value; }
static void originalAdjust(void *data, unsigned char increase) { (void)data; (void)increase; fallbackAdjust++; }
static void originalArrows(void *data, unsigned char *left, unsigned char *right) { (void)data; fallbackArrows++; *left = *right = 1; }
static unsigned char originalChanged(void *data) { (void)data; fallbackChanged++; return 1; }
static void originalReset(void *data)
{
    BYTE *bytes = (BYTE *)data; resets++;
    for (int i = 0; i < 8; ++i) put(bytes, 0x24c + i * 4, readInt(player, fields[i]));
    put(bytes, 0x26c, readInt(player, 0x84)); put(bytes, 0x294, readInt(player, 0x90));
    put(bytes, 0x298, readInt(player, 0x90)); put(bytes, 0x2a0, readInt(player, 0x94));
}
static void originalCommit(void *data, const int *values)
{
    BYTE *bytes = (BYTE *)data; commits++;
    for (int i = 0; i < 8; ++i) put(player, fields[i], values[i]);
    put(player, 0x90, readInt(bytes, 0x294)); put(player, 0x94, readInt(bytes, 0x2a0) - readInt(bytes, 0x29c));
    put(player, 0x80, values[8]); put(player, 0x84, values[8]);
}
static void openScreen(void)
{
    respecReset(context);
    // The native caller copies the baseline to its working stats after Reset.
    memcpy(context + 0x270, context + 0x24c, 36); put(context, 0x29c, 0);
}
static int relayProbe(void *argument, int value) { return (int)(uintptr_t)argument + value; }
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 2, "installed EXE path required");
    HMODULE image = LoadLibraryExW(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(image && !respecResolve((BYTE *)image), "unique native reset, adjustment, arrow, confirmation, assignment and cost signatures");
    BYTE *native = (BYTE *)respecNativeCommit; BYTE saved = native[0x1c2]; DWORD protection, ignored;
    CHECK(VirtualProtect(native + 0x1c2, 1, PAGE_READWRITE, &protection), "protect data-only signature fixture");
    native[0x1c2] ^= 1; CHECK(respecResolve((BYTE *)image) == ERROR_REVISION_MISMATCH, "changed native refresh rejected");
    native[0x1c2] = saved; VirtualProtect(native + 0x1c2, 1, protection, &ignored);
    BYTE *priceCode = (BYTE *)respecNativePrice;
    CHECK(priceCode[0x1e] == 0xb8 && readInt(priceCode, 0x1f) == 8432425, "native high-level special cost retained");
    CHECK(VirtualProtect(priceCode, 1, PAGE_READWRITE, &protection), "protect data-only cost fixture");
    saved = priceCode[0]; priceCode[0] ^= 1;
    CHECK(respecResolve((BYTE *)image) == ERROR_REVISION_MISMATCH, "changed cost signature rejected");
    priceCode[0] = saved; VirtualProtect(priceCode, 1, protection, &ignored);
    CHECK(!respecResolve((BYTE *)image), "signatures restored");
    BYTE *targets[] = {(BYTE *)respecOriginalAdjust, (BYTE *)respecOriginalArrows, (BYTE *)respecOriginalChanged, (BYTE *)respecNativeCommit, (BYTE *)respecOriginalReset};
    const unsigned counts[] = {3,1,3,2,5}; NativeCalls hooks[5] = {0}; BYTE original[5][32];
    for (unsigned i = 0; i < 5; ++i) {
        memcpy(original[i], targets[i], 32); hooks[i].target = targets[i]; hooks[i].callback = (void *)relayProbe;
        CHECK(nativePrepareCalls((BYTE *)image, &hooks[i], counts[i]) == ERROR_SUCCESS, "all known native call sites resolved before patching");
    }
    for (unsigned i = 0; i < 5; ++i) {
        CHECK(!nativeApplyCalls(&hooks[i]), "patch calls in private mapped image only");
        CHECK(!memcmp(original[i], targets[i], 32), "native function entry and unwind prologue unchanged");
        for (unsigned j = 0; j < counts[i]; ++j) {
            int32_t displacement; memcpy(&displacement, hooks[i].sites[j] + 1, 4);
            CHECK(hooks[i].sites[j] + 5 + displacement == hooks[i].relay, "call points to leaf relay");
        }
        CHECK(((int (*)(void *, int))hooks[i].relay)((void *)20, 7) == 27, "relay preserves native argument registers and return value");
    }
    for (unsigned i = 0; i < 5; ++i) CHECK(!nativeRestoreCalls(&hooks[i]) && nativeCallSites((BYTE *)image, targets[i], hooks[i].sites, 8) == counts[i], "all original calls and protections restored");
    NativeCalls mismatch = {0}; mismatch.target = targets[0]; mismatch.callback = (void *)relayProbe;
    CHECK(nativePrepareCalls((BYTE *)image, &mismatch, 4) == ERROR_REVISION_MISMATCH && !mismatch.relay, "unexpected call count fails without mutation");
    FreeLibrary(image);

    respecOriginalReset = originalReset; respecOriginalAdjust = originalAdjust; respecOriginalArrows = originalArrows;
    respecOriginalChanged = originalChanged; respecNativeCommit = originalCommit; respecNativePrice = price;
    respecIsLevelUp = isLevelUp; respecIsLoaded = isLoaded; respecRegister = uiRegister; respecSound = sound;
    respecDataGlobal = (uintptr_t)&managerPointer; respecWorldGlobal = (uintptr_t)&worldPointer;
    uintptr_t owner = (uintptr_t)player, chr = (uintptr_t)character;
    memcpy(manager + 0x10, &owner, 8); memcpy(world + 0x68, &chr, 8); put(character, 0x3e8, 600);
    for (int i = 0; i < 8; ++i) put(player, fields[i], i == 0 ? 12 : 10);
    put(player, 0x90, 1); put(player, 0x94, 0); put(player, 0x80, 77); put(player, 0x84, 3);
    openScreen(); selected = 0; unsigned char left, right;
    respecArrowsNative(context, &left, &right); CHECK(left && !right, "native arrows allow lowering base stat with no souls");
    respecAdjustNative(context, 0);
    CHECK(readInt(context, 0x270) == 11 && readInt(context, 0x24c) == 12 && readInt(context, 0x294) == 1 && !readInt(context, 0x29c), "working stat decreases; native comparison baseline and Soul Level preserved");
    CHECK(!respecChangedNative(context), "native confirmation disabled with unspent point");
    respecCommitNative(context, (int *)(context + 0x270)); CHECK(!commits, "unspent draft cannot reach native assignment");
    selected = 1; respecArrowsNative(context, &left, &right); CHECK(!left && right, "free point enables native plus arrow without souls");
    respecAdjustNative(context, 1); CHECK(!lastSound && readInt(context, 0x274) == 11 && respecChangedNative(context), "redistribution enables native confirmation");
    respecCommitNative(context, (int *)(context + 0x270));
    CHECK(commits == 1 && readInt(player, 0x40) == 11 && readInt(player, 0x48) == 11 && readInt(player, 0x90) == 1 && !readInt(player, 0x94), "native assignment commits redistribution without cost");
    CHECK(readInt(player, 0x80) == 77 && readInt(player, 0x84) == 3, "both humanity fields preserved");

    put(player, 0x94, 500); openScreen(); selected = 0;
    respecAdjustNative(context, 1); respecAdjustNative(context, 1);
    CHECK(readInt(context, 0x294) == 3 && readInt(context, 0x29c) == 500, "native previews show two purchased levels and exact total cost");
    respecArrowsNative(context, &left, &right); CHECK(left && !right, "plus disabled when next level is unaffordable");
    respecAdjustNative(context, 1); CHECK(lastSound == 200 && readInt(context, 0x29c) == 500, "unaffordable adjustment leaves draft unchanged");
    respecAdjustNative(context, 0); CHECK(readInt(context, 0x294) == 2 && readInt(context, 0x29c) == 200, "decrease refunds last purchased level");
    BYTE beforeCancel[sizeof(player)]; memcpy(beforeCancel, player, sizeof(player)); openScreen();
    CHECK(resets > 0 && !memcmp(player, beforeCancel, sizeof(player)) && readInt(context, 0x270) == 11 && !readInt(context, 0x29c), "native cancel/reset discards draft without character writes");
    respecAdjustNative(context, 1); put(context, 0x294, 100); put(context, 0x29c, 0);
    respecCommitNative(context, (int *)(context + 0x270));
    CHECK(commits == 2 && readInt(player, 0x90) == 2 && readInt(player, 0x94) == 300 && readInt(context, 0x29c) == 200, "commit recomputes native total fields from draft stats");

    openScreen(); respecAdjustNative(context, 1); int committed = commits;
    put(player, 0x94, 299); CHECK(!respecChangedNative(context), "concurrent soul change disables confirmation");
    respecCommitNative(context, (int *)(context + 0x270)); CHECK(commits == committed, "concurrent soul change cannot commit"); put(player, 0x94, 300);
    player[0xa0] ^= 1; CHECK(!respecChangedNative(context), "changed saved identity rejected"); player[0xa0] ^= 1;
    put(player, 0x90, 3); CHECK(!respecChangedNative(context), "concurrent level change rejected"); put(player, 0x90, 2);
    put(player, 0x40, 13); CHECK(!respecChangedNative(context), "concurrent base stat change rejected"); put(player, 0x40, 12);
    loaded = FALSE; CHECK(!respecChangedNative(context), "loading invalidates native draft"); loaded = TRUE;
    put(character, 0x3e8, 0); CHECK(!respecChangedNative(context), "death invalidates native draft"); put(character, 0x3e8, 600);
    uintptr_t different = chr + 8; memcpy(world + 0x68, &different, 8); CHECK(!respecChangedNative(context), "changed character instance rejected"); memcpy(world + 0x68, &chr, 8);
    openScreen(); selected = 8; respecArrowsNative(context, &left, &right); respecAdjustNative(context, 0);
    CHECK(!left && !right && lastSound == 200 && readInt(context, 0x290) == 3, "humanity row remains uneditable in Level Up");
    selected = -1; respecAdjustNative(context, 1); CHECK(lastSound == 200, "invalid native cursor rejected");
    mode = 0; respecAdjustNative(context, 1); respecArrowsNative(context, &left, &right);
    CHECK(fallbackAdjust == 1 && fallbackArrows == 1 && respecChangedNative(context) && fallbackChanged == 1, "other native menu modes delegate unchanged"); mode = 1;

    RespecDraft draft; int base[] = {12,10,10,10,10,10,10,10};
    CHECK(respecBegin(&draft, base, 1), "model fixture"); draft.price = excessivePrice; draft.souls = INT_MAX;
    CHECK(respecAdjust(&draft, 1) && !respecAdjust(&draft, 1), "cost overflow rejected");
    CHECK(respecBegin(&draft, base, 713), "level cap fixture"); draft.price = price; draft.souls = INT_MAX;
    CHECK(!respecAdjust(&draft, 1) && respecAdjust(&draft, -1) && respecAdjust(&draft, 1), "level cap permits free redistribution only");
    base[0] = 99; CHECK(respecBegin(&draft, base, 1), "stat cap fixture"); draft.price = price; draft.souls = INT_MAX;
    CHECK(!respecAdjust(&draft, 1), "stat 99 cap enforced"); draft.selected = 1;
    CHECK(!respecAdjust(&draft, -1), "stat 10 minimum enforced");
    (void)initializeRespec;
    puts("PASS: native screen signatures and 14 call sites; reversible register-preserving relays; original comparison stats; free redistribution; paid levels/refunds; confirm/cancel; stale-source guards; limits; other-mode passthrough. No game code executed.");
    return 0;
}
