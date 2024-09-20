// WindowManager.cpp
#include <windows.h>
#include <windowsx.h> // Added to define GET_X_LPARAM and GET_Y_LPARAM
#include "WindowManager.h"
#include "FormatUtils.h" // For FormatDuration
#include <gdiplus.h>
#include <mutex>
#include <string>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include "Resource.h"
#include <dwmapi.h>
#include <chrono> // Added for std::chrono::seconds
#include <cstdio>
#include <assert.h>
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

// Scroll parameters
int scrollPos = 0;          // Current scroll position
int scrollMax = 0;          // Maximum scroll position
const int scrollStep = 20;  // Amount to scroll on each step

// DPI scaling factors
float dpiScaleX = 1.0f;
float dpiScaleY = 1.0f;

// Custom Scroll Bar Parameters
const int SCROLL_BAR_WIDTH = static_cast<int>(15 * dpiScaleX);
const Color SCROLL_BAR_BACKGROUND_COLOR(30, 30, 30); // Dark gray background
const Color SCROLL_BAR_THUMB_COLOR(80, 80, 80);      // Lighter gray thumb
const int THUMB_HEIGHT = 50; // Example thumb height

// Variables to track scroll thumb dragging
bool isDraggingThumb = false;
POINT dragStartPoint;
int initialScrollPos = 0;

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

void DrawRoundedRectangle(Graphics& graphics, Brush& brush, Rect rect, int radius) {
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
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Removed WS_VSCROLL
            xPos, yPos, windowWidth, windowHeight,
            NULL,
            NULL,
            hInstance,
            NULL
    );

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

Bitmap* ResizeBitmap(Bitmap* source, int width, int height) {
    if (!source) return nullptr;

    // Create a new bitmap with the desired dimensions
    Bitmap* resized = new Bitmap(width, height, PixelFormat32bppARGB);
    if (resized->GetLastStatus() != Ok) {
        delete resized;
        return nullptr;
    }

    // Create a Graphics object associated with the new bitmap
    Graphics graphics(resized);

    // Set high-quality rendering options
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    // Draw the original bitmap onto the new bitmap, scaling it
    graphics.DrawImage(source, 0, 0, width, height);

    return resized;
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

            // Enable dark mode for the title bar
            BOOL useDarkMode = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

            // Set window corner preference to rounded (Windows 11)
            DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

            // Initialize scrollbar
            scrollPos = 0;
            scrollMax = 0;

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
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Calculate scroll bar position
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int scrollBarX = clientRect.right - SCROLL_BAR_WIDTH;
            int scrollBarY = 0;
            int scrollBarHeight = clientRect.bottom - clientRect.top;

            // Calculate thumb position
            double proportion = (double)scrollPos / (double)(scrollMax > 0 ? scrollMax : 1);
            int thumbY = static_cast<int>((scrollBarHeight - THUMB_HEIGHT) * proportion);

            // Check if click is within the thumb area
            if (x >= scrollBarX && x <= scrollBarX + SCROLL_BAR_WIDTH &&
                y >= thumbY && y <= thumbY + THUMB_HEIGHT) {
                isDraggingThumb = true;
                dragStartPoint = { x, y };
                initialScrollPos = scrollPos;
                SetCapture(hwnd); // Capture mouse input
            } else {
                // Enable window dragging if not clicking the scroll bar
                ReleaseCapture();
                SendMessage(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (isDraggingThumb) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                int deltaY = y - dragStartPoint.y;

                // Calculate the proportion of movement
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                int scrollBarHeight = clientRect.bottom - clientRect.top;
                int maxThumbY = scrollBarHeight - THUMB_HEIGHT;

                double proportionChange = (double)deltaY / (double)maxThumbY;
                int scrollChange = static_cast<int>(proportionChange * scrollMax);

                // Update scrollPos based on movement
                scrollPos = initialScrollPos + scrollChange;

                // Clamp scrollPos
                if (scrollPos < 0) scrollPos = 0;
                if (scrollPos > scrollMax) scrollPos = scrollMax;

                // Update the custom scroll bar position
                // No need to use SetScrollPos for standard scroll bar
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (isDraggingThumb) {
                isDraggingThumb = false;
                ReleaseCapture();
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            // Extract the wheel delta
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);

            // Determine the scroll amount (number of lines to scroll)
            int linesToScroll = delta / WHEEL_DELTA; // Typically 1 line per wheel delta

            // Adjust scrollPos
            scrollPos -= linesToScroll * scrollStep; // scrollStep defines how much to scroll per wheel step

            // Clamp scrollPos within [0, scrollMax]
            if (scrollPos < 0) scrollPos = 0;
            if (scrollPos > scrollMax) scrollPos = scrollMax;

            // Update the scroll bar position
            // No need to use SetScrollPos for standard scroll bar
            InvalidateRect(hwnd, NULL, TRUE);

            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            Bitmap bufferBitmap(ps.rcPaint.right, ps.rcPaint.bottom);
            Graphics bufferGraphics(&bufferBitmap);

            // Set high-quality rendering
            bufferGraphics.SetSmoothingMode(SmoothingModeAntiAlias);
            bufferGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            bufferGraphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            bufferGraphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            bufferGraphics.SetCompositingQuality(CompositingQualityHighQuality);

            bufferGraphics.SetPageUnit(UnitPixel);
            bufferGraphics.ResetTransform();

            // Fill background with black color
            SolidBrush backgroundBrush(Color(0, 0, 0)); // Black color
            bufferGraphics.FillRectangle(&backgroundBrush, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom);

            // Use white color for text
            SolidBrush textBrush(Color(255, 255, 255)); // White text

            std::lock_guard<std::mutex> lock(dataMutex);
            for (const auto& entry : appActiveTime) {
                // Ensure valid data is present
                assert(entry.second.count() > 0 && "App active time should not be zero");
            }

            // Sort applications by usage time
            std::vector<std::pair<std::string, std::chrono::seconds>> apps(appActiveTime.begin(), appActiveTime.end());
            std::sort(apps.begin(), apps.end(), [](const auto& a, const auto& b) -> bool {
                return a.second > b.second;
            });

            // Calculate total content height with padding
            const int LIST_PADDING = static_cast<int>(20 * dpiScaleY); // 20 pixels padding
            int contentHeight = 0;
            int iconSize = static_cast<int>(32 * dpiScaleX); // Updated to DESIRED_ICON_SIZE
            int yIncrement = iconSize + static_cast<int>(15 * dpiScaleY);
            for (const auto& entry : apps) {
                contentHeight += yIncrement;
            }
            contentHeight += LIST_PADDING; // Add padding after the last item

            // Update scrollbar range
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int windowHeight = clientRect.bottom - clientRect.top;

            if (contentHeight > windowHeight) {
                scrollMax = contentHeight - windowHeight;
            } else {
                scrollMax = 0;
            }

            // Adjust starting y based on scroll position
            int xPos = static_cast<int>(10 * dpiScaleX);
            int yPos = static_cast<int>(10 * dpiScaleY) - scrollPos;

            std::chrono::seconds totalTime(0);
            for (const auto& entry : appActiveTime)
                totalTime += entry.second;
            if (totalTime.count() == 0)
                totalTime = std::chrono::seconds(1);

            Font font(L"Segoe UI", static_cast<REAL>(10 * dpiScaleY)); // Adjust for scaling

            for (const auto& entry : apps) {
                std::string appName = entry.first;
                auto appTime = entry.second;
                std::string appPath = appPaths[appName];

                // Check if the icon is already cached
                Bitmap* pResizedIcon = nullptr;

                // Load the app icon with desired size
                HICON hIconLarge = NULL;
                UINT iconCount = ExtractIconExA(appPath.c_str(), 0, &hIconLarge, NULL, 1);
                if (iconCount == 0 || !hIconLarge) {
                    hIconLarge = LoadIcon(NULL, IDI_APPLICATION);
                }

                // Create a GDI+ Bitmap from the HICON
                Bitmap* pIconBitmap = Bitmap::FromHICON(hIconLarge);

                // Resize the icon bitmap using the helper function
                pResizedIcon = ResizeBitmap(pIconBitmap, iconSize, iconSize);

                // Clean up
                delete pIconBitmap;
                DestroyIcon(hIconLarge);

                // Adjust positions based on scaling
                int iconX = xPos;
                int iconY = yPos + static_cast<int>(15 * dpiScaleY) - 6;
                int textX = xPos + iconSize + static_cast<int>(5 * dpiScaleX);
                int appNameY = yPos + static_cast<int>(3 * dpiScaleY);
                int barX = textX;
                int barY = yPos + iconSize + static_cast<int>(1 * dpiScaleY);
                int timeY = barY - static_cast<int>(6 * dpiScaleY);

                // Draw the resized icon
                if (pResizedIcon) {
                    bufferGraphics.DrawImage(pResizedIcon, Rect(iconX, iconY, iconSize, iconSize));
                }

                // Draw the app name
                std::wstring wAppName(appName.begin(), appName.end());
                bufferGraphics.DrawString(wAppName.c_str(), -1, &font,
                                          PointF(static_cast<REAL>(textX), static_cast<REAL>(appNameY)), &textBrush);

                // Draw the bar using a gradient brush
                double percentage = static_cast<double>(appTime.count()) / totalTime.count();
                int barMaxWidth = ps.rcPaint.right - barX - static_cast<int>(20 * dpiScaleX) - 100;
                int barWidth = static_cast<int>(percentage * barMaxWidth);
                Rect barRect(barX, barY, barWidth, static_cast<int>(8 * dpiScaleY)); // BAR_HEIGHT

                // Define gradient colors
                Color gradientStart(180, 180, 180, 180); // Light gray
                Color gradientEnd(100, 100, 100, 100);   // Dark gray

                // Create a linear gradient brush
                LinearGradientBrush gradientBrush(
                        Point(barRect.X, barRect.Y),
                        Point(barRect.X, barRect.Y + barRect.Height),
                        gradientStart,
                        gradientEnd
                );

                // Draw the bar using the gradient brush
                DrawRoundedRectangle(bufferGraphics, gradientBrush, barRect, static_cast<int>(5 * dpiScaleX));

                // Draw the time label aligned with the bar
                std::string timeStr = FormatDuration(appTime);
                std::wstring wTimeStr(timeStr.begin(), timeStr.end());
                bufferGraphics.DrawString(wTimeStr.c_str(), -1, &font,
                                          PointF(static_cast<REAL>(barX + barWidth + static_cast<int>(5 * dpiScaleX)),
                                                 static_cast<REAL>(timeY)), &textBrush);

                // Move to the next item
                yPos += yIncrement;
            }

            // Draw the custom scroll bar
            int scrollBarX = clientRect.right - SCROLL_BAR_WIDTH;
            int scrollBarY = 0;
            int scrollBarHeight = clientRect.bottom - clientRect.top;

            // Draw scroll bar background
            SolidBrush scrollBarBgBrush(SCROLL_BAR_BACKGROUND_COLOR);
            bufferGraphics.FillRectangle(&scrollBarBgBrush, scrollBarX, scrollBarY, SCROLL_BAR_WIDTH, scrollBarHeight);

            // Calculate thumb position
            double proportion = (double)scrollPos / (double)(scrollMax > 0 ? scrollMax : 1);
            int thumbY = static_cast<int>((scrollBarHeight - THUMB_HEIGHT) * proportion);
            if (scrollMax == 0) thumbY = 0; // Prevent division by zero

            // Draw scroll bar thumb with gradient
            Color thumbGradientStart(200, 200, 200, 200); // Lighter gray
            Color thumbGradientEnd(150, 150, 150, 150);   // Darker gray

            LinearGradientBrush thumbGradientBrush(
                    Point(scrollBarX, thumbY),
                    Point(scrollBarX, thumbY + THUMB_HEIGHT),
                    thumbGradientStart,
                    thumbGradientEnd
            );

            Rect thumbRect(scrollBarX, thumbY, SCROLL_BAR_WIDTH, THUMB_HEIGHT);
            DrawRoundedRectangle(bufferGraphics, thumbGradientBrush, thumbRect, static_cast<int>(5 * dpiScaleX));

            // Render the off-screen buffer to the window
            Graphics graphics(hdc);
            graphics.DrawImage(&bufferBitmap, 0, 0);

            EndPaint(hwnd, &ps);
            break;
        }
        case WM_VSCROLL: {
            // Not needed for custom scroll bar, but kept for compatibility
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
