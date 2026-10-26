// Isolated input capture/release checks. No game or real input devices are used.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void **findImport(const char *library,const char *name) { (void)library;(void)name;return NULL; }
static BOOL replaceImport(void **slot,void *replacement) { *slot=replacement;return TRUE; }
static void logMessage(const char *text) { (void)text; }
#include "../runtime/QualityInput.h"
static BOOL capture;
static unsigned ticks;
BOOL qualityCapture(void) { return capture; }
void qualityTick(void) { ++ticks; }
#define CHECK(x,m) do { if(!(x)){fprintf(stderr,"FAIL: %s (%d)\n",m,__LINE__);return 1;} }while(0)
static SHORT WINAPI key(int code) { return code==VK_LEFT || code==VK_F12 ? (SHORT)0x8001 : 0; }
static BOOL WINAPI keys(PBYTE state) { memset(state,0x80,256);return TRUE; }
static DWORD WINAPI pad(DWORD user,XINPUT_STATE *state) {
    if(user) return ERROR_DEVICE_NOT_CONNECTED;
    memset(state,0,sizeof(*state)); state->dwPacketNumber=17; state->Gamepad.sThumbRX=12345; state->Gamepad.sThumbRY=-23456; state->Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_RIGHT;return ERROR_SUCCESS;
}
static BOOL WINAPI peek(LPMSG msg,HWND w,UINT min,UINT max,UINT remove) {
    (void)w;(void)min;(void)max;(void)remove; memset(msg,0,sizeof(*msg));msg->message=WM_KEYDOWN;msg->wParam=VK_LEFT;return TRUE;
}
static HRESULT __stdcall deviceState(void *device,DWORD size,void *state) { (void)device;memset(state,0x81,size);return S_OK; }
static HRESULT __stdcall deviceData(void *device,DWORD size,void *data,DWORD *count,DWORD flags) {
    (void)device;(void)flags;memset(data,0,size*2);*(DWORD *)data=1;*(DWORD *)((BYTE *)data+size)=0x58;*count=2;return S_OK;
}
int main(void)
{
    qualityRealKey=key;qualityRealKeys=keys;qualityRealXInput=pad;qualityRealPeek=peek;
    BYTE state[256];XINPUT_STATE controller;MSG message;
    CHECK(qualityKey(VK_LEFT)==(SHORT)0x8001,"closed keyboard passthrough");
    CHECK(qualityKeys(state) && state[VK_LEFT]==0x80,"closed keyboard snapshot passthrough");
    CHECK(!qualityXInput(0,&controller) && controller.Gamepad.wButtons==XINPUT_GAMEPAD_DPAD_RIGHT,"closed controller passthrough");
    CHECK(qualityPeek(&message,NULL,0,0,PM_REMOVE) && message.message==WM_KEYDOWN,"closed messages passthrough");
    capture=TRUE;
    CHECK(qualityKey(VK_LEFT)==0 && qualityKey(VK_F12)==(SHORT)0x8001,"active keyboard capture preserves screenshot key");
    CHECK(qualityKeys(state) && state[VK_LEFT]==0 && state[VK_F12]==0x80,"active keyboard snapshot filtered");
    CHECK(!qualityXInput(0,&controller) && !controller.Gamepad.wButtons && controller.dwPacketNumber==17 && controller.Gamepad.sThumbRX==12345 && controller.Gamepad.sThumbRY==-23456,"camera right stick passes through while menu buttons are captured");
    CHECK(qualityPeek(&message,NULL,0,0,PM_REMOVE) && message.message==WM_NULL && ticks==2,"active window messages consumed");
    CHECK(qualityButtons()==12,"panel reads original keyboard/controller input despite capture");
    void *table[11]={0};table[9]=(void *)deviceState;table[10]=(void *)deviceData;void **device=table;
    CHECK(qualityHookInput(table+9,(void *)qualityDeviceState) && qualityHookInput(table+10,(void *)qualityDeviceData),"mock DirectInput capture installed");
    CHECK(SUCCEEDED(qualityDeviceState(&device,sizeof(state),state)) && state[1]==0 && state[0x58]==0x81,"active DirectInput state filtered");
    BYTE events[40];DWORD count=2;
    CHECK(SUCCEEDED(qualityDeviceData(&device,20,events,&count,0)) && count==1 && *(DWORD *)events==0x58,"buffered DirectInput events filtered");
    capture=FALSE;
    CHECK(SUCCEEDED(qualityDeviceState(&device,sizeof(state),state)) && state[1]==0x81,"DirectInput state restored after close");
    count=2;CHECK(SUCCEEDED(qualityDeviceData(&device,20,events,&count,0)) && count==2,"buffered DirectInput restored after close");
    CHECK(qualityKey(VK_LEFT)==(SHORT)0x8001 && !qualityXInput(0,&controller) && controller.Gamepad.wButtons==XINPUT_GAMEPAD_DPAD_RIGHT,"keyboard/controller restored after close");
    CHECK(qualityPeek(&message,NULL,0,0,PM_REMOVE) && message.message==WM_KEYDOWN,"window input restored after close");
    puts("PASS: right-stick camera passes through; menu inputs are captured only while open and released after closing.");
    return 0;
}
