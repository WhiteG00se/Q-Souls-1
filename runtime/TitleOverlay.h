#ifndef QSOULS_TITLE_OVERLAY_H
#define QSOULS_TITLE_OVERLAY_H
#include <windows.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
DWORD initializeTitleOverlay(const wchar_t *root, BOOL (*visible)(void), void (*log)(const char *));
void setRespecOverlayCallbacks(BOOL (*snapshot)(char *, size_t), void (*frame)(HWND, BOOL));
#ifdef __cplusplus
}
#endif
#endif
