#pragma once

#include <windows.h>

extern HWND G_HWND;

typedef struct DXSample DXSample;

int Win32App_Run(DXSample* const pSample, const HINSTANCE hInstance, const int nCmdShow);
