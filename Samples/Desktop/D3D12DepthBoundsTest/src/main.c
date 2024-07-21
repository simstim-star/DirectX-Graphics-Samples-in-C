#include "window.h"
#include "sample.h"

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    return Win32App_Run(&(DXSample){ .title = "D3D12 Depth Bounds Test Sample", .width = 1280, .height = 720 }, hInstance, nCmdShow);
}
