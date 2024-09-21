#include <windows.h>
#include <windowsx.h>
#include "WindowManager.h"
#include "FormatUtils.h"
#include <gdiplus.h>
#include <mutex>
#include <string>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include "Resource.h"
#include <dwmapi.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include "json.hpp"
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shcore.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define IDC_CLEAR_BUTTON      1001
#define IDC_BUTTON_TODAY      1002
#define IDC_BUTTON_3DAYS      1003
#define IDC_BUTTON_WEEK       1004
#define IDC_BUTTON_MONTH      1005
#endif

enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT       = 0,
    DWMWCP_DONOTROUND    = 1,
    DWMWCP_ROUND         = 2,
    DWMWCP_ROUNDSMALL    = 3
};

enum TimeRange { TODAY, LAST_3_DAYS, LAST_WEEK, LAST_MONTH };
TimeRange selectedTimeRange = TODAY;

using namespace Gdiplus;
using json = nlohmann::json;

extern HINSTANCE hInst;
extern HWND hWnd;
extern bool isRunning;
extern bool isPaused;
extern std::mutex dataMutex;
extern std::map<std::string, std::chrono::seconds> appActiveTime;
extern std::map<std::string, std::string> appPaths;
std::map<std::string, std::chrono::system_clock::time_point> appStartTime;
extern std::string currentAppName;

int scrollPos = 0;
int scrollMax = 0;
const int scrollStep = 10;

float dpiScaleX = 1.0f;
float dpiScaleY = 1.0f;

int windowWidth = 400;
int windowHeight = 400;

const int SCROLL_BAR_WIDTH = static_cast<int>(15 * dpiScaleX);
const int MIN_BAR_WIDTH = 5;

const Color DARK_SCROLL_BAR_BACKGROUND_COLOR(25, 25, 25);
const Color DARK_SCROLL_BAR_THUMB_COLOR(50, 50, 50);
const Color LESS_AGGRESSIVE_GRADIENT_START(150, 150, 150, 150);
const Color LESS_AGGRESSIVE_GRADIENT_END(90, 90, 90, 90);

const Color HIGHLIGHT_COLOR(80, 80, 80);

const int THUMB_HEIGHT = 50;

// Variables to track scroll thumb dragging
bool isDraggingThumb = false;
POINT dragStartPoint;
int initialScrollPos = 0;

// Animation state
std::map<std::string, int> currentBarWidths;

std::chrono::system_clock::time_point GetStartTimeForRange(TimeRange range) {
    auto now = std::chrono::system_clock::now();
    switch (range) {
        case TODAY:
            return now - std::chrono::hours(24);
        case LAST_3_DAYS:
            return now - std::chrono::hours(72);
        case LAST_WEEK:
            return now - std::chrono::hours(24 * 7);
        case LAST_MONTH:
            return now - std::chrono::hours(24 * 30);
        default:
            return now; // Should never happen
    }
}

void SaveTrackingDataToFile(const std::string& filename) {
    std::cout << "Saving data..." << std::endl;
    json j;
    {
        std::lock_guard<std::mutex> lock(dataMutex); // Lock the dataMutex within a scoped block

        for (const auto& [appName, timeSpent] : appActiveTime) {
            // Ensure valid data before saving
            if (appPaths.find(appName) != appPaths.end() && appStartTime.find(appName) != appStartTime.end()) {
                j["app_data"][appName]["time_in_seconds"] = timeSpent.count();
                j["app_data"][appName]["app_path"] = appPaths[appName];

                // Convert the start time to a string
                std::time_t startTime = std::chrono::system_clock::to_time_t(appStartTime[appName]);
                j["app_data"][appName]["start_time"] = std::ctime(&startTime);
            }
        }
    }

    try {
        std::ofstream file(filename, std::ios::out | std::ios::trunc); // Open in truncate mode
        if (!file.is_open()) {
            std::cerr << "Error: Unable to open file for saving data" << std::endl;
            return;
        }

        // Dump the JSON with proper indentation
        file << j.dump(4) << std::endl;  // 4-space indentation and ensuring newline at the end

        // Flush to make sure all content is written
        file.flush();

        // Close the file after writing
        file.close();

        // Check if file failed to close properly
        if (file.fail()) {
            std::cerr << "Error: Failed to close the file properly after saving data" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "Error saving JSON to file: " << e.what() << std::endl;
    }
}

void LoadTrackingDataFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file for loading data" << std::endl;
        return; // If the file doesn't exist, skip loading
    }

    json j;
    try {
        file >> j;
        file.close(); // Ensure the file is closed after reading
    } catch (const std::exception& e) {
        // Log the error or handle it if the file is not a valid JSON
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        file.close();
        return; // Exit function if the file contains invalid JSON
    }

    if (j.is_null() || j.empty()) {
        // Handle empty or null JSON case
        std::cerr << "Warning: Loaded JSON is empty or null." << std::endl;
        return; // Don't proceed if the JSON is empty
    }

    std::lock_guard<std::mutex> lock(dataMutex);

    for (auto& [appName, data] : j["app_data"].items()) {
        if (!data.contains("time_in_seconds") || !data.contains("app_path") || !data.contains("start_time")) {
            continue; // Skip entries with missing data
        }

        std::chrono::seconds timeSpent(data["time_in_seconds"].get<int>());
        appActiveTime[appName] = timeSpent;
        appPaths[appName] = data["app_path"].get<std::string>();

        // Parse the start time
        std::string startTimeStr = data["start_time"].get<std::string>();
        std::tm tm = {};
        std::istringstream ss(startTimeStr);
        ss >> std::get_time(&tm, "%a %b %d %H:%M:%S %Y");
        auto startTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        appStartTime[appName] = startTime;
    }
}


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

// Linear interpolation function
float Lerp(float start, float end, float t) {
    return start + t * (end - start);
}

void UpdateBarWidth(const std::string& appName, int targetWidth) {
    const float animationSpeed = 0.1f;
    if (currentBarWidths.find(appName) == currentBarWidths.end()) {
        currentBarWidths[appName] = targetWidth; // Initialize if not present
    }

    // LERP towards the target width for smooth transitions
    currentBarWidths[appName] = static_cast<int>(
            Lerp(static_cast<float>(currentBarWidths[appName]), static_cast<float>(targetWidth), animationSpeed)
    );
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowEx(
            WS_EX_COMPOSITED,  // Adding WS_EX_COMPOSITED for double buffering
            "ScreenTimeTrackerWindowClass",
            "Screen Time Tracker",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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

    Graphics graphics(resized);

    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    graphics.DrawImage(source, 0, 0, width, height);

    return resized;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static NOTIFYICONDATA nid = {};
    static std::thread trackingThread;  // Keep a reference to the tracking thread
    switch (uMsg) {
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDrawItem = (LPDRAWITEMSTRUCT)lParam;

            // Handle the Clear Button
            if (pDrawItem->CtlID == IDC_CLEAR_BUTTON) {
                HDC hdc = pDrawItem->hDC;
                RECT rect = pDrawItem->rcItem;

                HBRUSH hBrush = CreateSolidBrush(RGB(25, 25, 25));
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);

                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
                SelectObject(hdc, hPen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 5, 5);
                DeleteObject(hPen);

                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, "Clear Data", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            else if (pDrawItem->CtlID == IDC_BUTTON_TODAY ||
                     pDrawItem->CtlID == IDC_BUTTON_3DAYS ||
                     pDrawItem->CtlID == IDC_BUTTON_WEEK ||
                     pDrawItem->CtlID == IDC_BUTTON_MONTH) {

                HDC hdc = pDrawItem->hDC;
                RECT rect = pDrawItem->rcItem;

                bool isSelected = false;
                if ((pDrawItem->CtlID == IDC_BUTTON_TODAY && selectedTimeRange == TODAY) ||
                    (pDrawItem->CtlID == IDC_BUTTON_3DAYS && selectedTimeRange == LAST_3_DAYS) ||
                    (pDrawItem->CtlID == IDC_BUTTON_WEEK && selectedTimeRange == LAST_WEEK) ||
                    (pDrawItem->CtlID == IDC_BUTTON_MONTH && selectedTimeRange == LAST_MONTH)) {
                    isSelected = true;
                }

                // Button background color (highlight if selected)
                HBRUSH hBrush = CreateSolidBrush(isSelected ? RGB(80, 80, 80) : RGB(25, 25, 25)); // Highlight or dark gray
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);

                HPEN hPen = CreatePen(PS_SOLID, 1, isSelected ? RGB(255, 255, 255) : RGB(100, 100, 100));
                SelectObject(hdc, hPen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 5, 5); // Rounded corners
                DeleteObject(hPen);

                SetTextColor(hdc, RGB(255, 255, 255)); // White text
                SetBkMode(hdc, TRANSPARENT);
                if (pDrawItem->CtlID == IDC_BUTTON_TODAY) {
                    DrawText(hdc, "Today", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else if (pDrawItem->CtlID == IDC_BUTTON_3DAYS) {
                    DrawText(hdc, "Last 3 Days", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else if (pDrawItem->CtlID == IDC_BUTTON_WEEK) {
                    DrawText(hdc, "Last Week", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else if (pDrawItem->CtlID == IDC_BUTTON_MONTH) {
                    DrawText(hdc, "Last Month", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                return TRUE;
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == 1) {  // Check if this is our timer
                InvalidateRect(hwnd, NULL, TRUE); // Request the window to repaint
            }
            break;
        }
        case WM_CREATE: {
            // For debug:
            // AllocConsole();
            // freopen("CONOUT$", "w", stdout);

            LoadTrackingDataFromFile("tracking_data.json"); // Load the tracking data from file
            SetTimer(hwnd, 1, 1000 / 60, NULL);

            // Get the DPI scaling factor using GetDeviceCaps
            HDC screen = GetDC(hwnd);
            int dpiX = GetDeviceCaps(screen, LOGPIXELSX);
            int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
            ReleaseDC(hwnd, screen);

            dpiScaleX = dpiX / 96.0f;
            dpiScaleY = dpiY / 96.0f;

            int headerHeight = static_cast<int>(50 * dpiScaleY);
            int buttonWidth = static_cast<int>(60 * dpiScaleX);
            int buttonHeight = static_cast<int>(20 * dpiScaleY);
            int buttonSpacing = 4;

            HWND hButtonToday = CreateWindow(
                    "BUTTON", "Today",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10, 5, buttonWidth, buttonHeight,
                    hwnd, (HMENU)IDC_BUTTON_TODAY, hInst, NULL
            );
            SendMessage(hButtonToday, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            HWND hButton3Days = CreateWindow(
                    "BUTTON", "Last 3 Days",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10 + buttonWidth + buttonSpacing, 5, buttonWidth, buttonHeight,
                    hwnd, (HMENU)IDC_BUTTON_3DAYS, hInst, NULL
            );
            SendMessage(hButton3Days, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            HWND hButtonWeek = CreateWindow(
                    "BUTTON", "Last Week",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10 + 2 * (buttonWidth + buttonSpacing), 5, buttonWidth, buttonHeight,
                    hwnd, (HMENU)IDC_BUTTON_WEEK, hInst, NULL
            );
            SendMessage(hButtonWeek, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            HWND hButtonMonth = CreateWindow(
                    "BUTTON", "Last Month",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10 + 3 * (buttonWidth + buttonSpacing), 5, buttonWidth, buttonHeight,
                    hwnd, (HMENU)IDC_BUTTON_MONTH, hInst, NULL
            );
            SendMessage(hButtonMonth, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            HWND hClearButton = CreateWindow(
                    "BUTTON", "Clear Data",
                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10 + 4 * (buttonWidth + buttonSpacing), 5, buttonWidth, buttonHeight,
                    hwnd, (HMENU)IDC_CLEAR_BUTTON, hInst, NULL
            );
            SendMessage(hClearButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            // Enable dark mode for the title bar
            BOOL useDarkMode = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

            // Set window corner preference to rounded (Windows 11)
            DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

            scrollPos = 0;
            scrollMax = 0;

            int contentStartY = headerHeight + static_cast<int>(20 * dpiScaleY);
            SetWindowPos(hWnd, NULL, 0, contentStartY, windowWidth, windowHeight - contentStartY, SWP_NOZORDER | SWP_NOMOVE);

            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = 1001;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_APP + 1;
            nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
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

            // Scroll bar position
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int scrollBarX = clientRect.right - SCROLL_BAR_WIDTH;
            int scrollBarY = 0;
            int scrollBarHeight = clientRect.bottom - clientRect.top;

            // Thumb position
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

            // Determine the scroll amount
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
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_CLEAR_BUTTON: {
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);

                        // Clear active time, paths, and start time
                        appActiveTime.clear();
                        appPaths.clear();
                        appStartTime.clear();

                        // Reset tracking for the current app
                        if (!currentAppName.empty()) {
                            auto now = std::chrono::system_clock::now();
                            appStartTime[currentAppName] = now;
                            appActiveTime[currentAppName] = std::chrono::seconds(0);
                        }

                        // Save the cleared data as an empty JSON structure
                        json j;
                        std::ofstream file("tracking_data.json");
                        if (file.is_open()) {
                            file << j.dump(4); // Save as empty object
                            file.close();
                        }

                        // Debug output to ensure correct reset
                        std::time_t startTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        std::string startTimeStr = std::ctime(&startTime);
                        OutputDebugStringA(("Start time after clearing: " + startTimeStr).c_str());
                    }

                    // Immediately invalidate the window to trigger a repaint with new values
                    InvalidateRect(hwnd, NULL, TRUE);

                    break;
                }
                case IDC_BUTTON_TODAY:
                    selectedTimeRange = TODAY;
                    InvalidateRect(hwnd, NULL, TRUE); // Redraw the window
                    break;

                case IDC_BUTTON_3DAYS:
                    selectedTimeRange = LAST_3_DAYS;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;

                case IDC_BUTTON_WEEK:
                    selectedTimeRange = LAST_WEEK;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;

                case IDC_BUTTON_MONTH:
                    selectedTimeRange = LAST_MONTH;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
            }
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            Bitmap bufferBitmap(ps.rcPaint.right, ps.rcPaint.bottom);
            Graphics bufferGraphics(&bufferBitmap);

            bufferGraphics.SetSmoothingMode(SmoothingModeAntiAlias);
            bufferGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            bufferGraphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            bufferGraphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            bufferGraphics.SetCompositingQuality(CompositingQualityHighQuality);

            SolidBrush backgroundBrush(Color(25, 25, 25));
            bufferGraphics.FillRectangle(&backgroundBrush, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom);

            SolidBrush textBrush(Color(255, 255, 255));

            std::lock_guard<std::mutex> lock(dataMutex); // Lock data during painting

            // Get the current time filter based on selected time range
            auto timeFilter = GetStartTimeForRange(selectedTimeRange);

            // Ensure there is data to display
            if (appActiveTime.empty()) {
                std::wstring emptyMessage = L"No application data available.";
                Font font(L"Segoe UI", static_cast<REAL>(12 * dpiScaleY)); // Adjust font for DPI
                bufferGraphics.DrawString(emptyMessage.c_str(), -1, &font, PointF(10.0f, 10.0f), &textBrush);
            } else {
                std::vector<std::pair<std::string, std::chrono::seconds>> apps(appActiveTime.begin(), appActiveTime.end());
                std::sort(apps.begin(), apps.end(), [](const auto& a, const auto& b) -> bool {
                    return a.second > b.second;
                });

                const int LIST_PADDING = static_cast<int>(20 * dpiScaleY);
                int contentHeight = 0;
                int iconSize = static_cast<int>(32 * dpiScaleX);
                int yIncrement = iconSize + static_cast<int>(15 * dpiScaleY);

                for (const auto& entry : apps) {
                    if (appStartTime[entry.first] < timeFilter) {
                        continue;
                    }
                    contentHeight += yIncrement;
                }
                contentHeight += LIST_PADDING;

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                int windowHeight = clientRect.bottom - clientRect.top;

                if (contentHeight > windowHeight) {
                    scrollMax = contentHeight - windowHeight;
                } else {
                    scrollMax = 0;
                }

                int xPos = static_cast<int>(10 * dpiScaleX);
                int yPos = static_cast<int>(30 * dpiScaleY) - scrollPos;

                std::chrono::seconds totalTime(0);
                for (const auto& entry : appActiveTime) {
                    totalTime += entry.second;
                }
                if (totalTime.count() == 0) {
                    totalTime = std::chrono::seconds(1); // Avoid division by zero
                }

                Font font(L"Segoe UI", static_cast<REAL>(10 * dpiScaleY));

                for (const auto& entry : apps) {
                    std::string appName = entry.first;

                    if (appStartTime.find(appName) == appStartTime.end() || appStartTime[appName] < timeFilter) {
                        continue;
                    }

                    auto appTime = entry.second;
                    std::string appPath = appPaths[appName];

                    HICON hIconLarge = NULL;
                    UINT iconCount = ExtractIconExA(appPath.c_str(), 0, &hIconLarge, NULL, 1);
                    if (iconCount == 0 || !hIconLarge) {
                        hIconLarge = LoadIcon(NULL, IDI_APPLICATION);
                    }

                    Bitmap* pIconBitmap = Bitmap::FromHICON(hIconLarge);
                    Bitmap* pResizedIcon = ResizeBitmap(pIconBitmap, iconSize, iconSize);

                    delete pIconBitmap;
                    DestroyIcon(hIconLarge);

                    int iconX = xPos;
                    int iconY = yPos + static_cast<int>(15 * dpiScaleY) - 6;
                    int textX = xPos + iconSize + static_cast<int>(5 * dpiScaleX);
                    int appNameY = yPos + static_cast<int>(3 * dpiScaleY) + 5;
                    int barX = textX;
                    int barY = yPos + iconSize + static_cast<int>(1 * dpiScaleY) - 5;
                    int timeY = barY - static_cast<int>(6 * dpiScaleY);

                    if (pResizedIcon) {
                        bufferGraphics.DrawImage(pResizedIcon, Rect(iconX, iconY, iconSize, iconSize));
                        delete pResizedIcon;
                    }

                    std::wstring wAppName(appName.begin(), appName.end());
                    bufferGraphics.DrawString(wAppName.c_str(), -1, &font, PointF(static_cast<REAL>(textX), static_cast<REAL>(appNameY)), &textBrush);

                    double percentage = static_cast<double>(appTime.count()) / totalTime.count();
                    int barMaxWidth = std::min(300, static_cast<int>(ps.rcPaint.right - barX - 20 * dpiScaleX));
                    int targetBarWidth = std::max(static_cast<int>(percentage * barMaxWidth), MIN_BAR_WIDTH); // Ensure the bar has at least MIN_BAR_WIDTH
                    UpdateBarWidth(appName, targetBarWidth);

                    int animatedBarWidth = currentBarWidths[appName];
                    Rect barRect(barX, barY, animatedBarWidth, static_cast<int>(8 * dpiScaleY));

                    LinearGradientBrush gradientBrush(
                            Point(barRect.X, barRect.Y),
                            Point(barRect.X, barRect.Y + barRect.Height),
                            LESS_AGGRESSIVE_GRADIENT_START,
                            LESS_AGGRESSIVE_GRADIENT_END
                    );

                    DrawRoundedRectangle(bufferGraphics, gradientBrush, barRect, static_cast<int>(3 * dpiScaleX));

                    std::string timeStr = FormatDuration(appTime);
                    std::wstring wTimeStr(timeStr.begin(), timeStr.end());
                    bufferGraphics.DrawString(wTimeStr.c_str(), -1, &font, PointF(static_cast<REAL>(barX + animatedBarWidth + static_cast<int>(5 * dpiScaleX)), static_cast<REAL>(timeY)), &textBrush);

                    yPos += yIncrement;
                }
            }

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int scrollBarX = clientRect.right - SCROLL_BAR_WIDTH;
            int scrollBarY = 0;
            int scrollBarHeight = clientRect.bottom - clientRect.top;

            SolidBrush scrollBarBgBrush(DARK_SCROLL_BAR_BACKGROUND_COLOR);
            bufferGraphics.FillRectangle(&scrollBarBgBrush, scrollBarX, scrollBarY, SCROLL_BAR_WIDTH, scrollBarHeight);

            double proportion = (double)scrollPos / (double)(scrollMax > 0 ? scrollMax : 1);
            int thumbY = static_cast<int>((scrollBarHeight - THUMB_HEIGHT) * proportion);
            if (scrollMax == 0) thumbY = 0;

            LinearGradientBrush thumbGradientBrush(
                    Point(scrollBarX, thumbY),
                    Point(scrollBarX, thumbY + THUMB_HEIGHT),
                    DARK_SCROLL_BAR_THUMB_COLOR,
                    LESS_AGGRESSIVE_GRADIENT_END
            );

            Rect thumbRect(scrollBarX, thumbY, SCROLL_BAR_WIDTH, THUMB_HEIGHT);
            DrawRoundedRectangle(bufferGraphics, thumbGradientBrush, thumbRect, static_cast<int>(5 * dpiScaleX));

            Graphics graphics(hdc);
            graphics.DrawImage(&bufferBitmap, 0, 0);

            EndPaint(hwnd, &ps);
            break;
        }
        case WM_VSCROLL: {
            // Not needed for custom scroll bar, but kept for compatibility
            break;
        }
        case WM_APP + 1: {
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 1, "Show/Hide");
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 2, isPaused ? "Resume" : "Pause");
                    InsertMenu(hMenu, -1, MF_BYPOSITION, 3, "Kill");
                    SetForegroundWindow(hwnd);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                    if (cmd == 1) {
                        if (IsWindowVisible(hwnd)) {
                            ShowWindow(hwnd, SW_HIDE);
                        } else {
                            ShowWindow(hwnd, SW_SHOW);
                        }
                    } else if (cmd == 2) {
                        isPaused = !isPaused;
                    } else if (cmd == 3) {  // "Kill" selected
                        isRunning = false;

                        SaveTrackingDataToFile("tracking_data.json");

                        PostMessage(hwnd, WM_DESTROY, 0, 0);
                    }
                    DestroyMenu(hMenu);
                }
            }
            break;
        }
        case WM_DESTROY: {
            SaveTrackingDataToFile("tracking_data.json");
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
