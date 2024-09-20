cmake_minimum_required(VERSION 3.27)
project(screen_time)

set(CMAKE_CXX_STANDARD 17)

add_executable(screen_time WIN32
        main.cpp
        WindowManager.cpp
        Tracker.cpp
        Resource.rc
)

target_link_libraries(screen_time
        Wtsapi32
        User32
        Psapi
        Gdi32
        Gdiplus
        Dwmapi
)