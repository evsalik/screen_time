cmake_minimum_required(VERSION 3.27)
project(screen_time_tracker)

set(CMAKE_CXX_STANDARD 17)

add_executable(screen_time_tracker WIN32
        main.cpp
        WindowManager.cpp
        Tracker.cpp
        Resource.rc
        FormatUtils.cpp
)

target_link_libraries(screen_time_tracker
        Wtsapi32
        User32
        Psapi
        Gdi32
        Gdiplus
        Dwmapi
        Shcore
)

