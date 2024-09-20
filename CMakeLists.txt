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
)

# Comment out or remove the subsystem linker flags
# Set the subsystem linker flag based on the compiler
# if (MINGW)
#     set_target_properties(screen_time PROPERTIES LINK_FLAGS "-Wl,-subsystem,windows")
# else()
#     set_target_properties(screen_time PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
# endif()