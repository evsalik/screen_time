#include "Tracker.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <sstream>

extern std::map<std::string, std::chrono::system_clock::time_point> appStartTime;

std::mutex dataMutex;
std::map<std::string, std::chrono::seconds> appActiveTime;
std::map<std::string, std::string> appPaths;
std::string currentAppName = "";
std::string currentAppPath = "";
extern HWND hWnd;
extern bool isRunning;
extern bool isPaused;

void StartTrackingThread() {
    std::thread trackingThread([]() {
        while (isRunning) {
            if (!isPaused) {
                HWND hwnd = GetForegroundWindow();
                if (hwnd != NULL) {
                    auto [appName, appPath] = GetAppNameAndPathFromWindow(hwnd);
                    if (!appName.empty()) {
                        std::lock_guard<std::mutex> lock(dataMutex);

                        // Check if the app changed
                        if (appName != currentAppName) {
                            // Update the old app's time
                            if (!currentAppName.empty()) {
                                auto now = std::chrono::system_clock::now();
                                auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                                        now - appStartTime[currentAppName]);
                                appActiveTime[currentAppName] += duration;
                                appPaths[currentAppName] = currentAppPath;
                            }

                            // Reset tracking for the new app
                            currentAppName = appName;
                            currentAppPath = appPath;
                            appStartTime[currentAppName] = std::chrono::system_clock::now();
                        } else {
                            // Update the active time for the current app
                            auto now = std::chrono::system_clock::now();
                            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                                    now - appStartTime[currentAppName]);
                            appActiveTime[currentAppName] += duration;
                            appStartTime[currentAppName] = now; // Reset the start time to "now"
                        }
                    }
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    trackingThread.detach();
}

std::pair<std::string, std::string> GetAppNameAndPathFromWindow(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, processId);
    if (hProcess) {
        char exePath[MAX_PATH];
        if (GetModuleFileNameExA(hProcess, NULL, exePath, MAX_PATH)) {
            CloseHandle(hProcess);
            std::string fullPath(exePath);
            size_t pos = fullPath.find_last_of("\\/");
            std::string appName = fullPath.substr(pos + 1);
            return { appName, fullPath };
        }
        CloseHandle(hProcess);
    }
    return { "Unknown", "" };
}
