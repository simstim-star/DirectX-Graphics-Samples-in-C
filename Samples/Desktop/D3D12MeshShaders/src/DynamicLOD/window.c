#include "window.h"
#include "sample.h"

HWND G_HWND = NULL;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

/* Only for anomalous exiting */
static DXSample* __pSampleForCleanup = NULL;
static void __cleanOnExit(void);

int Win32App_Run(DXSample* const pSample, const HINSTANCE hInstance, const int nCmdShow)
{
    // Initialize the window class.
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = "DXSampleClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, (LONG)(pSample->width), (LONG)(pSample->height) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    G_HWND = CreateWindow(
        windowClass.lpszClassName,
        pSample->title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        NULL,        // We have no parent window.
        NULL,        // We aren't using menus.
        hInstance,
        pSample);

    __pSampleForCleanup = pSample;
    atexit(__cleanOnExit);
    
    // Initialize the sample.
    Sample_Init(pSample);

    ShowWindow(G_HWND, nCmdShow);

    // Main sample loop.
    MSG msg = {0};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    Sample_Destroy(pSample);
    // Return this part of the WM_QUIT message to Windows.
    return (char)(msg.wParam);
}

// Main message handler for the sample.
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    DXSample* pSample = (DXSample*)(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
        // Save the DXSample* passed in to CreateWindow.
        LPCREATESTRUCT pCreateStruct = (LPCREATESTRUCT)(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(pCreateStruct->lpCreateParams));
    }
    return 0;

    case WM_KEYDOWN:
        if (pSample)
        {
            Sample_KeyDown(pSample, wParam);
        }
        return 0;

    case WM_KEYUP:
        if (pSample)
        {
            Sample_KeyUp(pSample, wParam);
        }
        return 0;

    case WM_PAINT:
        if (pSample)
        {
            Sample_Update(pSample);
            Sample_Render(pSample);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hwnd, message, wParam, lParam);
}

static void __cleanOnExit(void) {
    if (__pSampleForCleanup) {
        Sample_AtExit(__pSampleForCleanup);
    }
}