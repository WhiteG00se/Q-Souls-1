#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "QualitySettings.h"
#include "QualityModel.h"
#include "NativeCalls.h"
#include "../third_party/ds1-fps-unlock/src/dsr/state.h"
#include "../third_party/ds1-fps-unlock/src/dsr/profile.h"
#include "../third_party/ds1-fps-unlock/src/dsr/patches.h"
#include "../third_party/ds1-fps-unlock/src/dsr/bonfire.h"
#include "../third_party/ds1-fps-unlock/src/common/log.h"

namespace {
BYTE *gameImage;
void *(*findWidget)(void *, const wchar_t *);
void (*registerButton)(void *, void *, int);
unsigned char (*menuQuery)(void *, int);
int (*getRegister)(int);
void (*setRegister)(int, int);
void (*originalMessage)(void *, int, void *);
void (*originalEvent)(void *, int, int);
void (*originalInput)(void *);
void (*originalConfirm)(void *);
void (*originalInit)(void *);
void (*originalCursor)(void *);
void (*playSound)(int);
bool mouseDown;
NativeCalls hooks[3];
std::atomic<int> page{0}, fpsStatus{0}, saveFailed{0};
std::atomic<ULONGLONG> menuTick{0};
wchar_t settingsPath[MAX_PATH], fpsLogPath[MAX_PATH];
void (*report)(const char *);

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
bool saveLimit(unsigned fps)
{
    if (!writableFilePath(settingsPath)) return false;
    wchar_t value[16]; swprintf(value, 16, L"%u", fps);
    return WritePrivateProfileStringW(L"Quality", L"FPSLimit", value, settingsPath) != FALSE;
}
void moveLimit(int direction)
{
    if (fpsStatus.load() != 1) return;
    unsigned fps = qualityNextFps(g_target_fps.load(), direction);
    // Save first: a failed save leaves both the displayed and active limit unchanged.
    if (!saveLimit(fps)) { saveFailed.store(1); return; }
    saveFailed.store(0);
    g_target_fps.store(fps);
    playSound(0);
}
void openPage(int next)
{
    page.store(next); saveFailed.store(0); menuTick.store(GetTickCount64());
    mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; playSound(100);
}
void confirm(void *menu)
{
    if (page.load()) return;
    if (getRegister(0x4b) == 4) openPage(1);
    else originalConfirm(menu);
}
void mouseInput()
{
    HWND window = GetForegroundWindow(); DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool clicked = down && !mouseDown; mouseDown = down;
    if (!clicked || pid != GetCurrentProcessId()) return;
    RECT rect; POINT point;
    if (!GetClientRect(window, &rect) || !GetCursorPos(&point) || !ScreenToClient(window, &point)) return;
    float scale = (float)rect.bottom / 1080.0f;
    if ((float)rect.right / 1920.0f < scale) scale = (float)rect.right / 1920.0f;
    int font = (int)(26 * scale + 0.5f); if (font < 8) font = 8;
    int left = (rect.right - font * 32) / 2, top = (rect.bottom - font * 13) / 2;
    if (point.x >= left + font && point.x < left + font * 31 &&
        point.y >= top + font * 3 && point.y < top + font * 5) {
        if (page.load() == 1) openPage(2);
        else moveLimit(1);
    }
}
void input(void *menu)
{
    menuTick.store(GetTickCount64());
    int current = page.load();
    if (!current) { originalInput(menu); return; }
    // The Start bar uses its own mapped confirm action. The child pages also
    // accept the standard menu confirm, retaining native keyboard/controller bindings.
    bool back = menuQuery(menu, 0x97) || menuQuery(menu, 0xb5);
    bool left = menuQuery(menu, 0x89) != 0;
    bool right = menuQuery(menu, 0x88) != 0;
    bool accept = menuQuery(menu, 0xb4) || menuQuery(menu, 0x96);
    if (back) { page.store(current - 1); playSound(200); }
    else if (current == 1 && accept) openPage(2);
    else if (current == 2 && left) moveLimit(-1);
    else if (current == 2 && (right || accept)) moveLimit(1);
    else mouseInput();
}
void event(void *menu, int kind, int id)
{
    if (page.load()) {
        // Block mouse activation of the covered category bar; right-click goes back.
        if (kind == 6) { page.fetch_sub(1); playSound(200); }
        return;
    }
    // ID 4 already belongs to the native background listener. Give Q its own ID.
    if (id == 100 && (kind == 2 || kind == 8) && getRegister(8) == 8) {
        int previous = getRegister(0x4b); setRegister(0x4b, 4); originalCursor(menu);
        if (kind == 2) openPage(1);
        else if (previous != 4) playSound(0);
        return;
    }
    originalEvent(menu, kind, id);
}
void message(void *menu, int id, void *value)
{
    if (id == 1001) page.store(0);
    originalMessage(menu, id, value);
}
void initializeBar(void *menu)
{
    page.store(0); originalInit(menu);
    void *button = findWidget(menu, L"cursor0_4");
    if (button) registerButton(menu, button, 100);
}
BOOL overlayText(char *text, size_t capacity)
{
    int current = page.load();
    if (!current || GetTickCount64() - menuTick.load() > 250) return FALSE;
    if (current == 1) {
        snprintf(text, capacity, "Q Souls\n\n> Quality settings\n\nConfirm / Click: open\nBack: return to Start menu");
        return TRUE;
    }
    const char *status = fpsStatus.load() == 1 ? "Saved automatically. Disable VSync in PC settings for high FPS." :
        fpsStatus.load() < 0 ? "FPS module failed. See .local/fps.log." : "FPS module is starting...";
    if (saveFailed.load()) status = "Could not save. FPS limit was not changed.";
    snprintf(text, capacity, "Quality settings\n\nFPS limit:  < %u FPS >\n\n"
        "Left / Right: change    Confirm / Click FPS: next\nBack: return to Q Souls\n\n%s", g_target_fps.load(), status);
    return TRUE;
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
    if (!supportedImage(image)) return ERROR_REVISION_MISMATCH;
    struct Site { unsigned rva; const char *bytes; size_t length; };
    const Site sites[] = {
        {0x66ec20, "\x4c\x89\x44\x24\x18\x57", 6},
        {0x66ece0, "\x48\x89\x5c\x24\x10\x57", 6},
        {0x66f4a0, "\x48\x89\x5c\x24\x18\x55\x56\x57", 8},
        {0x66f6a0, "\x40\x53\x48\x83\xec\x30", 6},
        {0x66e7f0, "\x48\x8b\xc4\x56\x57\x41\x56", 7},
        {0xed6020, "\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x20", 10},
        // Three independent native bounds for left/right and shoulder navigation.
        {0x66f600, "\x83\xf8\x03", 3}, {0x66f623, "\x83\xf8\x03", 3},
        {0x66f636, "\xbb\x03\x00\x00\x00", 5}
    };
    for (const Site &site : sites) if (memcmp(image + site.rva, site.bytes, site.length)) return ERROR_REVISION_MISMATCH;
    if (*(void **)(image + 0x13adf08) != image + 0x66ec20 ||
        *(void **)(image + 0x13adf48) != image + 0x66ece0) return ERROR_REVISION_MISMATCH;
    gameImage = image;
    findWidget = (decltype(findWidget))(image + 0xed6020);
    registerButton = (decltype(registerButton))(image + 0x719950);
    menuQuery = (decltype(menuQuery))(image + 0xed6750);
    playSound = (decltype(playSound))(image + 0x723620);
    getRegister = (decltype(getRegister))(image + 0x71abd0);
    setRegister = (decltype(setRegister))(image + 0x71abf0);
    originalMessage = (decltype(originalMessage))(image + 0x66ec20);
    originalEvent = (decltype(originalEvent))(image + 0x66ece0);
    originalInput = (decltype(originalInput))(image + 0x66f4a0);
    originalConfirm = (decltype(originalConfirm))(image + 0x66f6a0);
    originalInit = (decltype(originalInit))(image + 0x66e7f0);
    originalCursor = (decltype(originalCursor))(image + 0x66f1f0);
    const unsigned targets[] = {0x66f4a0, 0x66f6a0, 0x66e7f0};
    void *callbacks[] = {(void *)input, (void *)confirm, (void *)initializeBar};
    for (unsigned i = 0; i < 3; ++i) { hooks[i].target = image + targets[i]; hooks[i].callback = callbacks[i]; }
    return ERROR_SUCCESS;
}
DWORD extendBar(BYTE *image)
{
    const unsigned offsets[] = {0x66f602, 0x66f625, 0x66f637};
    for (unsigned offset : offsets) {
        if (image[offset] != 3) return ERROR_REVISION_MISMATCH;
    }
    for (unsigned offset : offsets) {
        DWORD previous, ignored;
        if (!VirtualProtect(image + offset, 1, PAGE_EXECUTE_READWRITE, &previous)) return GetLastError();
        image[offset] = 4;
        DWORD error = FlushInstructionCache(GetCurrentProcess(), image + offset, 1) ? ERROR_SUCCESS : GetLastError();
        if (!VirtualProtect(image + offset, 1, previous, &ignored) && !error) error = GetLastError();
        if (error) return error;
    }
    return ERROR_SUCCESS;
}
DWORD WINAPI startFps(void *)
{
    bool ok = patches_apply();
    fpsStatus.store(ok ? 1 : -1);
    if (ok) bonfire_start();
    if (report) report(ok ? "FPS module ready; Quality settings controls the live frame limit." : "FPS module failed; see .local/fps.log.");
    return 0;
}
}
extern "C" DWORD initializeQualitySettings(const wchar_t *root, void (*log)(const char *))
{
    DWORD error = resolveMenu((BYTE *)GetModuleHandleW(nullptr));
    if (error) return error;
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
    const unsigned counts[] = {1, 2, 1};
    for (unsigned i = 0; i < 3; ++i) {
        error = nativePrepareCalls(gameImage, &hooks[i], counts[i]);
        if (error) { for (auto &hook : hooks) nativeRestoreCalls(&hook); return error; }
    }
    for (auto &hook : hooks) if ((error = nativeApplyCalls(&hook)) != ERROR_SUCCESS) {
        for (auto &restore : hooks) nativeRestoreCalls(&restore);
        return error;
    }
    error = extendBar(gameImage);
    if (error) return error;
    const unsigned slots[] = {0x13adf08, 0x13adf48};
    void *callbacks[] = {(void *)message, (void *)event};
    for (unsigned i = 0; i < 2; ++i) {
        void **slot = (void **)(gameImage + slots[i]); DWORD previous, ignored;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return GetLastError();
        InterlockedExchangePointer(slot, callbacks[i]);
        if (!VirtualProtect(slot, sizeof(*slot), previous, &ignored)) return GetLastError();
    }
    setQualityOverlay(overlayText);
    HANDLE thread = CreateThread(nullptr, 0, startFps, nullptr, 0, nullptr);
    if (!thread) return GetLastError();
    CloseHandle(thread);
    log("Q category added to the native Start bar: Q Souls > Quality settings (120 FPS default).");
    return ERROR_SUCCESS;
}
