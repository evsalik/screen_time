#ifndef TRACKER_H
#define TRACKER_H

#include <windows.h>
#include <string>
#include <utility>
#include <chrono>
#include <mutex>
#include <map>

extern std::mutex dataMutex;
extern std::map<std::string, std::chrono::seconds> appActiveTime;
extern std::map<std::string, std::string> appPaths;
extern std::string currentAppName;
extern std::string currentAppPath;

std::pair<std::string, std::string> GetAppNameAndPathFromWindow(HWND hwnd);
void StartTrackingThread();
std::string FormatDuration(std::chrono::seconds duration);

#endif
