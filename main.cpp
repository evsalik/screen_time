#include <windows.h>
#include <wtsapi32.h>
#include <iostream>

int main() {
    // Basic application setup

    // Get the instance handle
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // Register window class
    const char CLASS_NAME[] = "ScreenTimeTrackerWindowClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc; // Default window procedure
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        std::cerr << "Failed to register window class.\n";
        return 1;
    }

    // Create an invisible window
    HWND hwnd = CreateWindowEx(
            0,
            CLASS_NAME,
            "Screen Time Tracker",
            0,
            0, 0, 0, 0,
            NULL,
            NULL,
            hInstance,
            NULL
    );

    if (hwnd == NULL) {
        std::cerr << "Failed to create window.\n";
        return 1;
    }

    // Register for session change notifications
    if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION)) {
        std::cerr << "Failed to register session notifications.\n";
        return 1;
    }

    // Message loop placeholder
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // TODO: Add idle time checking and tracking logic here
    }

    // Clean up
    WTSUnRegisterSessionNotification(hwnd);
    DestroyWindow(hwnd);
    UnregisterClass(CLASS_NAME, hInstance);

    return 0;
}
