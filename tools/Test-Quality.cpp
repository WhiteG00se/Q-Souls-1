// Read the installed EXE as data only; exercise native dispatch with private mocks.
#include "../runtime/QualitySettings.cpp"
#include "../third_party/ds1-fps-unlock/src/dsr/live_clock.h"
extern "C" void setQualityOverlay(BOOL (*)(char *, size_t)) {}
#define CHECK(x, message) do { if (!(x)) { fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); return 1; } } while (0)
static unsigned sounds, nativeInputs, nativeConfirms, cursors, registered;
static int registers[256];
static int getReg(int index) { return registers[index]; }
static void setReg(int index, int value) { registers[index] = value; }
static int action;
static unsigned nativeEvents;
static void mockEvent(void *, int, int) { ++nativeEvents; }
static void sound(int) { ++sounds; }
static void mockInput(void *) { ++nativeInputs; }
static void mockConfirm(void *) { ++nativeConfirms; }
static void mockCursor(void *) { ++cursors; }
static unsigned char query(void *, int code) { return code == action; }
static void mockMessage(void *, int, void *) {}
static void *widget(void *, const wchar_t *name) { return !wcscmp(name, L"cursor0_4") ? (void *)1 : nullptr; }
static void button(void *, void *value, int id) { if (value == (void *)1 && id == 100) ++registered; }
int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 3, "EXE and external temporary folder required");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE, "read-only game file");
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr);
    CHECK(mapping, "non-executable mapping");
    BYTE *image = (BYTE *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CHECK(image && supportedImage(image), "supported installed Steam build");
    CHECK(*(uintptr_t *)(image + 0x13adf08) == 0x14066ec20ull, "native Start message vtable");
    BYTE *fixture = (BYTE *)VirtualAlloc(nullptr, kBuild.image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(fixture, "private image fixture"); memcpy(fixture, image, kBuild.image_size);
    *(void **)(fixture + 0x13adf08) = fixture + 0x66ec20;
    *(void **)(fixture + 0x13adf48) = fixture + 0x66ece0;
    CHECK(resolveMenu(fixture) == ERROR_SUCCESS, "all native menu signatures validated");
    const unsigned expected[] = {1,2,1};
    for (unsigned i = 0; i < 3; ++i) {
        BYTE *sites[8]; unsigned count = nativeCallSites(fixture, hooks[i].target, sites, 8);
        printf("Menu target RVA %llx: %u calls\n", (unsigned long long)(hooks[i].target-fixture), count);
        CHECK(count == expected[i], "native call-site count");
    }
    fixture[0x66f4a0] ^= 1;
    CHECK(resolveMenu(fixture) == ERROR_REVISION_MISMATCH, "changed menu code rejected"); fixture[0x66f4a0] ^= 1;
    ((IMAGE_NT_HEADERS64 *)(fixture + ((IMAGE_DOS_HEADER *)fixture)->e_lfanew))->FileHeader.TimeDateStamp ^= 1;
    CHECK(!supportedImage(fixture), "unsupported build rejected");
    LiveFpsClock clock;
    CHECK(clock.sample(1000, 900, 60) == 1000 && clock.sample(1600,900,60) == 1600, "native-rate clock");
    CHECK(clock.sample(1600,900,120) == 1600 && clock.sample(2200,900,120) == 2800, "120 FPS transition is continuous");
    CHECK(clock.sample(2200,900,30) == 2800 && clock.sample(2800,900,30) == 3100, "30 FPS transition is continuous");
    CHECK(clock.sample(3000,2900,240) == 3000, "game clock reset reanchors");
    for (unsigned fps : qualityFpsLimits) {
        CHECK(qualityValidFps(fps), "offered limit valid");
        CHECK(qualityNextFps(qualityNextFps(fps,1),-1) == fps, "forward/backward selection");
    }
    CHECK(!qualityValidFps(0) && !qualityValidFps(999) && qualityNextFps(0,1) == 120, "invalid saved limit fallback");
    CreateDirectoryW(argv[2], nullptr);
    CHECK(swprintf(settingsPath, MAX_PATH, L"%ls\\quality.ini", argv[2]) > 0, "fixture settings path");
    playSound = sound; fpsStatus.store(1); g_target_fps.store(60);
    moveLimit(1);
    CHECK(g_target_fps.load() == 120 && GetPrivateProfileIntW(L"Quality",L"FPSLimit",0,settingsPath)==120, "live limit saved and reloadable");
    wchar_t saved[MAX_PATH]; wcscpy(saved, settingsPath);
    swprintf(settingsPath, MAX_PATH, L"%ls\\missing\\quality.ini", argv[2]); moveLimit(1);
    CHECK(saveFailed.load() && g_target_fps.load() == 120, "failed save retains active limit"); wcscpy(settingsPath,saved);
    BYTE menu[0x280] = {};
    getRegister=getReg; setRegister=setReg; originalCursor=mockCursor;
    originalInput=mockInput; originalConfirm=mockConfirm; menuQuery=query;
    registers[0x4b]=4; registers[8]=8;
    page.store(0); confirm(menu);
    CHECK(page.load()==1 && nativeConfirms==0,"Q confirm opens Q Souls");
    char text[512]; CHECK(overlayText(text,sizeof(text)) && strstr(text,"Q Souls") && strstr(text,"Quality settings"),"Q Souls contains Quality settings");
    action=0xb4; input(menu); CHECK(page.load()==2,"native Start confirm enters Quality settings");
    CHECK(overlayText(text,sizeof(text)) && strstr(text,"120 FPS") && strstr(text,"return to Q Souls"),"quality child reports current FPS and parent");
    action=0x88; input(menu); CHECK(g_target_fps.load()==240 && nativeInputs==0,"native right action changes limit without bar input");
    action=0x89; input(menu); CHECK(g_target_fps.load()==120,"native left action restores prior limit");
    action=0x97; input(menu); CHECK(page.load()==1,"back returns to Q Souls");
    input(menu); CHECK(!page.load(),"back returns to Start category bar");
    originalEvent=mockEvent; action=0;
    event(menu,8,100); CHECK(registers[0x4b]==4 && cursors==1 && nativeEvents==0,"new Q mouse hover selects fifth category");
    event(menu,2,100); CHECK(page.load()==1 && nativeEvents==0,"Q mouse click opens Q Souls");
    event(menu,2,0); CHECK(nativeEvents==0,"child blocks covered bar mouse actions");
    confirm(menu); CHECK(nativeConfirms==0,"child blocks native direct confirmation");
    event(menu,6,4); CHECK(!page.load(),"mouse back returns to category bar");
    event(menu,8,0); CHECK(nativeEvents==1,"original mouse events retained");
    input(menu); CHECK(nativeInputs==1,"original bar input retained");
    registers[0x4b]=0; confirm(menu); CHECK(nativeConfirms==1,"original category selection retained");
    originalMessage=mockMessage; originalInit=mockInput; findWidget=widget; registerButton=button;
    page.store(2); initializeBar(menu); CHECK(!page.load() && registered==1,"bar init registers Q hit target and resets child");
    page.store(2); message(menu,1001,nullptr); CHECK(!page.load(),"native menu close resets Q Souls");
    page.store(1); menuTick.store(GetTickCount64()-1000); CHECK(!overlayText(text,sizeof(text)),"inactive bar hides child panel");
    CHECK(extendBar(fixture)==ERROR_SUCCESS && fixture[0x66f602]==4 && fixture[0x66f625]==4 && fixture[0x66f637]==4,"all native arrow/shoulder bounds extend from four to five categories");
    CHECK(extendBar(fixture)==ERROR_REVISION_MISMATCH,"duplicate or changed native bounds rejected");
    CHECK(sounds > 0, "native menu feedback");
    DeleteFileW(saved); VirtualFree(fixture,0,MEM_RELEASE); UnmapViewOfFile(image); CloseHandle(mapping); CloseHandle(file);
    puts("PASS: native menu signatures, navigation/clicks, child input, FPS persistence, failure handling and continuous live clock.");
    return 0;
}
