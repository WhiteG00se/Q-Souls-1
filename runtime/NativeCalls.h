#pragma once
#include "GamePresentation.h"

// Redirect known direct CALLs, leaving original functions and their unwind metadata intact.
// The relay is a leaf jump: it does not change registers or allocate stack space.
typedef struct {
    BYTE *target, *relay, *sites[8]; void *callback;
    unsigned count, applied;
} NativeCalls;
static inline unsigned nativeCallSites(BYTE *image, BYTE *target, BYTE **sites, unsigned capacity)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt); unsigned count = 0;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < 5) continue;
        BYTE *data = image + section->VirtualAddress;
        for (size_t i = 0; i <= section->Misc.VirtualSize - 5; ++i) if (data[i] == 0xe8) {
            int32_t displacement; memcpy(&displacement, data + i + 1, 4);
            if (data + i + 5 + displacement != target) continue;
            if (count < capacity) sites[count] = data + i;
            ++count;
        }
    }
    return count;
}
static inline DWORD nativePrepareSites(NativeCalls *hook)
{
    if (!hook->target || !hook->callback || !hook->count || hook->count > 8 || hook->relay) return ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < hook->count; ++i) {
        BYTE *site = hook->sites[i]; int32_t displacement;
        if (!site || site[0] != 0xe8) return ERROR_REVISION_MISMATCH;
        memcpy(&displacement, site + 1, 4);
        if (site + 5 + displacement != hook->target) return ERROR_REVISION_MISMATCH;
    }
    hook->relay = (BYTE *)allocatePresentationSlot(hook->sites[0]);
    if (!hook->relay) return ERROR_NOT_ENOUGH_MEMORY;
    for (unsigned i = 0; i < hook->count; ++i) {
        intptr_t distance = hook->relay - (hook->sites[i] + 5);
        if (distance < INT32_MIN || distance > INT32_MAX) {
            VirtualFree(hook->relay, 0, MEM_RELEASE); hook->relay = NULL; return ERROR_ARITHMETIC_OVERFLOW;
        }
    }
    const BYTE jump[] = {0xff,0x25,0,0,0,0}; memcpy(hook->relay, jump, sizeof(jump));
    memcpy(hook->relay + 6, &hook->callback, sizeof(void *));
    DWORD previous;
    if (!VirtualProtect(hook->relay, 0x1000, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), hook->relay, 14)) {
        DWORD error = GetLastError(); VirtualFree(hook->relay, 0, MEM_RELEASE); hook->relay = NULL; return error;
    }
    return ERROR_SUCCESS;
}
static inline DWORD nativePrepareCalls(BYTE *image, NativeCalls *hook, unsigned expected)
{
    if (!hook->target || !hook->callback || !expected || expected > 8 || hook->relay) return ERROR_INVALID_PARAMETER;
    hook->count = nativeCallSites(image, hook->target, hook->sites, 8);
    if (hook->count != expected) return ERROR_REVISION_MISMATCH;
    return nativePrepareSites(hook);
}
static inline DWORD nativeWriteCall(BYTE *site, BYTE *target)
{
    intptr_t distance = target - (site + 5);
    if (site[0] != 0xe8 || distance < INT32_MIN || distance > INT32_MAX) return ERROR_REVISION_MISMATCH;
    DWORD previous, ignored;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return GetLastError();
    int32_t displacement = (int32_t)distance; memcpy(site + 1, &displacement, 4);
    DWORD error = FlushInstructionCache(GetCurrentProcess(), site, 5) ? ERROR_SUCCESS : GetLastError();
    if (!VirtualProtect(site, 5, previous, &ignored) && !error) error = GetLastError();
    return error;
}
static inline DWORD nativeApplyCalls(NativeCalls *hook)
{
    if (!hook->relay) return ERROR_INVALID_PARAMETER;
    while (hook->applied < hook->count) {
        DWORD error = nativeWriteCall(hook->sites[hook->applied++], hook->relay);
        if (error) return error;
    }
    return ERROR_SUCCESS;
}
static inline DWORD nativeRestoreCalls(NativeCalls *hook)
{
    while (hook->applied) {
        DWORD error = nativeWriteCall(hook->sites[hook->applied - 1], hook->target);
        if (error) return error; // Never release a relay still referenced by a live call.
        --hook->applied;
    }
    if (hook->relay) VirtualFree(hook->relay, 0, MEM_RELEASE);
    hook->relay = NULL; return ERROR_SUCCESS;
}
