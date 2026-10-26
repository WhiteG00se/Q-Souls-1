#pragma once
#include "GamePresentation.h"

// Set vertical FOV in degrees and scale native distance before smoothing/collision handling.
// Shared game parameters remain untouched.
struct QualityCamera {
    BYTE *sites[2]{}, *code=nullptr;
    volatile LONG *values=nullptr;
    unsigned applied=0;
};
static const BYTE qualityCameraOriginal[2][8]={
    {0x48,0x8d,0x48,0x14,0xf3,0x0f,0x10,0x00}, // lea rcx,[rax+14]; movss xmm0,[rax]
    {0xf3,0x0f,0x10,0x08,0xf3,0x0f,0x59,0xcd}  // movss xmm1,[rax]; mulss xmm1,xmm5
};
static inline void qualityCameraValue(QualityCamera *camera,unsigned index,float value)
{
    LONG bits; memcpy(&bits,&value,4);
    InterlockedExchange(camera->values+index,bits);
}
static inline void qualityCameraDistance(QualityCamera *camera,unsigned percent)
{
    qualityCameraValue(camera,0,percent/100.0f);
}
static inline void qualityCameraFov(QualityCamera *camera,unsigned degrees)
{
    qualityCameraValue(camera,1,(float)degrees);
}
static inline DWORD qualityCameraWrite(BYTE *site,const BYTE *bytes)
{
    DWORD previous,ignored;
    if (!VirtualProtect(site,8,PAGE_EXECUTE_READWRITE,&previous)) return GetLastError();
    memcpy(site,bytes,8);
    DWORD error=FlushInstructionCache(GetCurrentProcess(),site,8) ? ERROR_SUCCESS : GetLastError();
    if (!VirtualProtect(site,8,previous,&ignored) && !error) error=GetLastError();
    return error;
}
static inline DWORD qualityCameraRestore(QualityCamera *camera)
{
    while (camera->applied) {
        unsigned i=camera->applied-1;
        DWORD error=qualityCameraWrite(camera->sites[i],qualityCameraOriginal[i]);
        if (error) return error;
        --camera->applied;
    }
    return ERROR_SUCCESS;
}
static inline DWORD qualityCameraPrepare(QualityCamera *camera,BYTE *image)
{
    if (camera->code) return ERROR_ALREADY_INITIALIZED;
    camera->sites[0]=image+0x236ddf; camera->sites[1]=image+0x236edd;
    for (unsigned i=0;i<2;++i)
        if (memcmp(camera->sites[i],qualityCameraOriginal[i],8)) return ERROR_REVISION_MISMATCH;
    camera->code=(BYTE *)allocatePresentationSlot(camera->sites[0]);
    if (!camera->code) return ERROR_NOT_ENOUGH_MEMORY;
    camera->values=(volatile LONG *)allocatePresentationSlot(camera->code);
    DWORD error=camera->values ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (!error) for (unsigned i=0;i<2;++i) {
        qualityCameraValue(camera,i,i ? 48.0f : 1.0f);
        BYTE *stub=camera->code+i*64, *p=stub;
        // Preserve every register except the scalar destination, just as the original loads do.
        unsigned prefix=i ? 0 : 8;
        memcpy(p,qualityCameraOriginal[i],prefix); p+=prefix;
        const BYTE load[]={0xf3,0x0f,(BYTE)(i ? 0x10 : 0x59),(BYTE)(i ? 0x0d : 0x05)};
        memcpy(p,load,4); p+=4;
        intptr_t distance=(intptr_t)(camera->values+i)-(intptr_t)(p+4);
        if (distance<INT32_MIN || distance>INT32_MAX) { error=ERROR_ARITHMETIC_OVERFLOW; break; }
        int32_t relative=(int32_t)distance; memcpy(p,&relative,4); p+=4;
        if (i) { memcpy(p,qualityCameraOriginal[i]+4,4); p+=4; }
        *p++=0xe9;
        distance=(intptr_t)(camera->sites[i]+8)-(intptr_t)(p+4);
        if (distance<INT32_MIN || distance>INT32_MAX) { error=ERROR_ARITHMETIC_OVERFLOW; break; }
        relative=(int32_t)distance; memcpy(p,&relative,4);
        distance=(intptr_t)stub-(intptr_t)(camera->sites[i]+5);
        if (distance<INT32_MIN || distance>INT32_MAX) { error=ERROR_ARITHMETIC_OVERFLOW; break; }
    }
    DWORD previous;
    if (!error && (!VirtualProtect(camera->code,0x1000,PAGE_EXECUTE_READ,&previous) ||
        !FlushInstructionCache(GetCurrentProcess(),camera->code,128))) error=GetLastError();
    if (error) {
        if (camera->values) VirtualFree((void *)camera->values,0,MEM_RELEASE);
        VirtualFree(camera->code,0,MEM_RELEASE); camera->code=nullptr; camera->values=nullptr;
    }
    return error;
}
static inline DWORD qualityCameraApply(QualityCamera *camera)
{
    if (!camera->code || !camera->values || camera->applied) return ERROR_INVALID_PARAMETER;
    for (unsigned i=0;i<2;++i) {
        BYTE patch[]={0xe9,0,0,0,0,0x90,0x90,0x90};
        int32_t relative=(int32_t)((intptr_t)(camera->code+i*64)-(intptr_t)(camera->sites[i]+5));
        memcpy(patch+1,&relative,4);
        ++camera->applied;
        DWORD error=qualityCameraWrite(camera->sites[i],patch);
        if (error) { qualityCameraRestore(camera); return error; }
    }
    return ERROR_SUCCESS;
}
