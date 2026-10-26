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
    CHECK(qualityDefaultFps==120 && !qualityValidFps(999),"default and invalid saved value");
    CreateDirectoryW(argv[2], nullptr);
    CHECK(swprintf(settingsPath,MAX_PATH,L"%ls\\quality.ini",argv[2])>0,"fixture settings path");
    volatile LONG word=0; fpsStatus.store(1); g_target_fps.store(120);
    auto tick=[&](ULONGLONG now,unsigned buttons,bool alive=true,bool healthy=true,bool focused=true,uintptr_t character=42) {
        tickPanel(now,&word,character,alive,healthy,focused,buttons);
    };
    tick(1000,0); CHECK(flag(&word,readyFlag) && !qualityCapture(),"healthy closed panel advertises availability without capture");
    setFlag(&word,requestFlag,true); tick(1010,8);
    char text[512]; CHECK(qualityText(text,sizeof(text)) && strstr(text,"FPS Cap < 120 >") && qualityCapture() && g_external_menu_active.load(),"bridge opens the reference-style panel");
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
    tick(2160,0); tick(2170,16);
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
    puts("PASS: Equipment default on Start opening/reopening, reference panel bridge, FPS limits/persistence, Back/death/loading/render failure cleanup, input release, signatures and live clock.");
    return 0;
}
