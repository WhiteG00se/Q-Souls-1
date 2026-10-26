#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "QualitySettings.h"
#include "QualityModel.h"
#include "QualityCamera.h"
#include "NativeCalls.h"
#include "../third_party/ds1-fps-unlock/src/dsr/state.h"
#include "../third_party/ds1-fps-unlock/src/dsr/profile.h"
#include "../third_party/ds1-fps-unlock/src/dsr/patches.h"
#include "../third_party/ds1-fps-unlock/src/dsr/bonfire.h"
#include "../third_party/ds1-fps-unlock/src/common/log.h"

namespace {
constexpr int requestFlag=70000980, doneFlag=70000981, readyFlag=70000983;
NativeCalls openingHook;
QualityCamera camera;
unsigned selectedRow=0, fovDegrees=qualityDefaultFov, distancePercent=qualityDefaultDistance;
uintptr_t flagsGlobal, worldGlobal;
void (*setRegister)(int, int);
std::atomic<int> fpsStatus{0};
std::atomic<bool> capturing{false};
SRWLOCK panelLock=SRWLOCK_INIT;
bool editing=false, saveFailed=false, renderOK=false;
HWND panelWindow;
ULONGLONG lastFrame=0, captureUntil=0;
uintptr_t lastFlags=0, lastCharacter=0;
wchar_t settingsPath[MAX_PATH], fpsLogPath[MAX_PATH];
void (*report)(const char *);
void selectOpeningCategory(int index, int value)
{
    setRegister(index,index==0x4b && value==0 ? 1 : value);
}
bool readMemory(uintptr_t address,void *out,size_t size)
{
    SIZE_T count=0;
    return address && ReadProcessMemory(GetCurrentProcess(),(void *)address,out,size,&count) && count==size;
}
uintptr_t pointer(uintptr_t address)
{
    uintptr_t result=0; readMemory(address,&result,sizeof(result)); return result;
}
volatile LONG *flagWord()
{
    uintptr_t address=pointer(pointer(flagsGlobal));
    if (!address) return nullptr;
    address+=0x11378; MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
        (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
        !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
        address<(uintptr_t)info.BaseAddress || sizeof(LONG)>info.RegionSize-(address-(uintptr_t)info.BaseAddress)) return nullptr;
    return (volatile LONG *)address;
}
LONG mask(int flag) { return (LONG)(0x80000000u>>(flag%32)); }
bool flag(volatile LONG *word,int id) { return (InterlockedCompareExchange(word,0,0)&mask(id))!=0; }
void setFlag(volatile LONG *word,int id,bool value)
{
    if (value) InterlockedOr(word,mask(id)); else InterlockedAnd(word,~mask(id));
}
bool writableFilePath(const wchar_t *path)
{
    wchar_t part[MAX_PATH];
    if (wcslen(path) >= MAX_PATH) return false;
    wcscpy(part, path);
    for (;;) {
        DWORD attrs = GetFileAttributesW(part);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        wchar_t *slash = wcsrchr(part, L'\\');
        if (!slash) break;
        *slash = 0;
    }
    return true;
}
bool saveSetting(const wchar_t *key,unsigned value)
{
    if (!writableFilePath(settingsPath)) return false;
    wchar_t text[16]; swprintf(text, 16, L"%u", value);
    return WritePrivateProfileStringW(L"Quality", key, text, settingsPath) != FALSE;
}
void moveLimit(int direction)
{
    if (fpsStatus.load()!=1) return;
    unsigned current=g_target_fps.load(), next=qualityNextFps(current,direction);
    if (next==current) return;
    if (!saveSetting(L"FPSLimit",next)) { saveFailed=true; return; }
    saveFailed=false; g_target_fps.store(next);
}
void moveSetting(int direction)
{
    if (!selectedRow) { moveLimit(direction); return; }
    unsigned &current=selectedRow==1 ? fovDegrees : distancePercent;
    unsigned next=selectedRow==1 ? qualityNext(qualityFovDegrees,current,direction,qualityDefaultFov) :
        qualityNext(qualityDistances,current,direction,qualityDefaultDistance);
    if (next==current) return;
    if (!saveSetting(selectedRow==1 ? L"FOVDegrees" : L"CameraDistancePercent",next)) { saveFailed=true; return; }
    saveFailed=false; current=next;
    if (selectedRow==1) qualityCameraFov(&camera,next); else qualityCameraDistance(&camera,next);
}
void loadCameraSettings()
{
    fovDegrees=GetPrivateProfileIntW(L"Quality",L"FOVDegrees",qualityDefaultFov,settingsPath);
    distancePercent=GetPrivateProfileIntW(L"Quality",L"CameraDistancePercent",qualityDefaultDistance,settingsPath);
    if (!qualityValid(qualityFovDegrees,fovDegrees)) fovDegrees=qualityDefaultFov;
    if (!qualityValid(qualityDistances,distancePercent)) distancePercent=qualityDefaultDistance;
    qualityCameraDistance(&camera,distancePercent); qualityCameraFov(&camera,fovDegrees);
}
BOOL qualityText(char *text,size_t capacity)
{
    AcquireSRWLockShared(&panelLock);
    bool show=editing;
    if (show) {
        const char *status=saveFailed ? "Could not save. Setting unchanged." :
            fpsStatus.load()<0 ? "FPS module failed. See .local/fps.log." :
            fpsStatus.load()==0 ? "FPS module is starting..." : "";
        unsigned fps=g_target_fps.load();
        snprintf(text,capacity,"Q Souls\n\n%sFPS < %u%s >\n%sFOV (Y axis) < %u\xB0%s >\n%sCamera Distance < %u%%%s >%s%s",
            selectedRow==0 ? "> " : "  ",fps,qualityDefaultLabel(fps,qualityDefaultFps),
            selectedRow==1 ? "> " : "  ",fovDegrees,qualityDefaultLabel(fovDegrees,qualityDefaultFov),
            selectedRow==2 ? "> " : "  ",distancePercent,qualityDefaultLabel(distancePercent,qualityDefaultDistance),
            *status ? "\n\n" : "",status);
    }
    ReleaseSRWLockShared(&panelLock); return show;
}
void presented(HWND window,BOOL success)
{
    AcquireSRWLockExclusive(&panelLock);
    panelWindow=window; lastFrame=GetTickCount64(); renderOK=success;
    ReleaseSRWLockExclusive(&panelLock);
}
void endPanel(ULONGLONG now) { editing=false; saveFailed=false; captureUntil=now+150; }
// This runs on the game's window thread; rendering reads a locked snapshot only.
void tickPanel(ULONGLONG now,volatile LONG *word,uintptr_t character,bool alive,bool rendering,bool focused,unsigned buttons)
{
    static unsigned previousButtons=0;
    static ULONGLONG repeatAt=0;
    if (word && ((uintptr_t)word!=lastFlags || character!=lastCharacter)) {
        setFlag(word,requestFlag,false); setFlag(word,doneFlag,false);
        lastFlags=(uintptr_t)word; lastCharacter=character;
        if (editing) endPanel(now);
    }
    if (word) setFlag(word,readyFlag,alive && rendering);
    bool request=word && flag(word,requestFlag);
    if (editing && (!request || !alive || !word || (!rendering && focused))) {
        endPanel(now); if (request) setFlag(word,doneFlag,true);
    }
    if (!editing && request && !flag(word,doneFlag) && alive && rendering) {
        editing=true; saveFailed=false; selectedRow=0; previousButtons=buttons; repeatAt=now+350;
    }
    if (editing && focused) {
        unsigned pressed=buttons&~previousButtons;
        if ((buttons&15) && now>=repeatAt) { pressed|=buttons&15; repeatAt=now+100; }
        if (buttons!=previousButtons) repeatAt=now+350;
        if ((pressed&3)==1 && selectedRow) { --selectedRow; saveFailed=false; }
        if ((pressed&3)==2 && selectedRow<2) { ++selectedRow; saveFailed=false; }
        if ((pressed&12)==4) moveSetting(-1);
        if ((pressed&12)==8) moveSetting(1);
        if (pressed&16) { setFlag(word,doneFlag,true); endPanel(now); }
        previousButtons=buttons;
    }
    capturing.store(editing || now<captureUntil);
    g_external_menu_active.store(capturing.load());
}
bool supportedImage(BYTE *image)
{
    auto *dos = (IMAGE_DOS_HEADER *)image;
    if (!image || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->FileHeader.TimeDateStamp == kBuild.timestamp && nt->OptionalHeader.SizeOfImage == kBuild.image_size &&
        nt->OptionalHeader.CheckSum == kBuild.checksum;
}
DWORD resolveMenu(BYTE *image)
{
    if (!supportedImage(image) || memcmp(image+0x66f070,"\x33\xd2\x8d\x4a\x4b",5) ||
        memcmp(image+0x66f07a,"\x66\xc7\x83\x48\x02\x00\x00\x01\x01",9)) return ERROR_REVISION_MISMATCH;
    // The same event-flag manager reference used by the original Respec bridge.
    const BYTE pattern[]={0x48,0x8b,0x0d,0,0,0,0,0x99,0x33,0xc2,0x45,0x33,0xc0,0x2b,0xc2,0x8d,0x50,0xf6};
    auto *nt=(IMAGE_NT_HEADERS64 *)(image+((IMAGE_DOS_HEADER *)image)->e_lfanew);
    auto *section=IMAGE_FIRST_SECTION(nt); BYTE *found=nullptr;
    for (unsigned n=0;n<nt->FileHeader.NumberOfSections;++n,++section) {
        if (!(section->Characteristics&IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize<sizeof(pattern)) continue;
        BYTE *start=image+section->VirtualAddress;
        for (size_t i=0;i<=section->Misc.VirtualSize-sizeof(pattern);++i) {
            if (memcmp(start+i,pattern,3) || memcmp(start+i+7,pattern+7,sizeof(pattern)-7)) continue;
            if (found) return ERROR_REVISION_MISMATCH;
            found=start+i;
        }
    }
    if (!found) return ERROR_REVISION_MISMATCH;
    int32_t offset; memcpy(&offset,found+3,4); flagsGlobal=(uintptr_t)(found+7+offset);
    worldGlobal=(uintptr_t)(image+0x1c77e50);
    setRegister=(decltype(setRegister))(image+0x71abf0);
    openingHook.target=(BYTE *)setRegister; openingHook.callback=(void *)selectOpeningCategory;
    openingHook.sites[0]=image+0x66f075; openingHook.count=1;
    return nativePrepareSites(&openingHook);
}
DWORD WINAPI startFps(void *)
{
    bool ok = patches_apply();
    fpsStatus.store(ok ? 1 : -1);
    if (ok) bonfire_start();
    if (report) report(ok ? "FPS module ready; Q Souls controls the live frame limit." : "FPS module failed; see .local/fps.log.");
    return 0;
}
}
extern "C" BOOL qualityCapture(void) { return capturing.load(); }
extern "C" void qualityTick(void)
{
    AcquireSRWLockExclusive(&panelLock);
    if (!panelWindow || GetWindowThreadProcessId(panelWindow,nullptr)!=GetCurrentThreadId()) {
        ReleaseSRWLockExclusive(&panelLock); return;
    }
    ULONGLONG now=GetTickCount64();
    uintptr_t character=pointer(pointer(worldGlobal)+0x68); int hp=0;
    bool alive=character && readMemory(character+0x3e8,&hp,sizeof(hp)) && hp>0;
    bool healthy=renderOK && now-lastFrame<3000 && qualityInputHealthy();
    tickPanel(now,flagWord(),character,alive,healthy,GetForegroundWindow()==panelWindow,qualityButtons());
    ReleaseSRWLockExclusive(&panelLock);
}
extern "C" DWORD initializeQualitySettings(const wchar_t *root,void (*log)(const char *))
{
    if (swprintf(settingsPath, MAX_PATH, L"%ls\\.local\\quality.ini", root) < 0 ||
        swprintf(fpsLogPath, MAX_PATH, L"%ls\\.local\\fps.log", root) < 0) return ERROR_BUFFER_OVERFLOW;
    wchar_t previousLog[MAX_PATH];
    if (swprintf(previousLog, MAX_PATH, L"%ls.prev", fpsLogPath) < 0) return ERROR_BUFFER_OVERFLOW;
    if (!writableFilePath(settingsPath) || !writableFilePath(fpsLogPath) || !writableFilePath(previousLog)) return ERROR_ACCESS_DENIED;
    unsigned fps = GetPrivateProfileIntW(L"Quality", L"FPSLimit", qualityDefaultFps, settingsPath);
    if (!qualityValidFps(fps)) fps = qualityDefaultFps;
    g_target_fps.store(fps); g_frame_dt.store(1.0f / fps);
    // Retain Remastered's baseline movement/camera/HUD; enable frame-rate corrections only.
    g_camera_ptde_speed.store(0); g_hud_ptde_speed.store(0); g_foot_ik_ptde_speed.store(0);
    g_slide_ptde.store(0); g_graze_ptde.store(0); g_step_down_ptde.store(0);
    g_step_height_ptde.store(0); g_air_damping_ptde.store(0);
    report = log; log_init(fpsLogPath, L"fps.log");
    DWORD error=resolveMenu((BYTE *)GetModuleHandleW(nullptr));
    if (error) return error;
    error=nativeApplyCalls(&openingHook);
    if (error) { nativeRestoreCalls(&openingHook); return error; }
    error=initializeQualityInput();
    if (error) { nativeRestoreCalls(&openingHook); return error; }
    error=qualityCameraPrepare(&camera,(BYTE *)GetModuleHandleW(nullptr));
    if (error) { nativeRestoreCalls(&openingHook); return error; }
    loadCameraSettings();
    error=qualityCameraApply(&camera);
    if (error) { nativeRestoreCalls(&openingHook); return error; }
    setQualityOverlay(qualityText); setQualityFrame(presented);
    HANDLE thread=CreateThread(nullptr,0,startFps,nullptr,0,nullptr);
    if (!thread) { DWORD failure=GetLastError(); qualityCameraRestore(&camera); nativeRestoreCalls(&openingHook); return failure; }
    CloseHandle(thread);
    log("Q Souls uses the original Respec panel bridge; the native bonfire list is closed while editing.");
    return ERROR_SUCCESS;
}
