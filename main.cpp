#include <windows.h>
#include "WindowManager.h"
#include "Tracker.h"
#include <gdiplus.h>
#pragma comment(lib, "Gdiplus.lib")

// Global variables
HINSTANCE hInst;
HWND hWnd;
ULONG_PTR gdiplusToken;
bool isRunning = true;
bool isPaused = false;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    hInst = hInstance;

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL)
        != Gdiplus::Ok) {
        MessageBox(NULL, "Failed to initialize GDI+.", "Error", MB_ICONERROR);
        return -1;
    }

    // Register window class and create window
    RegisterMainWindowClass(hInst);
    hWnd = CreateMainWindow(hInst);

    // Start the tracking thread
    StartTrackingThread();

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0 && isRunning) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}
