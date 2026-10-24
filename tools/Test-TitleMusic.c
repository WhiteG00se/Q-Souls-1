// Silent isolated regression checks; never starts or controls the game.
#include "../runtime/TitleMusic.c"
#include <math.h>
#define CHECK(ok, label) do { if (!(ok)) { fprintf(stderr, "FAIL: %s\n", label); return 1; } } while (0)
#define API(lib, name, type) ((type)(void *)GetProcAddress(lib, name))
typedef int (__stdcall *CreateEvents)(void **);
typedef int (__stdcall *GetSystem)(void *, void **);
typedef int (__stdcall *SetOutput)(void *, int);
typedef int (__stdcall *SetVolume)(void *, float);
typedef int (__stdcall *GetVolume)(void *, float *);
typedef int (__stdcall *SetFileSystem)(void *, void *, void *, void *, void *, void *, void *, int);
static HMODULE core;
static int (__stdcall *normalInit)(void *, int, unsigned int, void *);
static unsigned int rejectedOpens;
static int movieMuteCalls;
static BOOL releasedGameSystem, earlyOutputClosed, concurrentUpdates;
static int (*normalRelease)(void *);
static int (*normalEventInit)(void *, int, unsigned int, void *, unsigned int);
static int (*normalEventUpdate)(void *);
static int (*normalUpdate)(void *);
static volatile LONG updating;
static volatile LONG coreUpdates;
static BOOL checkNativeMovie, nativeOpenSafe, nativeCloseSafe;
static int simulatedDevice;
static BOOL failSharedStream;
static int (__stdcall *normalCreateStream)(void *, const char *, unsigned int, void *, void **);

static int __stdcall createStream(void *system, const char *data, unsigned int mode, void *info, void **sound)
{
    if (failSharedStream && system == gameSystem) return 23;
    return normalCreateStream(system, data, mode, info, sound);
}

static int __stdcall observeRelease(void *system)
{
    if (system == gameSystem) releasedGameSystem = TRUE;
    return normalRelease(system);
}
static int __stdcall observeEventInit(void *system, int channels, unsigned int flags, void *extra, unsigned int eventFlags)
{
    earlyOutputClosed = !titleSystem && !musicChannel && !musicSound && !ownsTitleSystem;
    // Model a headphone connection during game mixer initialization.
    simulatedDevice = 2;
    return normalEventInit(system, channels, flags, extra, eventFlags);
}
static int __stdcall failEventInit(void *system, int channels, unsigned int flags, void *extra, unsigned int eventFlags)
{
    (void)system; (void)channels; (void)flags; (void)extra; (void)eventFlags;
    return 23;
}
static int __stdcall simulateDriver(void *system, int driver)
{
    if (system != gameSystem || (musicChannel && titleSystem != system)) return 23;
    simulatedDevice = driver;
    return 0;
}
static int __stdcall observeUpdate(void *system)
{
    InterlockedIncrement(&coreUpdates);
    if (InterlockedIncrement(&updating) != 1) concurrentUpdates = TRUE;
    Sleep(1);
    int result = normalUpdate(system);
    InterlockedDecrement(&updating);
    return result;
}
static int __stdcall observeEventUpdate(void *system)
{
    if (InterlockedIncrement(&updating) != 1) concurrentUpdates = TRUE;
    Sleep(1);
    int result = normalEventUpdate(system);
    InterlockedDecrement(&updating);
    return result;
}
static DWORD WINAPI workerUpdates(void *unused)
{
    (void)unused;
    for (int i = 0; i < 40; ++i) updateMusic();
    return 0;
}
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
static DWORD WINAPI nativeMovieCallback(void *unused)
{
    (void)unused;
    return musicEventSystem ? (DWORD)updateEvents(musicEventSystem) : 0;
}
static BOOL nativeMovieAudioReady(void)
{
    int muted = TRUE;
    LONG before = coreUpdates;
    BOOL readyForMovie = !active && !titleOverlayVisible() && !musicChannel && !titleSystem &&
        !musicSuppressed && !resumePosition && musicCategory &&
        !categoryGetMute(musicCategory, &muted) && !muted;
    // Model a movie API waiting for an audio callback on another thread.
    HANDLE callback = CreateThread(NULL, 0, nativeMovieCallback, NULL, 0, NULL);
    if (!callback) return FALSE;
    DWORD wait = WaitForSingleObject(callback, 2000), result = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeThread(callback, &result);
    CloseHandle(callback);
    return readyForMovie && wait == WAIT_OBJECT_0 && !result && before == coreUpdates;
}
static void * __stdcall testOpen(const char *name, uint32_t flags)
{
    static char movies[32]; static unsigned int next;
    if (checkNativeMovie) nativeOpenSafe = nativeMovieAudioReady() &&
        !strcmp(name, "movww\\frpg_opening.bik") && flags == 0x1234;
    return &movies[next++ % 32];
}
static void __stdcall testClose(void *movie)
{
    (void)movie;
    if (checkNativeMovie) nativeCloseSafe = nativeMovieAudioReady();
}
static int __stdcall testSound(void *movie, int enabled)
{ (void)movie; if (!enabled) ++movieMuteCalls; return 0; }

int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 4, "arguments: game directory, WAV, temporary log");
    CHECK(SetDllDirectoryW(argv[1]), "DLL search directory");
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\DarkSoulsRemastered.exe", argv[1]);
    HANDLE executable = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(executable != INVALID_HANDLE_VALUE, "open game executable as data");
    HANDLE image = CreateFileMappingW(executable, NULL, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, NULL);
    CHECK(image, "map game imports without execution");
    BYTE *base = MapViewOfFile(image, FILE_MAP_READ, 0, 0, 0);
    CHECK(base, "read installed imports");
    const char *eventImports[] = {
        "?init@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@HIPEAXI@Z",
        "?update@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@XZ",
        "?release@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@XZ",
        "?getCategory@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDPEAPEAVEventCategory@2@@Z"
    };
    for (size_t i = 0; i < sizeof(eventImports) / sizeof(*eventImports); ++i)
        CHECK(findImportInImage(base, "fmod_event64.dll", eventImports[i]), "required event hook exists in installed game");
    CHECK(findImportInImage(base, "fmodex64.dll", "?setDriver@System@FMOD@@QEAA?AW4FMOD_RESULT@@H@Z"),
          "required output-device hook exists in installed game");
    UnmapViewOfFile(base); CloseHandle(image); CloseHandle(executable);
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
    normalEventInit = (void *)GetProcAddress(events, "FMOD_EventSystem_Init");
    realInitEvents = observeEventInit;
    normalEventUpdate = (void *)GetProcAddress(events, "FMOD_EventSystem_Update");
    realUpdateEvents = observeEventUpdate;
    realSetDriver = simulateDriver;
    normalRelease = systemRelease; systemRelease = observeRelease;
    normalUpdate = systemUpdate; systemUpdate = observeUpdate;
    normalCreateStream = systemCreateStream; systemCreateStream = createStream;
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
    CHECK(ownsTitleSystem && !gameMixerSeen, "early playback owns its output");
    CHECK(!channelSetPosition(musicChannel, 1234, 1), "set early playback position");
    unsigned int handoffPosition = 0;
    CHECK(!channelGetPosition(musicChannel, &handoffPosition, 1), "read early playback position");
    simulatedDevice = 1; // Speakers before the game's mixer initializes.

    void *eventSystem = NULL, *system = NULL, *category = NULL;
    CHECK(!API(events, "FMOD_EventSystem_Create", CreateEvents)(&eventSystem), "event system creation");
    CHECK(!API(events, "FMOD_EventSystem_GetSystemObject", GetSystem)(eventSystem, &system), "game core");
    CHECK(!API(core, "FMOD_System_SetOutput", SetOutput)(system, 2), "silent game backend");
    CHECK(!API(core, "FMOD_System_SetFileSystem", SetFileSystem)(system,
        (void *)rejectOpen, (void *)unusedClose, (void *)unusedRead, (void *)unusedSeek, NULL, NULL, 2048),
        "game mixer rejects external file opens");
    failSharedStream = TRUE;
    CHECK(!initEvents(eventSystem, 32, 0, NULL, 0), "hooked game mixer initialization");
    CHECK(earlyOutputClosed, "independent output closed before game initialization");
    CHECK(gameSystem == system && !musicChannel && !titleSystem && !ownsTitleSystem,
          "shared stream failure never falls back to independent output");
    CHECK(resumePosition >= 1234, "stream failure retains handoff position for retry");
    failSharedStream = FALSE;
    updateMusic();
    CHECK(!rejectedOpens, "shared memory stream also bypasses game file callbacks");
    CHECK(gameMixerSeen && gameSystem == system && titleSystem == system && !ownsTitleSystem,
          "custom theme and game share one mixer after headphone connection");
    CHECK(!channelGetPosition(musicChannel, &handoffPosition, 1) && handoffPosition >= 1234 && handoffPosition < 1600,
          "handoff preserves playback position");
    CHECK(simulatedDevice == 2 && !setDriver(system, 3) && simulatedDevice == 3 && titleSystem == system,
          "device change after handoff cannot split custom and game output");
    firstChannel = musicChannel;
    CHECK(!getCategory(eventSystem, "master", &category) && musicGroup, "delayed music-setting capture");
    CHECK(!realGetCategory(eventSystem, "music", &category), "music category");
    int muted = FALSE;
    CHECK(musicSuppressed && !categoryGetMute(category, &muted) && muted, "game music muted during character creation");
    void *master = NULL;
    CHECK(!realGetCategory(eventSystem, "master", &master) && !categoryGetMute(master, &muted) && !muted,
          "menu sounds remain unmuted");
    CHECK(!categorySetMute(category, FALSE) && !updateEvents(eventSystem) &&
          !categoryGetMute(category, &muted) && muted, "suppression reapplied before game event update");
    options[0x0c] = options[0x0d] = options[0x0e] = 7;
    CHECK(!API(events, "FMOD_EventCategory_SetVolume", SetVolume)(category, 0.0f), "startup category muted");
    CHECK(!API(core, "FMOD_ChannelGroup_SetVolume", SetVolume)(musicGroup, 0.0f), "startup bus muted");
    updateMusic();
    CHECK(!API(core, "FMOD_Channel_GetVolume", GetVolume)(musicChannel, &gain), "prompt volume");
    CHECK(fabsf(gain - 0.28f) < 0.0001f, "music level seven is audible while BOTH FMOD values are zero");
    HANDLE worker = CreateThread(NULL, 0, workerUpdates, NULL, 0, NULL);
    CHECK(worker, "concurrent update worker");
    for (int tick = 0; tick < 40; ++tick) CHECK(!updateEvents(eventSystem), "game event update");
    CHECK(WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0 && !concurrentUpdates,
          "worker and game mixer updates serialize");
    CloseHandle(worker);
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
    options[0x0c] = 7;
    updateMusic();
    CHECK(!API(events, "FMOD_EventCategory_SetVolume", SetVolume)(category, 0.7f) &&
          !API(core, "FMOD_ChannelGroup_SetVolume", SetVolume)(musicGroup, 0.7f), "native cinematic music volume");
    int mutedBeforeMovie = movieMuteCalls;
    checkNativeMovie = TRUE;
    void *opening = openMovie("movww\\frpg_opening.bik", 0x1234);
    CHECK(opening && nativeOpenSafe && movieMuteCalls == mutedBeforeMovie && !titleOverlayVisible(),
          "original audio restored before cinematic opens, movie arguments unchanged, callbacks unblocked");
    LONG beforeMovieUpdates = coreUpdates;
    updateMusic();
    CHECK(!musicChannel && !active && !musicSuppressed && !titleSystem,
          "opening cinematic has exclusive original audio");
    CHECK(!groupGetVolume(musicGroup, &gain) && fabsf(gain - 0.7f) < 0.0001f,
          "native music volume remains unchanged");
    CHECK(!updateEvents(eventSystem) && beforeMovieUpdates == coreUpdates,
          "custom worker does not service mixer while native cinematic plays");
    uintptr_t camera[13] = {0}, player[13] = {0}, cameraPointer = (uintptr_t)camera;
    camera[12] = (uintptr_t)player; player[12] = 1;
    cameraGlobal = (uintptr_t)&cameraPointer;
    updateMusic();
    CHECK(!musicChannel && !active, "camera preparation cannot restart custom music during cinematic");
    closeMovie(opening);
    CHECK(nativeCloseSafe, "movie close allows audio callbacks without holding mixer lock");
    checkNativeMovie = FALSE;
    updateMusic();
    CHECK(!active && !musicChannel && !titleSystem && !musicSuppressed &&
          !categoryGetMute(category, &muted) && !muted, "gameplay retains original audio after cinematic");
    CHECK(!releasedGameSystem && !normalUpdate(system), "game mixer remains owned and usable by game");
    cameraGlobal = 0;
    CHECK(!categorySetMute(category, TRUE), "preexisting game music mute");
    openMovie("movww\\frpg_title.bik", 0); updateMusic();
    CHECK(titleOverlayVisible() && musicChannel && titleSystem == system, "title return reuses game output");
    mutedBeforeMovie = movieMuteCalls;
    openMovie("movww\\frpg_goodending.bik", 0);
    CHECK(movieMuteCalls == mutedBeforeMovie && !active && !musicChannel &&
          !categoryGetMute(category, &muted) && muted, "unrelated movie unchanged and prior mute restored");
    openMovie("movww\\frpg_title.bik", 0); updateMusic();
    CHECK(!releaseEvents(eventSystem), "mixer teardown");
    CHECK(!musicChannel && !musicSound && !titleSystem && !trackData && !musicGroup, "resources released");
    CHECK(!titleOverlayVisible(), "version hidden after title state is cleared");
    openMovie("movww\\opening.bik", 0);
    CHECK(!titleOverlayVisible(), "version hidden during other cinematics");
    openMovie("movww\\frpg_title.bik", 0);
    CHECK(titleOverlayVisible(), "version restored on returning to title");
    cameraGlobal = (uintptr_t)&cameraPointer;
    CHECK(!titleOverlayVisible(), "version hidden immediately when gameplay loads");
    updateMusic();
    CHECK(!InterlockedCompareExchange(&active, 0, 0), "gameplay clears title state");
    cameraGlobal = 0;
    openMovie("movww\\frpg_title.bik", 0);
    CHECK(titleOverlayVisible(), "version restored after gameplay and title return");
    updateMusic();
    CHECK(!musicChannel && !titleSystem, "teardown never falls back to independent output");
    CHECK(!API(events, "FMOD_EventSystem_Create", CreateEvents)(&eventSystem), "replacement event system");
    realInitEvents = failEventInit;
    CHECK(initEvents(eventSystem, 32, 0, NULL, 0) == 23, "initialization failure returned unchanged");
    updateMusic();
    CHECK(!musicChannel && !titleSystem && gameMixerSeen, "failed initialization cannot create split output");
    realInitEvents = observeEventInit;
    CHECK(!eventsGetSystem(eventSystem, &system) &&
          !API(core, "FMOD_System_SetOutput", SetOutput)(system, 2), "replacement silent backend");
    CHECK(!initEvents(eventSystem, 32, 0, NULL, 0) && titleSystem == gameSystem && musicChannel,
          "successful reinitialization recovers shared playback");
    CHECK(!releaseEvents(eventSystem) && !releasedGameSystem, "replacement teardown preserves ownership");
    CloseHandle(changed); DeleteCriticalSection(&audioLock);
    puts("PASS: startup gain, shared-output handoff, device changes, serialized updates, exclusive music, cinematics, restoration, teardown and recovery.");
    return 0;
}
