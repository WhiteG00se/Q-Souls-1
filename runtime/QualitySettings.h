#pragma once
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
DWORD initializeQualitySettings(const wchar_t *root, void (*log)(const char *));
void setQualityOverlay(BOOL (*text)(char *, size_t));
void setQualityFrame(void (*frame)(HWND, BOOL));
BOOL qualityCapture(void);
void qualityTick(void);
unsigned qualityButtons(void);
BOOL qualityInputHealthy(void);
DWORD initializeQualityInput(void);
#ifdef __cplusplus
}
#endif
