#ifndef QSOULS_TITLE_OVERLAY_H
#define QSOULS_TITLE_OVERLAY_H
#include <windows.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
DWORD initializeTitleOverlay(const wchar_t *root, BOOL (*visible)(void), void (*log)(const char *));
#ifdef __cplusplus
}
#endif
#endif
