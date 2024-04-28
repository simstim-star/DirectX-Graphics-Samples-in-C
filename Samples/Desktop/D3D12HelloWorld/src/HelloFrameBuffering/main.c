#include "window.h"
#include "sample.h"

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    DXSample sample = { 0 };
    sample.title = "HelloFrameBuffering";
    sample.width = 1280; 
    sample.height = 720; 
    return Win32App_Run(&sample, hInstance, nCmdShow);
}