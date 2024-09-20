#include <windows.h>
#include <wtsapi32.h>
#include <iostream>

int main() {
    // Simple test to ensure libraries are linked
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    std::cout << "Active Session ID: " << sessionId << std::endl;
    return 0;
}
