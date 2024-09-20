#include <windows.h>
#include "WindowManager.h"
#include "Tracker.h"
#include <gdiplus.h>
#include <mutex>
#include <string>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include "Resource.h"
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shcore.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT       = 0,
    DWMWCP_DONOTROUND    = 1,
    DWMWCP_ROUND         = 2,
    DWMWCP_ROUNDSMALL    = 3
};

using namespace Gdiplus;

extern HINSTANCE hInst;
extern HWND hWnd;
extern bool isRunning;
extern bool isPaused;
extern std::mutex dataMutex;
extern std::map<std::string, std::chrono::seconds> appActiveTime;
extern std::map<std::string, std::string> appPaths;

// DPI scaling factors
float dpiScaleX = 1.0f;
float dpiScaleY = 1.0f;

void RegisterMainWindowClass(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ScreenTimeTrackerWindowClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON)); // Load from resources
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // No background brush
    RegisterClass(&wc);
}

void DrawRoundedRectangle(Graphics& graphics, SolidBrush& brush, Rect rect, int radius) {
    GraphicsPath path;
    path.AddArc(rect.X, rect.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.X + rect.Width - radius * 2, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    graphics.FillPath(&brush, &path);
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 400;
    int windowHeight = 300;

    // Calculate window position to center it
    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowEx(
            0,
            "ScreenTimeTrackerWindowClass",
            "Screen Time Tracker", // Window title
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Window with title bar and minimize button
            xPos, yPos, windowWidth, windowHeight,
            NULL,
            NULL,
            hInstance,
            NULL
    );

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static NOTIFYICONDATA nid = {};
    switch (uMsg) {
        case WM_CREATE: {
            // Get the DPI scaling factor using GetDeviceCaps
            HDC screen = GetDC(hwnd);
            int dpiX = GetDeviceCaps(screen, LOGPIXELSX);
            int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
            ReleaseDC(hwnd, screen);

            dpiScaleX = dpiX / 96.0f;
            dpiScaleY = dpiY / 96.0f;

            BOOL useDarkMode = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

            // Set window corner preference to rounded (Windows 11)
            DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = 1001;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_APP + 1;
            nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON)); // Use custom icon
            strcpy_s(nid.szTip, "Screen Time Tracker");
            Shell_NotifyIcon(NIM_ADD, &nid);
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_ERASEBKGND:
            return 1; // Prevent background erasure to reduce flickering
        case WM_LBUTTONDOWN:
            ReleaseCapture();
            SendMessage(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            Bitmap bufferBitmap(ps.rcPaint.right, ps.rcPaint.bottom);
            Graphics bufferGraphics(&bufferBitmap);

            // Set high-quality rendering
            bufferGraphics.SetSmoothingMode(SmoothingModeAntiAlias);
            bufferGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            bufferGraphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            bufferGraphics.SetPageUnit(UnitPixel);
            bufferGraphics.ResetTransform();

            // Fill background with black color
            SolidBrush backgroundBrush(Color(0, 0, 0)); // Black color
            bufferGraphics.FillRectangle(&backgroundBrush, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom);

            // Use white color for text
            SolidBrush textBrush(Color(255, 255, 255)); // White text
            SolidBrush barBrush(Color(128, 255, 255, 255)); // 50% opaque white for bars

            std::lock_guard<std::mutex> lock(dataMutex);

            // Sort applications by usage time
            std::vector<std::pair<std::string, std::chrono::seconds>> apps(appActiveTime.begin(), appActiveTime.end());
            std::sort(apps.begin(), apps.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });


            int x = static_cast<int>(10 * dpiScaleX);
            int y = static_cast<int>(10 * dpiScaleY);
            int iconSize = static_cast<int>(24 * dpiScaleX);
            int yIncrement = iconSize + static_cast<int>(15 * dpiScaleY);
            int barHeight = static_cast<int>(8 * dpiScaleY);
            std::chrono::seconds totalTime(0);
            for (const auto& entry : appActiveTime)
                totalTime += entry.second;
            if (totalTime.count() == 0)
                totalTime = std::chrono::seconds(1);

            Font font(L"Segoe UI", static_cast<REAL>(10 * dpiScaleY)); // Adjust font size

            for (const auto& entry : apps) {
                std::string appName = entry.first;
                auto appTime = entry.second;
                std::string appPath = appPaths[appName];

                // Load the app icon
                HICON hIcon = NULL;
                UINT iconCount = ExtractIconExA(appPath.c_str(), 0, NULL, &hIcon, 1);
                if (iconCount == 0 || !hIcon) {
                    hIcon = LoadIcon(NULL, IDI_APPLICATION);
                }

                Bitmap* pIconBitmap = Bitmap::FromHICON(hIcon);

                // Adjust positions
                int iconX = x;
                int iconY = y + static_cast<int>(15 * dpiScaleY);
                int textX = x + iconSize + static_cast<int>(5 * dpiScaleX);
                int appNameY = y + static_cast<int>(3 * dpiScaleY);
                int barX = textX;
                int barY = y + iconSize + static_cast<int>(1 * dpiScaleY);
                int timeY = barY - static_cast<int>(6 * dpiScaleY);

                // Draw the icon
                if (pIconBitmap) {
                    // Draw the icon at desired size
                    bufferGraphics.DrawImage(pIconBitmap, Rect(iconX, iconY, iconSize, iconSize));
                    delete pIconBitmap;
                }

                // Destroy the icon handle
                DestroyIcon(hIcon);

                // Draw the app name
                std::wstring wAppName(appName.begin(), appName.end());
                bufferGraphics.DrawString(wAppName.c_str(), -1, &font,
                                          PointF(static_cast<REAL>(textX), static_cast<REAL>(appNameY)), &textBrush);

                // Draw the bar
                double percentage = static_cast<double>(appTime.count()) / totalTime.count();
                int barMaxWidth = ps.rcPaint.right - barX - static_cast<int>(20 * dpiScaleX) - 100;
                int barWidth = static_cast<int>(percentage * barMaxWidth);
                Rect barRect(barX, barY, barWidth, barHeight);
                int cornerRadius = static_cast<int>(5 * dpiScaleX); // Adjust as needed
                DrawRoundedRectangle(bufferGraphics, barBrush, barRect, cornerRadius);

                // Draw the time label aligned with the bar
                std::string timeStr = FormatDuration(appTime);
                std::wstring wTimeStr(timeStr.begin(), timeStr.end());
                bufferGraphics.DrawString(wTimeStr.c_str(), -1, &font,
                                          PointF(static_cast<REAL>(barX + barWidth + static_cast<int>(5 * dpiScaleX)),
                                                 static_cast<REAL>(timeY)), &textBrush);

                // Move to the next item
                y += yIncrement;
            }

            // Render the off-screen buffer to the window
            Graphics graphics(hdc);
            graphics.DrawImage(&bufferBitmap, 0, 0);

            EndPaint(hwnd, &ps);
            break;
        }
        case WM_APP + 1:
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 1, "Show/Hide");
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 2, isPaused ? "Resume" : "Pause");
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 3, "Kill");
                    SetForegroundWindow(hwnd);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                             pt.x, pt.y, 0, hwnd, NULL);
                    if (cmd == 1) {
                        if (IsWindowVisible(hwnd))
                            ShowWindow(hwnd, SW_HIDE);
                        else
                            ShowWindow(hwnd, SW_SHOW);
                    } else if (cmd == 2) {
                        isPaused = !isPaused;
                    } else if (cmd == 3) {
                        isRunning = false;
                        DestroyWindow(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
            }
            break;
        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
