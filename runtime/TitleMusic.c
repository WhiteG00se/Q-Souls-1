#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "TitleOverlay.h"
#include "ControllerHelper.h"
#include "IntroSkip.h"

static HMODULE self;
static HANDLE changed, ready;
static volatile LONG active;
static volatile LONG overlayActive;
static uintptr_t cameraGlobal, optionsGlobal;
static BYTE selectedAudio[3] = {10, 10, 10};
static BOOL optionsLoaded;

static void readStartupAudio(void)
{
    selectedAudio[0] = selectedAudio[1] = selectedAudio[2] = 10;
    wchar_t value[32], extra;
    unsigned int music, effects, voice;
    DWORD length = GetEnvironmentVariableW(L"QSOULS_SAVED_AUDIO", value, 32);
    if (length && length < 32 && swscanf(value, L"%u,%u,%u%lc", &music, &effects, &voice, &extra) == 3 &&
        music <= 10 && effects <= 10 && voice <= 10) {
        selectedAudio[0] = (BYTE)music; selectedAudio[1] = (BYTE)effects; selectedAudio[2] = (BYTE)voice;
    }
    optionsLoaded = FALSE;
}

static void *titleMovie;
static wchar_t trackPath[MAX_PATH], logPath[MAX_PATH];
static void * (__stdcall *realOpen)(const char *, uint32_t);
static void (__stdcall *realClose)(void *);
static int (__stdcall *soundOnOff)(void *, int);

static CRITICAL_SECTION audioLock;
static void *musicGroup, *musicSound, *musicChannel, *musicEventSystem;
static void *trackData;
static unsigned int trackLength;
static void *titleSystem;
static void *gameSystem, *musicCategory;
static BOOL ownsTitleSystem, gameMixerSeen, musicSuppressed;
static int previousMusicMute;
static unsigned int resumePosition;
static int (__stdcall *systemCreate)(void **);
static int (__stdcall *systemInit)(void *, int, unsigned int, void *);
static int (__stdcall *systemUpdate)(void *);
static int (__stdcall *systemRelease)(void *);

// FMOD 4.44 x64 sound-creation ABI. Unused options remain zero.
typedef struct {
    int size;
    unsigned int length, fileOffset;
    int channels, frequency, format;
    unsigned int decodeBufferSize;
    int initialSubsound, subsoundCount;
    void *inclusionList;
    int inclusionCount;
    void *pcmRead, *pcmSeek, *nonblockingCallback;
    const char *dlsName, *encryptionKey;
    int maxPolyphony;
    void *userData;
    int suggestedType;
    void *fileOpen, *fileClose, *fileRead, *fileSeek, *fileAsyncRead, *fileAsyncCancel;
    int speakerMap;
    void *initialSoundGroup;
    unsigned int initialSeekPosition, initialSeekUnit;
    int ignoreFileSystem, forceAspi;
    unsigned int audioQueuePolicy, minMidiGranularity;
    int nonblockingThread;
} LegacySoundInfo;
_Static_assert(sizeof(LegacySoundInfo) == 216, "Unexpected FMOD 4.44 x64 ABI");
static int (__stdcall *realGetCategory)(void *, const char *, void **);
static int (__stdcall *realReleaseEvents)(void *);
static int (__stdcall *realInitEvents)(void *, int, unsigned int, void *, unsigned int);
static int (__stdcall *realUpdateEvents)(void *);
static int (__stdcall *realSetDriver)(void *, int);
static int (__stdcall *eventsGetSystem)(void *, void **);
static int (__stdcall *categoryGetMute)(void *, int *);
static int (__stdcall *categorySetMute)(void *, int);
static int (__stdcall *categoryGetGroup)(void *, void **);
static int (__stdcall *groupGetVolume)(void *, float *);
static int (__stdcall *systemCreateStream)(void *, const char *, unsigned int, void *, void **);
static int (__stdcall *systemPlaySound)(void *, int, void *, int, void **);
static int (__stdcall *channelSetGroup)(void *, void *);
static int (__stdcall *channelSetVolume)(void *, float);
static int (__stdcall *channelGetAudibility)(void *, float *);
static int (__stdcall *channelSetPaused)(void *, int);
static int (__stdcall *channelGetPosition)(void *, unsigned int *, unsigned int);
static int (__stdcall *channelSetPosition)(void *, unsigned int, unsigned int);
static int (__stdcall *channelStop)(void *);
static int (__stdcall *soundRelease)(void *);


static void logMessage(const char *message)
{
    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        SYSTEMTIME now;
        GetLocalTime(&now);
        char time[48];
        int length = snprintf(time, sizeof(time), "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        WriteFile(file, time, (DWORD)length, &written, NULL);
        WriteFile(file, message, (DWORD)strlen(message), &written, NULL);
        WriteFile(file, "\r\n", 2, &written, NULL);
        CloseHandle(file);
    }
}


static BOOL bindMixer(void)
{
    HMODULE core = GetModuleHandleW(L"fmodex64.dll");
    HMODULE events = GetModuleHandleW(L"fmod_event64.dll");
    if (!core || !events) return FALSE;
    categoryGetGroup = (void *)GetProcAddress(events, "FMOD_EventCategory_GetChannelGroup");
    eventsGetSystem = (void *)GetProcAddress(events, "FMOD_EventSystem_GetSystemObject");
    categoryGetMute = (void *)GetProcAddress(events, "FMOD_EventCategory_GetMute");
    categorySetMute = (void *)GetProcAddress(events, "FMOD_EventCategory_SetMute");
    groupGetVolume = (void *)GetProcAddress(core, "FMOD_ChannelGroup_GetVolume");
    systemCreate = (void *)GetProcAddress(core, "FMOD_System_Create");
    systemInit = (void *)GetProcAddress(core, "FMOD_System_Init");
    systemUpdate = (void *)GetProcAddress(core, "FMOD_System_Update");
    systemRelease = (void *)GetProcAddress(core, "FMOD_System_Release");
    systemCreateStream = (void *)GetProcAddress(core, "FMOD_System_CreateStream");
    systemPlaySound = (void *)GetProcAddress(core, "FMOD_System_PlaySound");
    channelSetGroup = (void *)GetProcAddress(core, "FMOD_Channel_SetChannelGroup");
    channelSetVolume = (void *)GetProcAddress(core, "FMOD_Channel_SetVolume");
    channelGetAudibility = (void *)GetProcAddress(core, "FMOD_Channel_GetAudibility");
    channelSetPaused = (void *)GetProcAddress(core, "FMOD_Channel_SetPaused");
    channelGetPosition = (void *)GetProcAddress(core, "FMOD_Channel_GetPosition");
    channelSetPosition = (void *)GetProcAddress(core, "FMOD_Channel_SetPosition");
    channelStop = (void *)GetProcAddress(core, "FMOD_Channel_Stop");
    soundRelease = (void *)GetProcAddress(core, "FMOD_Sound_Release");
    return eventsGetSystem && categoryGetMute && categorySetMute &&
           channelGetPosition && channelSetPosition && categoryGetGroup && groupGetVolume && systemCreate && systemInit &&
           systemUpdate && systemRelease && systemCreateStream && systemPlaySound &&
           channelSetGroup && channelSetVolume && channelGetAudibility && channelSetPaused && channelStop && soundRelease;
}

// Use Windows file I/O directly: the game's FMOD callbacks cannot open repo assets.
static DWORD readTrack(void)
{
    HANDLE file = CreateFileW(trackPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) {
        DWORD error = GetLastError();
        CloseHandle(file);
        return error;
    }
    if (size.QuadPart <= 0 || size.QuadPart > MAXDWORD) {
        CloseHandle(file);
        return ERROR_FILE_TOO_LARGE;
    }
    void *data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart);
    if (!data) { CloseHandle(file); return ERROR_NOT_ENOUGH_MEMORY; }
    DWORD read = 0, error = ERROR_SUCCESS;
    if (!ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL)) error = GetLastError();
    else if (read != (DWORD)size.QuadPart) error = ERROR_HANDLE_EOF;
    CloseHandle(file);
    if (error) { HeapFree(GetProcessHeap(), 0, data); return error; }
    trackData = data;
    trackLength = read;
    return ERROR_SUCCESS;
}

static const char *lastFailureStage;
static unsigned int lastFailureCode;
static void musicFailure(const char *stage, unsigned int code)
{
    // Avoid logging the same startup failure every worker tick.
    if (lastFailureStage == stage && lastFailureCode == code) return;
    char message[160];
    snprintf(message, sizeof(message), "Title music %s error: %u", stage, code);
    logMessage(message);
    lastFailureStage = stage;
    lastFailureCode = code;
}

// All playback operations hold audioLock so event-system teardown cannot race them.
static void stopMusic(void)
{
    if (musicChannel) { channelStop(musicChannel); musicChannel = NULL; }
    if (musicSound) { soundRelease(musicSound); musicSound = NULL; }
    // A memory stream can read this buffer until Sound::release has completed.
    if (trackData) { HeapFree(GetProcessHeap(), 0, trackData); trackData = NULL; }
    trackLength = 0;
    if (titleSystem && ownsTitleSystem) systemRelease(titleSystem);
    titleSystem = NULL;
    ownsTitleSystem = FALSE;
}

// Only the game's music category is muted. The custom channel uses its master bus.
static void suppressGameMusic(BOOL wanted)
{
    if (!musicCategory) return;
    if (wanted) {
        if (!musicSuppressed) {
            int result = categoryGetMute(musicCategory, &previousMusicMute);
            if (result) { musicFailure("read game music mute", (unsigned int)result); return; }
        }
        int result = categorySetMute(musicCategory, TRUE);
        if (!result) musicSuppressed = TRUE;
        else musicFailure("mute game music", (unsigned int)result);
    } else if (musicSuppressed) {
        int result = categorySetMute(musicCategory, previousMusicMute);
        if (!result) musicSuppressed = FALSE;
        else musicFailure("restore game music", (unsigned int)result);
    }
}

static void captureMusicCategory(void)
{
    if (!musicCategory && musicEventSystem) {
        void *category = NULL, *group = NULL;
        if (!realGetCategory(musicEventSystem, "music", &category) &&
            !categoryGetGroup(category, &group)) {
            musicCategory = category;
            musicGroup = group;
            logMessage("Connected to game music category; custom theme shares game output.");
        }
    }
    suppressGameMusic(InterlockedCompareExchange(&active, 0, 0) != 0);
}

// The live options are all zero until profile loading after the title prompt.
// Use saved settings for that interval, then follow live changes (including zero).
static float titleGain(void)
{
    uintptr_t manager = 0, options = 0;
    SIZE_T read;
    BYTE levels[3];
    BOOL valid = optionsGlobal &&
        ReadProcessMemory(GetCurrentProcess(), (void *)optionsGlobal, &manager, sizeof(manager), &read) &&
        read == sizeof(manager) && manager &&
        ReadProcessMemory(GetCurrentProcess(), (void *)(manager + 0x58), &options, sizeof(options), &read) &&
        read == sizeof(options) && options &&
        ReadProcessMemory(GetCurrentProcess(), (void *)(options + 0x0c), levels, sizeof(levels), &read) &&
        read == sizeof(levels) && levels[0] <= 10 && levels[1] <= 10 && levels[2] <= 10;
    if (valid) {
        if (levels[0] || levels[1] || levels[2] ||
            !(selectedAudio[0] || selectedAudio[1] || selectedAudio[2])) optionsLoaded = TRUE;
        if (optionsLoaded) memcpy(selectedAudio, levels, sizeof(selectedAudio));
    }
    return 0.01f * selectedAudio[0];
}

static BOOL startMusic(void)
{
    // Once game initialization begins, never reopen a separate output device.
    if (gameMixerSeen && !gameSystem) return FALSE;
    DWORD fileError = readTrack();
    if (fileError) { musicFailure("Windows file read", fileError); return FALSE; }
    LegacySoundInfo info = {0};
    info.size = sizeof(info);
    info.length = trackLength;
    const char *stage = "independent mixer creation";
    int result = 0;
    titleSystem = gameSystem;
    if (!titleSystem) {
        ownsTitleSystem = TRUE;
        result = systemCreate(&titleSystem);
        if (!result) { stage = "independent mixer initialization"; result = systemInit(titleSystem, 8, 0, NULL); }
    }
    if (!result) {
        stage = "memory stream creation";
        result = systemCreateStream(titleSystem, trackData, 0x0000080a, &info, &musicSound);
    }
    // The worker also services the shared core mixer while game menus are idle.
    if (!result) {
        stage = "channel creation";
        result = systemPlaySound(titleSystem, -1, musicSound, TRUE, &musicChannel);
    }
    if (!result) { stage = "gain"; result = channelSetVolume(musicChannel, titleGain()); }
    if (!result && resumePosition) {
        stage = "resume position";
        result = channelSetPosition(musicChannel, resumePosition, 1); // FMOD_TIMEUNIT_MS
    }
    if (!result) { stage = "unpause"; result = channelSetPaused(musicChannel, FALSE); }
    if (result) {
        musicFailure(stage, (unsigned int)result);
        stopMusic();
        return FALSE;
    }
    lastFailureStage = NULL;
    lastFailureCode = 0;
    resumePosition = 0;
    return TRUE;
}

static int __stdcall getCategory(void *system, const char *name, void **category)
{
    EnterCriticalSection(&audioLock);
    int result = realGetCategory(system, name, category);
    if (system == musicEventSystem) captureMusicCategory();
    LeaveCriticalSection(&audioLock);
    return result;
}

static int __stdcall initEvents(void *system, int channels, unsigned int flags, void *extra, unsigned int eventFlags)
{
    EnterCriticalSection(&audioLock);
    gameMixerSeen = TRUE;
    if (musicChannel) {
        int result = channelGetPosition(musicChannel, &resumePosition, 1);
        if (result) { resumePosition = 0; musicFailure("save position", (unsigned int)result); }
    }
    // Release the early output before the game's initialization can open another.
    stopMusic();
    suppressGameMusic(FALSE);
    musicSuppressed = FALSE;
    gameSystem = NULL;
    musicEventSystem = musicCategory = musicGroup = NULL;
    int result = realInitEvents(system, channels, flags, extra, eventFlags);
    if (!result) {
        musicEventSystem = system;
        int coreResult = eventsGetSystem(system, &gameSystem);
        if (coreResult) {
            gameSystem = NULL;
            musicFailure("get game mixer", (unsigned int)coreResult);
        }
        captureMusicCategory();
        if (InterlockedCompareExchange(&active, 0, 0)) startMusic();
    } else musicFailure("game mixer initialization", (unsigned int)result);
    SetEvent(changed);
    LeaveCriticalSection(&audioLock);
    return result;
}

static void updateMusic(void);

static int __stdcall updateEvents(void *system)
{
    EnterCriticalSection(&audioLock);
    if (system == musicEventSystem) updateMusic();
    int result = realUpdateEvents(system);
    LeaveCriticalSection(&audioLock);
    return result;
}

static int __stdcall setDriver(void *system, int driver)
{
    EnterCriticalSection(&audioLock);
    int result = realSetDriver(system, driver);
    LeaveCriticalSection(&audioLock);
    return result;
}

static int __stdcall releaseEvents(void *system)
{
    EnterCriticalSection(&audioLock);
    if (system == musicEventSystem) {
        stopMusic();
        suppressGameMusic(FALSE);
        musicSuppressed = FALSE;
        resumePosition = 0;
        gameSystem = musicCategory = NULL;
        musicGroup = NULL;
        musicEventSystem = NULL;
        InterlockedExchange(&active, 0);
        InterlockedExchange(&overlayActive, 0);
    }
    int result = realReleaseEvents(system);
    LeaveCriticalSection(&audioLock);
    return result;
}

// Camera pointer layout reference: JKAnderson/DSR-Gadget, DSROffsets.cs.
// Scan the installed executable instead of hard-coding a version-specific address.
static uintptr_t findCameraGlobal(void)
{
    const BYTE pattern[] = {0x48,0x8b,0x0d,0,0,0,0,0xe8,0,0,0,0,
        0x48,0x8b,0x4e,0x68,0x48,0x8b,0x05,0,0,0,0,0x48,0x89,0x48,0x60};
    const char mask[] = "xxx????x????xxxxxxx????xxxx";
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *data = base + section->VirtualAddress;
        DWORD size = section->Misc.VirtualSize;
        for (size_t i = 0; i + sizeof(pattern) <= size; ++i) {
            size_t j = 0;
            for (; j < sizeof(pattern); ++j)
                if (mask[j] != '?' && data[i+j] != pattern[j]) break;
            if (j != sizeof(pattern)) continue;
            if (found) return 0; // Refuse ambiguous layouts.
            int32_t relative;
            memcpy(&relative, data + i + 3, sizeof(relative));
            found = (uintptr_t)(data + i + 7) + relative;
        }
    }
    return found;
}

// Identify the options getter by its adjacent +0x58/+0x68/+0x60 getters.
// Validate all three reference the same manager; the shorter pattern is ambiguous.
static uintptr_t findOptionsGlobal(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    const BYTE prefix[] = {0x48, 0x8b, 0x05};
    const BYTE offsets[] = {0x58, 0x68, 0x60};
    uintptr_t found = 0;
    for (WORD n = 0; n < nt->FileHeader.NumberOfSections; ++n, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *data = base + section->VirtualAddress;
        for (size_t i = 0; i + 76 <= section->Misc.VirtualSize; ++i) {
            uintptr_t target = 0;
            size_t j = 0;
            for (; j < 3; ++j) {
                BYTE *code = data + i + 32 * j;
                const BYTE suffix[] = {0x48, 0x8b, 0x40, offsets[j], 0xc3};
                if (memcmp(code, prefix, sizeof(prefix)) || memcmp(code + 7, suffix, sizeof(suffix))) break;
                int32_t displacement;
                memcpy(&displacement, code + 3, sizeof(displacement));
                uintptr_t current = (uintptr_t)(code + 7) + displacement;
                if (j && current != target) break;
                target = current;
            }
            if (j != 3) continue;
            if (found) return 0;
            found = target;
        }
    }
    return found;
}

static BOOL gameplayLoaded(void)
{
    uintptr_t pointer = cameraGlobal;
    const size_t offsets[] = {0, 0x60, 0x60};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
        SIZE_T read;
        uintptr_t next = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)(pointer + offsets[i]),
                               &next, sizeof(next), &read) || read != sizeof(next) || !next)
            return FALSE;
        pointer = next;
    }
    return TRUE;
}

static BOOL titleOverlayVisible(void)
{
    return InterlockedCompareExchange(&overlayActive, 0, 0) != 0 &&
           InterlockedCompareExchange(&active, 0, 0) != 0 && !gameplayLoaded();
}

// Playback does not wait for the game to expose or update its music mixer.
static void updateMusic(void)
{
    EnterCriticalSection(&audioLock);
    if (InterlockedCompareExchange(&active, 0, 0) && gameplayLoaded()) {
        InterlockedExchange(&active, 0);
        InterlockedExchange(&overlayActive, 0);
        logMessage("Gameplay loaded.");
    }
    BOOL wanted = InterlockedCompareExchange(&active, 0, 0) != 0;
    captureMusicCategory();
    if (wanted && !musicChannel) {
        if (startMusic()) logMessage("Title music playing.");
    } else if (!wanted && musicChannel) {
        stopMusic();
        logMessage("Title music stopped.");
    }
    if (!wanted) resumePosition = 0;
    if (musicChannel) {
        float gain = titleGain(), audible = -1.0f;
        int volumeResult = channelSetVolume(musicChannel, gain);
        int updateResult = systemUpdate(titleSystem);
        int audibleResult = channelGetAudibility(musicChannel, &audible);
        static ULONGLONG lastReport;
        static int lastGain = -1, lastAudible = -1, lastBus = -1;
        int gainPercent = (int)(gain * 100.0f + 0.5f);
        int audiblePercent = audible < 0 ? -1 : (int)(audible * 100.0f + 0.5f);
        int bus = musicGroup != NULL;
        float busVolume = -1.0f;
        if (musicGroup) groupGetVolume(musicGroup, &busVolume);
        int busPercent = busVolume < 0 ? -1 : (int)(busVolume * 100.0f + 0.5f);
        static int lastBusPercent = -2;
        ULONGLONG now = GetTickCount64();
        if (now - lastReport >= 250 &&
            (gainPercent != lastGain || audiblePercent != lastAudible || bus != lastBus || busPercent != lastBusPercent)) {
            char message[192];
            snprintf(message, sizeof(message),
                "Title audio: music=%d%% audible=%d%% bus=%d%% live-options=%d errors=%d/%d/%d",
                gainPercent, audiblePercent, busPercent, optionsLoaded, volumeResult, updateResult, audibleResult);
            logMessage(message);
            lastReport = now; lastGain = gainPercent; lastAudible = audiblePercent; lastBus = bus; lastBusPercent = busPercent;
        }
    }
    LeaveCriticalSection(&audioLock);
}

// Stay active across the short title video's end, until gameplay actually loads.
static DWORD WINAPI musicThread(void *unused)
{
    (void)unused;
    SetEvent(ready);
    for (;;) {
        DWORD wait = WaitForSingleObject(changed, 20);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) break;
        updateMusic();
    }
    EnterCriticalSection(&audioLock);
    stopMusic();
    suppressGameMusic(FALSE);
    LeaveCriticalSection(&audioLock);
    return 0;
}
static void * __stdcall openMovie(const char *name, uint32_t flags)
{
    const char *filename = strrchr(name, '\\');
    if (!filename) filename = strrchr(name, '/');
    filename = filename ? filename + 1 : name;
    BOOL isTitle = _stricmp(filename, "frpg_title.bik") == 0;
    if (!isTitle) {
        EnterCriticalSection(&audioLock);
        // Restore native audio before Bink opens its sound stream and clock.
        InterlockedExchange(&active, 0);
        InterlockedExchange(&overlayActive, 0);
        stopMusic();
        suppressGameMusic(FALSE);
        resumePosition = 0;
        LeaveCriticalSection(&audioLock);
        SetEvent(changed);
    }
    // Bink may wait for audio callbacks; never hold our mixer lock across it.
    void *movie = realOpen(name, flags);
    if (movie && isTitle) {
        EnterCriticalSection(&audioLock);
        titleMovie = movie;
        soundOnOff(movie, 0);
        InterlockedExchange(&active, 1);
        InterlockedExchange(&overlayActive, 1);
        captureMusicCategory();
        SetEvent(changed);
        logMessage("Title movie opened.");
        LeaveCriticalSection(&audioLock);
    }
    if (movie && !isTitle) logMessage("Cinematic opened with original audio; custom theme stopped.");
    return movie;
}

static void __stdcall closeMovie(void *movie)
{
    EnterCriticalSection(&audioLock);
    if (movie == titleMovie) {
        titleMovie = NULL;
        logMessage("Title movie closed.");
    }
    LeaveCriticalSection(&audioLock);
    realClose(movie);
}

static void **findImportInImage(BYTE *base, const char *library, const char *name)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; desc->Name; ++desc) {
        if (_stricmp((char *)(base + desc->Name), library) != 0) continue;
        if (!desc->OriginalFirstThunk) return NULL;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *entry = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)entry->Name, name) == 0) return (void **)&slots->u1.Function;
        }
    }
    return NULL;
}

static void **findImport(const char *library, const char *name)
{
    return findImportInImage((BYTE *)GetModuleHandleW(NULL), library, name);
}

static BOOL replaceImport(void **slot, void *replacement)
{
    DWORD oldProtect, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) return FALSE;
    InterlockedExchangePointer(slot, replacement);
    return VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
}

#include "ParamOverride.h"
#include "SaveExtension.h"
#include "RespecRuntime.h"

// Explicit initialization lets the launcher report a failure before starting the game.
__declspec(dllexport) DWORD WINAPI InitializeQSouls(void *unused)
{
    (void)unused;
    wchar_t root[MAX_PATH];
    DWORD length = GetModuleFileNameW(self, root, MAX_PATH);
    if (!length || length >= MAX_PATH) return ERROR_BUFFER_OVERFLOW;
    wchar_t *slash = wcsrchr(root, L'\\');
    if (!slash) return ERROR_BAD_PATHNAME;
    *slash = 0;
    slash = wcsrchr(root, L'\\');
    if (!slash) return ERROR_BAD_PATHNAME;
    *slash = 0;
    if (swprintf(trackPath, MAX_PATH, L"%ls\\assets\\audio\\title-theme.wav", root) < 0 ||
        swprintf(logPath, MAX_PATH, L"%ls\\.local\\title-music.log", root) < 0)
        return ERROR_BUFFER_OVERFLOW;
    DWORD parameterError = initializeParamOverride(root);
    if (parameterError) return parameterError;
    DWORD assetError = loadRespecOverrides(root);
    if (assetError) return assetError;
    DWORD saveError = initializeSaveExtension();
    if (saveError) return saveError;
    initializeControllerHelper(root);
    readStartupAudio();
    cameraGlobal = findCameraGlobal();
    optionsGlobal = findOptionsGlobal();
    if (!cameraGlobal || !optionsGlobal) return ERROR_REVISION_MISMATCH;
    DWORD overlayError = initializeTitleOverlay(root, titleOverlayVisible, logMessage);
    if (overlayError) return overlayError;
    DWORD respecError = initializeRespec(gameplayLoaded);
    if (respecError) return respecError;
    void **openSlot = findImport("binkw64.dll", "BinkOpen"), **closeSlot = findImport("binkw64.dll", "BinkClose");
    HMODULE bink = GetModuleHandleW(L"binkw64.dll");
    soundOnOff = (void *)GetProcAddress(bink, "BinkSetSoundOnOff");
    if (!openSlot || !closeSlot || !soundOnOff) return ERROR_PROC_NOT_FOUND;
    void **categorySlot = findImport("fmod_event64.dll",
        "?getCategory@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDPEAPEAVEventCategory@2@@Z");
    void **releaseSlot = findImport("fmod_event64.dll",
        "?release@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@XZ");
    void **initSlot = findImport("fmod_event64.dll",
        "?init@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@HIPEAXI@Z");
    void **updateSlot = findImport("fmod_event64.dll",
        "?update@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@XZ");
    void **driverSlot = findImport("fmodex64.dll",
        "?setDriver@System@FMOD@@QEAA?AW4FMOD_RESULT@@H@Z");
    if (!categorySlot || !releaseSlot || !initSlot || !updateSlot || !driverSlot || !bindMixer())
        return ERROR_PROC_NOT_FOUND;
    realGetCategory = (void *)*categorySlot;
    realReleaseEvents = (void *)*releaseSlot;
    realInitEvents = (void *)*initSlot;
    realUpdateEvents = (void *)*updateSlot;
    realSetDriver = (void *)*driverSlot;
    InitializeCriticalSection(&audioLock);
    realOpen = (void *)*openSlot;
    realClose = (void *)*closeSlot;
    changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!changed || !ready) return GetLastError();
    HANDLE worker = CreateThread(NULL, 0, musicThread, NULL, 0, NULL);
    if (!worker) return GetLastError();
    CloseHandle(worker);
    if (WaitForSingleObject(ready, 15000) != WAIT_OBJECT_0) return ERROR_TIMEOUT;
    CloseHandle(ready);

    if (!replaceImport(openSlot, (void *)openMovie) ||
        !replaceImport(closeSlot, (void *)closeMovie) ||
        !replaceImport(categorySlot, (void *)getCategory) ||
        !replaceImport(initSlot, (void *)initEvents) ||
        !replaceImport(updateSlot, (void *)updateEvents) ||
        !replaceImport(driverSlot, (void *)setDriver) ||
        !replaceImport(releaseSlot, (void *)releaseEvents)) return ERROR_WRITE_FAULT;
    DWORD introError = skipIntro();
    if (introError) {
        logMessage("Intro skip initialization failed.");
        return introError;
    }
    logMessage("Startup logos skipped; title prompt awaits player input.");
    char message[128];
    snprintf(message, sizeof(message), "Title music ready (saved music=%u/10; live options pending).", selectedAudio[0]);
    logMessage(message);
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
