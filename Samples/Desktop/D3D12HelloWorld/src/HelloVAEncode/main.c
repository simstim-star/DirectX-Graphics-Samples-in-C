#include "window.h"
#include "sample.h"

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    DXSample sample = { .title = "D3D12 Hello Window", .width = 352, .height = 288 };
    return Win32App_Run(&sample, hInstance, nCmdShow);
}