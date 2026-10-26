// Read the installed EXE as data; never execute game code.
#include <initializer_list>
#include "../runtime/QualitySettings.cpp"
#include "../third_party/ds1-fps-unlock/src/dsr/live_clock.h"
extern "C" void setQualityOverlay(BOOL (*)(char *, size_t)) {}
extern "C" void setQualityFrame(void (*)(HWND, BOOL)) {}
extern "C" unsigned qualityButtons(void) { return 0; }
extern "C" BOOL qualityInputHealthy(void) { return TRUE; }
extern "C" DWORD initializeQualityInput(void) { return ERROR_SUCCESS; }
#define CHECK(x, message) do { if (!(x)) { fprintf(stderr,"FAIL: %s (%d)\n",message,__LINE__); return 1; } } while(0)
static int registerIndex,registerValue;
static void storeRegister(int index,int value) { registerIndex=index; registerValue=value; }
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 3, "EXE and external temporary folder required");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE, "read-only game file");
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
    CHECK(mapping, "non-executable mapping");
    BYTE *image = (BYTE *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(image && supportedImage(image), "supported installed Steam build");
    BYTE *fixture = (BYTE *)VirtualAlloc(nullptr, kBuild.image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(fixture, "private fixture"); memcpy(fixture, image, kBuild.image_size);
    CHECK(resolveMenu(fixture)==ERROR_SUCCESS,"native opening signatures and original flag-manager reference validated");
    CHECK(qualityCameraPrepare(&camera,fixture)==ERROR_SUCCESS,"installed camera target instructions validated");
    CHECK(qualityCameraApply(&camera)==ERROR_SUCCESS && camera.applied==2,"both private camera sites patched");
    CHECK(qualityCameraRestore(&camera)==ERROR_SUCCESS && !camera.applied &&
        !memcmp(fixture+0x236ddf,qualityCameraOriginal[0],8) &&
        !memcmp(fixture+0x236edd,qualityCameraOriginal[1],8),"camera instructions restored exactly");
    QualityCamera rejected;
    fixture[0x236edd]^=1;
    CHECK(qualityCameraPrepare(&rejected,fixture)==ERROR_REVISION_MISMATCH && !rejected.code,"changed camera code rejected before patching");
    fixture[0x236edd]^=1;
    // Execute only our generated instructions and synthetic return code, never game code.
    DWORD old;
    CHECK(VirtualProtect(camera.code,4096,PAGE_READWRITE,&old),"prepare isolated camera harness");
    for (unsigned i=0;i<2;++i) {
        BYTE *stub=camera.code+i*64, *result=camera.code+128+i*16, *entry=camera.code+192+i*32;
        if (i) { const BYTE copy[]={0x0f,0x28,0xc1,0xc3}; memcpy(result,copy,4); }
        else *result=0xc3;
        unsigned end=i ? 17 : 21;
        int32_t rel=(int32_t)(result-(stub+end)); memcpy(stub+end-4,&rel,4);
        const BYTE setup[]={0x48,0x89,0xc8,0x0f,0x28,0xe9,0xe9,0,0,0,0};
        memcpy(entry,setup,sizeof(setup)); rel=(int32_t)(stub-(entry+11)); memcpy(entry+7,&rel,4);
    }
    CHECK(VirtualProtect(camera.code,4096,PAGE_EXECUTE_READ,&old) && FlushInstructionCache(GetCurrentProcess(),camera.code,256),"synthetic harness executable");
    float params[6]={4.0f,0,0,0,0,48.0f};
    auto distanceTarget=(float (*)(float *,float))(camera.code+192);
    auto fovTarget=(float (*)(float *,float))(camera.code+224);
    CHECK(distanceTarget(params,1)==4.0f && fovTarget(params+5,1)==48.0f,"default distance and absolute vertical FOV");
    qualityCameraDistance(&camera,150); qualityCameraFov(&camera,60);
    CHECK(distanceTarget(params,1)==6.0f && fovTarget(params+5,0.5f)==30.0f && params[0]==4.0f && params[5]==48.0f,
        "FOV degrees applied before radians conversion without altering shared parameters");
    params[5]=38.0f;
    CHECK(fovTarget(params+5,1)==60.0f,"selected FOV stays absolute across native area values");
    qualityCameraDistance(&camera,100); qualityCameraFov(&camera,48);
    CHECK(distanceTarget(params,1)==4.0f && fovTarget(params+5,1)==48.0f,"defaults restored without accumulation");
    for (unsigned value : qualityFovDegrees) {
        qualityCameraFov(&camera,value);
        CHECK(fovTarget(params+5,1)==(float)value,"every FOV option reaches native target exactly");
    }
    qualityCameraFov(&camera,48);
    for (unsigned value : qualityDistances) {
        qualityCameraDistance(&camera,value);
        CHECK(distanceTarget(params,1)==4.0f*(value/100.0f),"every distance option reaches native target");
    }
    qualityCameraDistance(&camera,100);
    auto checkSteps=[](const auto &values,unsigned fallback) {
        unsigned current=values[0];
        for (unsigned value : values) {
            if (value!=current) return false;
            current=qualityNext(values,current,1,fallback);
        }
        const size_t count=sizeof(values)/sizeof(values[0]);
        if (current!=values[count-1]) return false;
        for (size_t i=count;i>0;--i) {
            if (current!=values[i-1]) return false;
            current=qualityNext(values,current,-1,fallback);
        }
        return current==values[0] && qualityNext(values,999,1,fallback)==fallback;
    };
    CHECK(checkSteps(qualityFpsLimits,60) && checkSteps(qualityFovDegrees,48) && checkSteps(qualityDistances,100),
        "all requested options traverse forward/backward with clamped endpoints");
    CHECK(!qualityValid(qualityFovDegrees,50) && !qualityValid(qualityFovDegrees,100) &&
        !qualityValid(qualityDistances,95) && !qualityValid(qualityDistances,210),"unsupported saved values rejected");
    BYTE openingOriginal[5]; memcpy(openingOriginal,fixture+0x66f075,5);
    CHECK(nativeApplyCalls(&openingHook)==ERROR_SUCCESS && memcmp(openingOriginal,fixture+0x66f075,5),"private Start-opening call redirected");
    CHECK(nativeRestoreCalls(&openingHook)==ERROR_SUCCESS && !memcmp(openingOriginal,fixture+0x66f075,5),"Start-opening call restored");
    setRegister=storeRegister;
    selectOpeningCategory(0x4b,0);
    CHECK(registerIndex==0x4b && registerValue==1,"opening Start selects Equipment in its native second slot");
    selectOpeningCategory(0x4b,0);
    CHECK(registerValue==1,"reopening Start selects Equipment again");
    selectOpeningCategory(0x4b,2);
    CHECK(registerValue==2,"non-opening selection values pass through");
    selectOpeningCategory(8,0);
    CHECK(registerIndex==8 && registerValue==0,"other registers pass through");
    fixture[0x66f070]^=1;
    CHECK(resolveMenu(fixture)==ERROR_REVISION_MISMATCH,"changed Start-opening code rejected"); fixture[0x66f070]^=1;
    ((IMAGE_NT_HEADERS64 *)(fixture + ((IMAGE_DOS_HEADER *)fixture)->e_lfanew))->FileHeader.TimeDateStamp ^= 1;
    CHECK(!supportedImage(fixture), "unsupported build rejected");
    LiveFpsClock clock;
    CHECK(clock.sample(1000,900,60)==1000 && clock.sample(1600,900,60)==1600,"native clock");
    CHECK(clock.sample(1600,900,120)==1600 && clock.sample(2200,900,120)==2800,"120 transition continuous");
    CHECK(clock.sample(2200,900,30)==2800 && clock.sample(2800,900,30)==3100,"30 transition continuous");
    CHECK(clock.sample(3000,2900,240)==3000,"game clock reset reanchors");
    CHECK(qualityDefaultFps==60 && !qualityValidFps(999),"default and invalid saved value");
    CreateDirectoryW(argv[2], nullptr);
    CHECK(swprintf(settingsPath,MAX_PATH,L"%ls\\quality.ini",argv[2])>0,"fixture settings path");
    CHECK(GetPrivateProfileIntW(L"Quality",L"FPSLimit",qualityDefaultFps,settingsPath)==60,"fresh FPS setting defaults to 60");
    CHECK(saveSetting(L"FOVPercent",125) && saveSetting(L"CameraDistancePercent",95),"legacy settings fixture");
    loadCameraSettings();
    CHECK(fovDegrees==48 && distancePercent==100 && fovTarget(params+5,1)==48.0f,"legacy FOV units and invalid distance fall back to defaults");
    CHECK(saveSetting(L"FOVDegrees",64) && saveSetting(L"CameraDistancePercent",275),"write new saved camera units");
    loadCameraSettings();
    CHECK(fovDegrees==64 && distancePercent==275 && fovTarget(params+5,1)==64.0f && distanceTarget(params,1)==11.0f,"saved settings reload and apply with new units");
    CHECK(saveSetting(L"FOVDegrees",48) && saveSetting(L"CameraDistancePercent",100),"restore fixture defaults");
    loadCameraSettings();
    volatile LONG word=0; fpsStatus.store(1); g_target_fps.store(120);
    auto tick=[&](ULONGLONG now,unsigned buttons,bool alive=true,bool healthy=true,bool focused=true,uintptr_t character=42) {
        tickPanel(now,&word,character,alive,healthy,focused,buttons);
    };
    tick(1000,0); CHECK(flag(&word,readyFlag) && !qualityCapture(),"healthy closed panel advertises availability without capture");
    setFlag(&word,requestFlag,true); tick(1010,8);
    char text[512]; CHECK(qualityText(text,sizeof(text)) && strstr(text,"FPS < 120 >") && qualityCapture() && g_external_menu_active.load(),"bridge opens the reference-style panel");
    CHECK(strstr(text,"FOV (Y axis) < 48\xB0 (default) >") && strstr(text,"Camera Distance < 100% (default) >") &&
        !strstr(text,"120 (default)"),"default suffixes only on the requested default values");
    g_target_fps.store(60);
    CHECK(qualityText(text,sizeof(text)) && strstr(text,"FPS < 60 (default) >"),"FPS default suffix");
    g_target_fps.store(120);
    CHECK(g_target_fps.load()==120,"opening held button does not leak into panel");
    CHECK(!strstr(text,"Arrows") && !strstr(text,"D-pad") && !strstr(text,"Back:"),"requested instruction line removed");
    tick(1020,0); tick(1030,4); CHECK(g_target_fps.load()==60,"left lowers cap");
    tick(1040,4); CHECK(g_target_fps.load()==60,"held button respects repeat delay");
    tick(1400,4); CHECK(g_target_fps.load()==30,"held left repeats after delay");
    tick(1600,4); CHECK(g_target_fps.load()==30,"minimum stays 30");
    for (unsigned fps : {60u,120u,240u}) {
        tick(1700+fps,0); tick(1710+fps,8);
        CHECK(g_target_fps.load()==fps && GetPrivateProfileIntW(L"Quality",L"FPSLimit",0,settingsPath)==fps,"right increases and persists cap");
    }
    tick(2100,0); tick(2110,8); CHECK(g_target_fps.load()==240,"maximum stays 240");
    tick(2120,0); tick(2130,4,true,true,false); CHECK(g_target_fps.load()==240,"unfocused panel ignores buttons");
    wchar_t saved[MAX_PATH]; wcscpy(saved,settingsPath);
    swprintf(settingsPath,MAX_PATH,L"%ls\\missing\\quality.ini",argv[2]); tick(2140,0); tick(2150,4);
    CHECK(saveFailed && g_target_fps.load()==240 && qualityText(text,sizeof(text)) && strstr(text,"Could not save"),"failed save leaves active cap and shows error"); wcscpy(settingsPath,saved);
    tick(2151,0); tick(2152,2);
    CHECK(selectedRow==1 && qualityText(text,sizeof(text)) && strstr(text,"> FOV (Y axis) < 48\xB0 (default) >"),"down selects FOV");
    tick(2153,0); tick(2154,8);
    CHECK(fovDegrees==52 && GetPrivateProfileIntW(L"Quality",L"FOVDegrees",0,settingsPath)==52 &&
        fovTarget(params+5,1)==52.0f,"FOV changes persist and update the live target");
    tick(2155,0); tick(2156,2); tick(2157,0); tick(2158,4);
    CHECK(selectedRow==2 && distancePercent==90 && GetPrivateProfileIntW(L"Quality",L"CameraDistancePercent",0,settingsPath)==90 &&
        distanceTarget(params,1)==3.6f,"camera distance changes independently and persists");
    swprintf(settingsPath,MAX_PATH,L"%ls\\missing\\quality.ini",argv[2]);
    tick(2159,0); tick(2160,8);
    CHECK(saveFailed && distancePercent==90 && distanceTarget(params,1)==3.6f,"failed camera save preserves live value");
    wcscpy(settingsPath,saved);
    tick(2161,0); tick(2162,1);
    CHECK(selectedRow==1,"up selects previous row");
    fpsStatus.store(-1); tick(2163,0); tick(2164,8);
    CHECK(fovDegrees==56,"camera controls remain independent of FPS module failure"); fpsStatus.store(1);
    tick(2165,0); tick(2170,16);
    CHECK(flag(&word,doneFlag) && !qualityText(text,sizeof(text)) && qualityCapture(),"Back signals ESD completion and hides panel immediately");
    setFlag(&word,requestFlag,false); setFlag(&word,doneFlag,false); tick(2340,16);
    CHECK(!qualityCapture() && !editing && !g_external_menu_active.load(),"capture drains then releases after Back");
    setFlag(&word,requestFlag,true); tick(2400,0); tick(2410,0,false);
    CHECK(!editing && flag(&word,doneFlag) && !flag(&word,readyFlag),"death exits safely");
    setFlag(&word,requestFlag,false); setFlag(&word,doneFlag,false); tick(2600,0);
    setFlag(&word,requestFlag,true); tick(2610,0); tick(2620,0,true,false);
    CHECK(!editing && flag(&word,doneFlag) && !flag(&word,readyFlag),"renderer or input failure exits safely");
    setFlag(&word,requestFlag,false); setFlag(&word,doneFlag,false); tick(2800,0);
    setFlag(&word,requestFlag,true); tick(2810,0); setFlag(&word,requestFlag,false); tick(2820,0);
    CHECK(!editing && !qualityText(text,sizeof(text)),"ESD interruption closes panel without a leftover overlay");
    setFlag(&word,requestFlag,true); tick(3000,0,true,true,true,99);
    CHECK(!editing && !flag(&word,requestFlag) && !flag(&word,doneFlag),"loading or character replacement clears stale bridge state");
    tick(3200,0,true,true,true,99); CHECK(!qualityCapture(),"capture released after interruption");
    DeleteFileW(saved); VirtualFree(fixture,0,MEM_RELEASE); UnmapViewOfFile(image); CloseHandle(mapping); CloseHandle(file);
    puts("PASS: camera target signatures, executable scaling stubs, defaults/bounds, row selection, persistence/failure handling; Equipment default on Start opening/reopening, reference panel bridge, FPS limits/persistence, Back/death/loading/render failure cleanup, input release, signatures and live clock.");
    return 0;
}
