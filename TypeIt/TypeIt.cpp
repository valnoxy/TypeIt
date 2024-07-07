#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <thread>

#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_OPTION1 1002
#define ID_TRAY_OPTION2 1003

HINSTANCE hInst;
NOTIFYICONDATA nid;
HWND hWnd;
bool pressEnter = true;

// Read stored registry value
bool ReadRegistry(const wchar_t* keyPath, const wchar_t* valueName) {
    HKEY hKey;
    DWORD dwType = REG_DWORD;
    DWORD dwData = 0;
    DWORD dwDataSize = sizeof(DWORD);

    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        result = RegQueryValueEx(hKey, valueName, nullptr, &dwType, reinterpret_cast<BYTE*>(&dwData), &dwDataSize);
        RegCloseKey(hKey);
    }

    return (result == ERROR_SUCCESS && dwData != 0); // Return true if value exists and is non-zero
}

// Write stored registry value
bool WriteRegistry(bool value) {
    const wchar_t* keyPath = L"SOFTWARE\\valnoxy\\TypeIt";
    const wchar_t* valueName = L"PressEnter";

	HKEY hKey;
    DWORD dwDisposition;

    LONG result = RegCreateKeyEx(
        HKEY_CURRENT_USER,
        keyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        &dwDisposition
    );

    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to create registry key." << '\n';
        return false;
    }

    DWORD data = value ? 1 : 0;
    result = RegSetValueEx(
        hKey,
        valueName,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&data),
        sizeof(DWORD)
    );

    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to set registry value." << '\n';
        return false;
    }

    return true;
}

std::wstring GetClipboardText() {
    if (!OpenClipboard(nullptr)) {
        return L"";
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) {
        CloseClipboard();
        return L"";
    }

    auto pwszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pwszText == nullptr) {
        CloseClipboard();
        return L"";
    }

    std::wstring text(pwszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

// Simulate Keyboard Input
void SimulateKeyboardInput(const std::wstring& text) {
    for (wchar_t c : text) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;

        if (c == L'\n') {
            input.ki.wVk = VK_RETURN;
            input.ki.dwFlags = 0;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        else {
            input.ki.wVk = 0;
            input.ki.wScan = c;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }

		// Workaround for preventing typing too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

	// Press Enter if option is enabled
	if (pressEnter)
	{
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
		INPUT input = { 0 };
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = VK_RETURN;
		input.ki.dwFlags = 0;
		SendInput(1, &input, sizeof(INPUT));
		input.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &input, sizeof(INPUT));
	}
}

// Tray Icon
void CreateTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"TypeIt");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        // Option 1
        UINT uCheck1 = (pressEnter) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck1, ID_TRAY_OPTION1, L"CTRL-V + ENTER");

        // Option 2
        UINT uCheck2 = (!pressEnter) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck2, ID_TRAY_OPTION2, L"CTRL-V");

        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"Exit");

		// Display context menu
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_HOTKEY:
        if (wParam == 1) {
            std::wstring clipboardText = GetClipboardText();
            SimulateKeyboardInput(clipboardText);
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
	        case ID_TRAY_OPTION1:
                pressEnter = true;
                WriteRegistry(true);
	            break;
	        case ID_TRAY_OPTION2:
                pressEnter = false;
                WriteRegistry(false);
	            break;
	        case ID_TRAY_EXIT:
	            PostQuitMessage(0);
	            break;
			default: 
	            break;
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

// Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;
    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TypeItClass";

    RegisterClass(&wc);

    pressEnter = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"PressEnter");

    hWnd = CreateWindowEx(
        0,
        L"TypeItClass",
        L"TypeIt",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (hWnd == nullptr) {
        return 0;
    }

    if (!RegisterHotKey(hWnd, 1, MOD_CONTROL, 'B')) {
        MessageBox(nullptr, L"Failed to register hotkey! Make sure that no other application is already using the CTRL-B hotkey.", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
	CreateTrayIcon(hWnd);

    MSG msg = { nullptr };
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(hWnd, 1);
    return 0;
}
