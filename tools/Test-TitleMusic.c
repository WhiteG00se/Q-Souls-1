// Silent isolated regression checks; never starts or controls the game.
#include "../runtime/TitleMusic.c"
#include <math.h>
#define CHECK(ok, label) do { if (!(ok)) { fprintf(stderr, "FAIL: %s\n", label); return 1; } } while (0)
#define API(lib, name, type) ((type)(void *)GetProcAddress(lib, name))
typedef int (__stdcall *CreateEvents)(void **);
typedef int (__stdcall *GetSystem)(void *, void **);
typedef int (__stdcall *SetOutput)(void *, int);
typedef int (__stdcall *InitEvents)(void *, int, unsigned int, void *, unsigned int);
typedef int (__stdcall *SetVolume)(void *, float);
typedef int (__stdcall *GetVolume)(void *, float *);
typedef int (__stdcall *SetFileSystem)(void *, void *, void *, void *, void *, void *, void *, int);
static HMODULE core;
static int (__stdcall *normalInit)(void *, int, unsigned int, void *);
static unsigned int rejectedOpens;
static int __stdcall rejectOpen(const void *name, int unicode, unsigned int *size, void **handle, void **userdata)
{
    (void)name; (void)unicode; (void)size; (void)handle; (void)userdata;
    ++rejectedOpens;
    return 23;
}
static int __stdcall unusedClose(void *handle, void *userdata)
{ (void)handle; (void)userdata; return 0; }
static int __stdcall unusedRead(void *handle, void *buffer, unsigned int size, unsigned int *read, void *userdata)
{ (void)handle; (void)buffer; (void)size; (void)userdata; *read = 0; return 23; }
static int __stdcall unusedSeek(void *handle, unsigned int position, void *userdata)
{ (void)handle; (void)position; (void)userdata; return 0; }
static int __stdcall silentInit(void *system, int channels, unsigned int flags, void *extra)
{
    int result = API(core, "FMOD_System_SetOutput", SetOutput)(system, 2);
    if (!result) result = normalInit(system, channels, flags, extra);
    if (!result) result = API(core, "FMOD_System_SetFileSystem", SetFileSystem)(system,
        (void *)rejectOpen, (void *)unusedClose, (void *)unusedRead, (void *)unusedSeek, NULL, NULL, 2048);
    return result;
}
static void * __stdcall testOpen(const char *name, uint32_t flags)
{ (void)name; (void)flags; static char movie; return &movie; }
static void __stdcall testClose(void *movie) { (void)movie; }
static int __stdcall testSound(void *movie, int enabled) { (void)movie; (void)enabled; return 0; }

int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 4, "arguments: game directory, WAV, temporary log");
    CHECK(SetDllDirectoryW(argv[1]), "DLL search directory");
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\fmodex64.dll", argv[1]);
    core = LoadLibraryW(path);
    swprintf(path, MAX_PATH, L"%ls\\fmod_event64.dll", argv[1]);
    HMODULE events = LoadLibraryW(path);
    CHECK(core && events && bindMixer(), "legacy FMOD exports");
    wcscpy(trackPath, argv[2]); wcscpy(logPath, argv[3]);
    InitializeCriticalSection(&audioLock);
    changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    realGetCategory = (void *)GetProcAddress(events, "FMOD_EventSystem_GetCategory");
    realReleaseEvents = (void *)GetProcAddress(events, "FMOD_EventSystem_Release");
    normalInit = systemInit; systemInit = silentInit;
    realOpen = testOpen; realClose = testClose; soundOnOff = testSound;

    // Model the options manager independently of the zero-initialized FMOD values.
    BYTE options[16] = {0}, manager[0x60] = {0};
    SetEnvironmentVariableW(L"QSOULS_SAVED_AUDIO", L"0,0,0");
    readStartupAudio();
    CHECK(titleGain() == 0.0f, "saved mute is respected before profile load");
    SetEnvironmentVariableW(L"QSOULS_SAVED_AUDIO", L"7,7,7");
    readStartupAudio();
    // The real failure: valid live options exist, but all three are still zero.
    options[0x0c] = options[0x0d] = options[0x0e] = 0;
    uintptr_t optionsPointer = (uintptr_t)options, managerPointer = (uintptr_t)manager;
    memcpy(manager + 0x58, &optionsPointer, sizeof(optionsPointer));
    optionsGlobal = (uintptr_t)&managerPointer;
    // No game event system, category lookup, or button event exists yet.
    openMovie("movww\\frpg_title.bik", 0);
    CHECK(titleOverlayVisible(), "version visible on title entry");
    updateMusic();
    CHECK(musicChannel && titleSystem && !musicGroup, "playback starts without the game's mixer");
    CHECK(!rejectedOpens, "memory stream bypasses file callbacks");
    float gain;
    CHECK(!API(core, "FMOD_Channel_GetVolume", GetVolume)(musicChannel, &gain), "initial volume");
    CHECK(fabsf(gain - 0.28f) < 0.0001f, "saved music seven is audible while LIVE OPTIONS ARE ALL ZERO");
    CHECK(!optionsLoaded, "zero-filled live options remain pending");
    void *firstChannel = musicChannel;
    closeMovie(titleMovie); updateMusic();
    CHECK(titleOverlayVisible(), "version remains visible after short title video");
    CHECK(musicChannel == firstChannel, "music continues after short title video closes");

    void *eventSystem = NULL, *system = NULL, *category = NULL;
    CHECK(!API(events, "FMOD_EventSystem_Create", CreateEvents)(&eventSystem), "event system creation");
    CHECK(!API(events, "FMOD_EventSystem_GetSystemObject", GetSystem)(eventSystem, &system), "game core");
    CHECK(!API(core, "FMOD_System_SetOutput", SetOutput)(system, 2), "silent game backend");
    CHECK(!API(events, "FMOD_EventSystem_Init", InitEvents)(eventSystem, 32, 0, NULL, 0), "game mixer initialization");
    CHECK(!getCategory(eventSystem, "master", &category) && musicGroup, "delayed music-setting capture");
    CHECK(!realGetCategory(eventSystem, "music", &category), "music category");
    options[0x0c] = options[0x0d] = options[0x0e] = 7;
    CHECK(!API(events, "FMOD_EventCategory_SetVolume", SetVolume)(category, 0.0f), "startup category muted");
    CHECK(!API(core, "FMOD_ChannelGroup_SetVolume", SetVolume)(musicGroup, 0.0f), "startup bus muted");
    updateMusic();
    CHECK(!API(core, "FMOD_Channel_GetVolume", GetVolume)(musicChannel, &gain), "prompt volume");
    CHECK(fabsf(gain - 0.28f) < 0.0001f, "music level seven is audible while BOTH FMOD values are zero");
    const int selectedLevels[] = {10, 3, 0};
    const float levels[] = {1.0f, 0.3f, 0.0f};
    for (size_t i = 0; i < sizeof(levels)/sizeof(*levels); ++i) {
        options[0x0c] = (BYTE)selectedLevels[i];
        // Keep both mixer values muted: only the actual selected option should matter.
        for (int tick = 0; tick < 6; ++tick) { updateMusic(); Sleep(25); }
        float audible = -1;
        CHECK(!API(core, "FMOD_Channel_GetAudibility", GetVolume)(musicChannel, &audible), "audibility readback");
        printf("Music level %.2f: audibility %.4f\n", levels[i], audible);
        CHECK(fabsf(audible - 0.4f * levels[i]) < 0.01f, "live music scaling including mute");
        CHECK(musicChannel == firstChannel, "setting capture does not restart the track");
    }
    options[0x0c] = options[0x0d] = options[0x0e] = 0;
    updateMusic();
    CHECK(optionsLoaded && titleGain() == 0.0f, "all-zero live settings honored AFTER initialization");
    CHECK(!releaseEvents(eventSystem), "mixer teardown");
    CHECK(!musicChannel && !musicSound && !titleSystem && !trackData && !musicGroup, "resources released");
    CHECK(!titleOverlayVisible(), "version hidden after title state is cleared");
    openMovie("movww\\opening.bik", 0);
    CHECK(!titleOverlayVisible(), "version hidden during other cinematics");
    openMovie("movww\\frpg_title.bik", 0);
    CHECK(titleOverlayVisible(), "version restored on returning to title");
    uintptr_t camera[13] = {0}, player[13] = {0}, cameraPointer = (uintptr_t)camera;
    camera[12] = (uintptr_t)player; player[12] = 1;
    cameraGlobal = (uintptr_t)&cameraPointer;
    CHECK(!titleOverlayVisible(), "version hidden immediately when gameplay loads");
    updateMusic();
    CHECK(!InterlockedCompareExchange(&active, 0, 0), "gameplay clears title state");
    cameraGlobal = 0;
    openMovie("movww\\frpg_title.bik", 0);
    CHECK(titleOverlayVisible(), "version restored after gameplay and title return");
    CloseHandle(changed); DeleteCriticalSection(&audioLock);
    puts("PASS: zero-filled startup options, saved mute, profile handoff, 40 percent gain, live scaling, mute, teardown.");
    return 0;
}
