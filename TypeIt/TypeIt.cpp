#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define WM_CLOSE_POPUP (WM_USER + 1)
#define APP_VERSION L"1.1.1.62"

enum
{
	ID_TRAY_EXIT = 1001,
	ID_TRAY_OPTION1 = 1002, // CTRL-V + ENTER
	ID_TRAY_OPTION2 = 1003, // CTRL-V
	ID_TRAY_OPTION3 = 1004  // Disable new line at the end
};

HINSTANCE h_inst;
NOTIFYICONDATA nid;
HWND h_wnd;
bool press_enter = true;
bool disable_new_line = false;
std::wstring lastClipboardText;

enum options
{
    PressEnter,
    DisableNewLine
};

constexpr wchar_t POPUP_TEXT[] = L"There are more than 50 characters in your clipboard.\nPress CTRL-B again to continue.";
HWND hwndPopupLocal = nullptr;

LRESULT CALLBACK PopupWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE_POPUP:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        hwndPopupLocal = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ClosePopupAfterDelay(HWND hwnd) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (hwnd) {
        PostMessage(hwnd, WM_CLOSE_POPUP, 0, 0);
    }
}

HWND CreatePopupWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = PopupWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PopupWindowClass";
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));

    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    int windowWidth = 400;
    int windowHeight = 60;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = screenHeight - windowHeight - 50;

    hwndPopupLocal = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PopupWindowClass",
        NULL,
        WS_POPUP,
        xPos, yPos, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL
    );

    if (hwndPopupLocal) {
        SetLayeredWindowAttributes(hwndPopupLocal, 0, (BYTE)(255 * 0.75), LWA_ALPHA);
        ShowWindow(hwndPopupLocal, SW_SHOW);

        HDC hdc = GetDC(hwndPopupLocal);

        HFONT hFont = CreateFont(
            20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
        );
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        RECT rect;
        GetClientRect(hwndPopupLocal, &rect);

        DrawText(hdc, POPUP_TEXT, -1, &rect, DT_CALCRECT | DT_WORDBREAK);
        int textHeight = rect.bottom - rect.top;

        // Center horizontal
        rect.top = (windowHeight - textHeight) / 2;
        rect.bottom = rect.top + textHeight;

        // Calculate width of text
        int textWidth = rect.right - rect.left;
        rect.left = (windowWidth - textWidth) / 2;
        rect.right = rect.left + textWidth;

        // Draw the text
        DrawText(hdc, POPUP_TEXT, -1, &rect, DT_WORDBREAK);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(hwndPopupLocal, hdc);

        // Thread to send a message to close the window after 5 seconds
        std::thread(ClosePopupAfterDelay, hwndPopupLocal).detach();
    }

    return hwndPopupLocal;
}
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
bool WriteRegistry(options option, bool value) {
    const wchar_t* keyPath = L"SOFTWARE\\valnoxy\\TypeIt";
    LPCWSTR valueName = nullptr;

    switch (option)
    {
    case PressEnter:
        valueName = L"PressEnter";
        break;
    case DisableNewLine:
        valueName = L"DisableNewLine";
        break;
    }

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

std::wstring GetClipboardText(bool disable_new_line = true) {
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

    if (disable_new_line) {
        if (!text.empty() && text.back() == L'\n') {
            text.pop_back();
            if (!text.empty() && text.back() == L'\r') {
                text.pop_back();
            }
        }
    }

    if (text.length() > 50 && text != lastClipboardText) {
        CreatePopupWindow(GetModuleHandle(nullptr));
        lastClipboardText = text;
        return L""; // Abort current operation
    }

    lastClipboardText = text;
    return text;
}

bool IsNoKeyCurrentlyPressed() {
    // Check relevant keys: Ctrl, Shift, Alt
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return false;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) return false;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) return false;

    return true;
}

// Simulate Keyboard Input
void SimulateKeyboardInput(const std::wstring& text) {
    // Wait until no relevant key is pressed
    while (!IsNoKeyCurrentlyPressed()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

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
    if (press_enter) {
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
    nid.hIcon = LoadIcon(h_inst, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"TypeIt");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        std::wstring fullVersion = std::wstring(L"TypeIt v") + APP_VERSION;
        LPCWSTR combinedString = fullVersion.c_str();
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, combinedString);
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        // Option 1
        UINT uCheck1 = (press_enter) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck1, ID_TRAY_OPTION1, L"CTRL-V + ENTER");

        // Option 2
        UINT uCheck2 = (!press_enter) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck2, ID_TRAY_OPTION2, L"CTRL-V");

        // Disable New Line at the end
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        UINT uCheck3 = (disable_new_line) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck3, ID_TRAY_OPTION3, L"Disable new line at the end");
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"Exit");

        // Display context menu
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);
    }
}

void ProcessRawInput(LPARAM lParam) {
    UINT dwSize = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));

    if (dwSize == 0) return;

    LPBYTE lpb = new BYTE[dwSize];
    if (lpb == nullptr) return;

    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb);

        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            RAWKEYBOARD& rawKeyboard = raw->data.keyboard;
            UINT virtualKey = rawKeyboard.VKey;
            UINT flags = rawKeyboard.Flags;

            bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool bKeyPressed = (virtualKey == 'B') && !(flags & RI_KEY_BREAK); // RI_KEY_BREAK: key up

            if (ctrlPressed && bKeyPressed) {
                SimulateKeyboardInput(GetClipboardText());
            }
        }
    }

    delete[] lpb;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INPUT:
        ProcessRawInput(lParam);
        break;
    case WM_HOTKEY:
        if (wParam == 1) {
            std::wstring clipboardText = GetClipboardText();
            SimulateKeyboardInput(clipboardText);
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hWnd);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPTION1:
            press_enter = true;
            WriteRegistry(PressEnter, true);
            break;
        case ID_TRAY_OPTION2:
            press_enter = false;
            WriteRegistry(PressEnter, false);
            break;
        case ID_TRAY_OPTION3:
            disable_new_line = !disable_new_line;
            WriteRegistry(DisableNewLine, disable_new_line);
            break;
        case ID_TRAY_EXIT:
            Shell_NotifyIcon(NIM_DELETE, &nid);
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
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}


// Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    h_inst = hInstance;
    WNDCLASS wc = { 0 };
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TypeItClass";

    RegisterClass(&wc);

    press_enter = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"PressEnter");
    disable_new_line = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"DisableNewLine");

    h_wnd = CreateWindowEx(
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

    if (h_wnd == nullptr) {
        return 0;
    }

    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;         // Desktop control
    rid.usUsage = 0x06;             // Keyboard
    rid.dwFlags = RIDEV_INPUTSINK;  // Background
    rid.hwndTarget = h_wnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        MessageBox(nullptr, L"Failed to register raw input device", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    CreateTrayIcon(h_wnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

