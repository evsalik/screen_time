#include <windows.h>
#include <psapi.h>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <sstream> // For std::ostringstream
#include <shellapi.h> // For system tray icon
#include <mutex>

// Global variables
HINSTANCE hInst; // Instance handle
HWND hWnd; // Main window handle
NOTIFYICONDATA nid; // Notification icon data

// Tracking data
std::map<std::string, std::chrono::seconds> appActiveTime;
std::string currentApp = "";
std::mutex dataMutex; // Mutex for synchronizing access to shared data
auto appStartTime = std::chrono::system_clock::now();

// Application state
bool isRunning = true;

// Function to get the executable name from a window handle
std::string GetAppNameFromWindow(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess) {
        char exePath[MAX_PATH];
        if (GetModuleFileNameExA(hProcess, NULL, exePath, MAX_PATH)) {
            CloseHandle(hProcess);
            // Extract the file name from the full path
            std::string fullPath(exePath);
            size_t pos = fullPath.find_last_of("\\/");
            return fullPath.substr(pos + 1);
        }
        CloseHandle(hProcess);
    }
    return "Unknown";
}

// Function to format duration into h:m:s
std::string FormatDuration(std::chrono::seconds duration) {
    int totalSeconds = static_cast<int>(duration.count());
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    std::ostringstream oss;
    oss << hours << "h:" << minutes << "m:" << seconds << "s";
    return oss.str();
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            // Initialize the system tray icon
            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = 1001;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_APP + 1;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            strcpy_s(nid.szTip, "Screen Time Tracker");
            Shell_NotifyIcon(NIM_ADD, &nid);
            break;

        case WM_PAINT:
            // Handle window painting
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Fill the background with the window's background color
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));

            // Lock the mutex before accessing shared data
            std::lock_guard<std::mutex> lock(dataMutex);

            // Prepare the text to display
            std::ostringstream oss;
            oss << "Tracked Applications:\n";
            oss << "---------------------\n";
            for (const auto& entry : appActiveTime) {
                oss << "Application: " << entry.first
                    << "  Total Time: " << FormatDuration(entry.second) << "\n";
            }

            // Get the text as std::string
            std::string text = oss.str();

            SetBkMode(hdc, OPAQUE);

            // Draw the text
            DrawTextA(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_TOP);

            EndPaint(hwnd, &ps);
        }
            break;

        case WM_APP + 1:
            // Handle system tray icon messages
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                // Show a context menu
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 1, "Show/Hide");
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 2, "Exit");
                    SetForegroundWindow(hwnd);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                    if (cmd == 1) {
                        // Toggle window visibility
                        if (IsWindowVisible(hwnd)) {
                            ShowWindow(hwnd, SW_HIDE);
                        } else {
                            ShowWindow(hwnd, SW_SHOW);
                        }
                    } else if (cmd == 2) {
                        // Exit application
                        isRunning = false;
                        DestroyWindow(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
            }
            break;

        case WM_DESTROY:
            // Clean up system tray icon
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    hInst = hInstance;

    // Register window class
    const char CLASS_NAME[] = "ScreenTimeTrackerWindowClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClass(&wc);

    // Create the window
    hWnd = CreateWindowEx(
            0,
            CLASS_NAME,
            "Screen Time Tracker",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
            NULL,
            NULL,
            hInst,
            NULL
    );

    if (hWnd == NULL) {
        return 0;
    }

    ShowWindow(hWnd, SW_SHOW);

    // Start the tracking thread
    std::thread trackingThread([]() {
        while (isRunning) {
            // Get the handle of the foreground window
            HWND hwnd = GetForegroundWindow();
            if (hwnd != NULL) {
                // Get the application name
                std::string appName = GetAppNameFromWindow(hwnd);

                // Lock the mutex
                std::lock_guard<std::mutex> lock(dataMutex);

                if (appName != currentApp) {
                    // If the application has changed, record the time spent on the previous app
                    if (!currentApp.empty()) {
                        auto now = std::chrono::system_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - appStartTime);
                        appActiveTime[currentApp] += duration;
                    }
                    // Update current application and start time
                    currentApp = appName;
                    appStartTime = std::chrono::system_clock::now();
                } else {
                    // Update the active time for the current app
                    auto now = std::chrono::system_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - appStartTime);
                    appActiveTime[currentApp] += duration;
                    appStartTime = now;
                }
            }

            // Invalidate the window to trigger repaint
            InvalidateRect(hWnd, NULL, TRUE);

            // Sleep for 1 second before updating again
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Wait for the tracking thread to finish
    if (trackingThread.joinable()) {
        trackingThread.join();
    }

    return 0;
}
