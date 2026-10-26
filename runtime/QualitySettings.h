#pragma once
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
DWORD initializeQualitySettings(const wchar_t *root, void (*log)(const char *));
void setQualityOverlay(BOOL (*text)(char *, size_t));
#ifdef __cplusplus
}
#endif
