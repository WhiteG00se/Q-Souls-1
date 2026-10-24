// Game EXE is mapped as non-executable data. Only private synthetic code runs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/GamePresentation.h"
#define CHECK(ok, message) do { if (!(ok)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)
using Frame = HRESULT (WINAPI *)(void *, UINT, UINT);
static unsigned frames, osdFrames, labels, flushes;
static UINT intervalSeen, flagsSeen;
static void *nativeCaller, *osdCaller, *drawChain;
static bool osdVisible = true;
static HRESULT WINAPI nativePresent(void *, UINT interval, UINT flags)
{
    ++frames; intervalSeen = interval; flagsSeen = flags;
    nativeCaller = __builtin_return_address(0); return S_FALSE;
}
static HRESULT WINAPI afterburnerPresent(void *chain, UINT interval, UINT flags)
{
    osdCaller = __builtin_return_address(0);
    if (osdVisible) ++osdFrames;
    return nativePresent(chain, interval, flags);
}
static void WINAPI drawLabel(void *chain) { ++labels; drawChain = chain; }
static void WINAPI flushContext(void *) { ++flushes; }
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 2, "installed executable argument");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE, "open installed EXE read-only");
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
    CHECK(mapping, "non-executable game image mapping");
    BYTE *image = (BYTE *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(image, "map game as data only");
    BYTE *site = findGamePresentation(image);
    CHECK(site, "one validated game Present call across all executable sections");
    printf("Game Present call RVA: 0x%llx\n", (unsigned long long)(site - image));
    BYTE *fixture = (BYTE *)VirtualAlloc(nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(fixture, "private signature fixture");
    memcpy(fixture + 100, site - 30, 53);
    BYTE *found = nullptr; unsigned count = 0;
    presentationCandidates(fixture, 0x2000, &found, &count);
    CHECK(count == 1 && found == fixture + 130, "signature relocates without fixed game address");
    found = nullptr; count = 0; presentationCandidates(fixture, 152, &found, &count);
    CHECK(!count, "truncated context rejected");
    fixture[114] ^= 1; count = 0; presentationCandidates(fixture, 0x2000, &found, &count);
    CHECK(!count, "changed Present skip condition rejected"); fixture[114] ^= 1;
    memcpy(fixture + 500, fixture + 100, 53); count = 0; presentationCandidates(fixture, 0x2000, &found, &count);
    CHECK(count == 2, "duplicate signatures counted for ambiguous-layout rejection");
    VirtualFree(fixture, 0, MEM_RELEASE);

    // Simulate the real game frame: RBP is the renderer, BL skips Present.
    const BYTE prologue[] = {0x55,0x53,0x48,0x83,0xec,0x28,0x48,0x8b,0xe9,0x8a,0xda};
    const BYTE epilogue[] = {0x48,0x83,0xc4,0x28,0x5b,0x5d,0xc3};
    BYTE *function = (BYTE *)VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(function, "private executable fixture");
    memcpy(function, prologue, sizeof(prologue));
    BYTE *body = function + sizeof(prologue); memcpy(body, site - 30, 53);
    memcpy(body + 53, epilogue, sizeof(epilogue));
    BYTE flushThunk[] = {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    void *flushCallback = (void *)flushContext; memcpy(flushThunk + 2, &flushCallback, 8); memcpy(function + 0x300, flushThunk, sizeof(flushThunk));
    int32_t relative = (int32_t)((function + 0x300) - (body + 12)); memcpy(body + 8, &relative, 4);
    DWORD previous;
    CHECK(VirtualProtect(function, 0x1000, PAGE_EXECUTE_READ, &previous), "fixture executable without writable code");
    void *table[18] = {}; table[8] = (void *)nativePresent; void **object = table;
    BYTE renderer[0x200] = {}; void *chain = &object; memcpy(renderer + 0x30, &chain, 8); renderer[0x1c0] = 2;
    auto call = (Frame)function;
    call(renderer, 0, 0);
    CHECK(frames == 1 && flushes == 1 && !labels && intervalSeen == 2 && !flagsSeen && nativeCaller == body + 36,
          "unmodified synthetic frame has native game return address");
    BYTE originalBody[53]; memcpy(originalBody, body, 53);
    void **bridge = nullptr;
    CHECK(patchGamePresentation(body + 30, (void *)drawLabel, &bridge) == ERROR_SUCCESS, "patch context-flush call before original Present");
    CHECK(!memcmp(body, originalBody, 8) && !memcmp(body + 12, originalBody + 12, 41), "only flush call displacement changes; original Present instruction stays byte-identical");
    memcpy(&relative, body + 8, 4);
    CHECK(body + 12 + relative == (BYTE *)bridge, "relative flush call reaches registered drawing bridge");
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(function, &info, sizeof(info)) && info.Protect == PAGE_EXECUTE_READ, "game code protection restored");
    CHECK(VirtualQuery(bridge, &info, sizeof(info)) && info.Protect == PAGE_EXECUTE_READ, "bridge has read/execute protection without writable code");
    DWORD64 imageBase = 0;
    CHECK(RtlLookupFunctionEntry((DWORD64)bridge + 10, &imageBase, nullptr) == (PRUNTIME_FUNCTION)((BYTE *)bridge + 64), "bridge has x64 unwind metadata");
    call(renderer, 0, 0);
    CHECK(frames == 2 && labels == 1 && flushes == 2 && drawChain == chain && nativeCaller == body + 36 && table[8] == (void *)nativePresent,
          "drawing returns to original game Present caller without changing native dispatch");
    table[8] = (void *)afterburnerPresent;
    call(renderer, 0, 0);
    CHECK(frames == 3 && labels == 2 && osdFrames == 1 && osdCaller == body + 36, "late Afterburner receives original game caller rather than mod DLL caller");
    osdVisible = false; call(renderer, 0, 0);
    CHECK(frames == 4 && labels == 3 && osdFrames == 1 && osdCaller == body + 36, "Afterburner toggle does not disable independent label");
    call(renderer, 1, 0);
    CHECK(frames == 4 && labels == 3 && flushes == 5, "skip-Present frame flushes without drawing or presenting");
    CHECK(patchGamePresentation(body + 30, (void *)drawLabel, &bridge) == ERROR_ALREADY_INITIALIZED, "repeat initialization rejected");
    RtlDeleteFunctionTable((PRUNTIME_FUNCTION)((BYTE *)bridge + 64));
    VirtualFree(function, 0, MEM_RELEASE); VirtualFree(bridge, 0, MEM_RELEASE);
    UnmapViewOfFile(image); CloseHandle(mapping); CloseHandle(file);
    puts("PASS: game signature, flush bridge, unchanged native Present bytes/caller, late hook chaining, independent visibility, skip handling and unwind/protection metadata.");
    return 0;
}
