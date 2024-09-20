#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <windows.h>

void RegisterMainWindowClass(HINSTANCE hInstance);
HWND CreateMainWindow(HINSTANCE hInstance);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
