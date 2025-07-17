#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define WM_CLOSE_POPUP (WM_USER + 1)
#define APP_VERSION L"1.1.2.69"

enum
{
    ID_TRAY_EXIT = 1001,
    ID_TRAY_OPTION1 = 1002, // CTRL-V + ENTER
    ID_TRAY_OPTION2 = 1003, // CTRL-V
    ID_TRAY_OPTION3 = 1004, // Disable new line at the end
    ID_TRAY_OPTION4 = 1005, // Use RegisterHotkey
    ID_TRAY_OPTION5 = 1006, // Use RawInput
    HOTKEY_ID_STOP = 2,     // Hotkey ID for ESC
};

HINSTANCE h_inst;
NOTIFYICONDATA nid;
HWND h_wnd;
std::wstring lastClipboardText;
volatile bool press_enter = true;
volatile bool disable_new_line = false;
volatile bool useRawInput = false;
volatile bool cancelTyping = false;

std::atomic<HWND> hwndPopupGlobal = ATOMIC_VAR_INIT(nullptr);
std::thread popupCloseThread;

enum options
{
    PressEnter,
    DisableNewLine,
    UseRawInput
};

static LRESULT CALLBACK PopupWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE_POPUP:
        if (hwndPopupGlobal.load() == hwnd) {
            hwndPopupGlobal.store(nullptr);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        const wchar_t* popupText = reinterpret_cast<const wchar_t*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!popupText) {
            popupText = L"";
        }

        HFONT hFont = CreateFont(
            20,
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
        );
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        RECT textRect = clientRect; 

        DrawText(hdc, popupText, -1, &textRect, DT_CALCRECT | DT_WORDBREAK);

        int textHeight = textRect.bottom - textRect.top;
        int clientHeight = clientRect.bottom - clientRect.top;

        if (textHeight < clientHeight) {
            textRect.top = clientRect.top + (clientHeight - textHeight) / 2;
            textRect.bottom = textRect.top + textHeight;
        }
        else {
            textRect.top = clientRect.top;
            textRect.bottom = clientRect.bottom;
        }
        textRect.left = clientRect.left;
        textRect.right = clientRect.right;

        DrawText(hdc, popupText, -1, &textRect, DT_WORDBREAK | DT_CENTER);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
    }
    break;
    case WM_DESTROY:
        if (hwndPopupGlobal.load() == hwnd) {
            hwndPopupGlobal.store(nullptr);
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void CloseExistingPopup() {
    HWND existingPopup = hwndPopupGlobal.load();
    if (existingPopup) {
        PostMessage(existingPopup, WM_CLOSE_POPUP, 0, 0);
    }
}

static void AutoClosePopupAfterDelay(HWND hwnd, int delaySeconds) {
    std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    if (hwndPopupGlobal.load() == hwnd) { 
        PostMessage(hwnd, WM_CLOSE_POPUP, 0, 0);
    }
}

static HWND CreatePopup(HINSTANCE hInstance, const std::wstring& text, int autoCloseDelaySeconds = 0) {
    CloseExistingPopup();

    WNDCLASS wc = {};
    wc.lpfnWndProc = PopupWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PopupWindowClass";
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));

    if (!GetClassInfo(hInstance, L"PopupWindowClass", &wc)) {
        if (!RegisterClass(&wc)) {
            return nullptr;
        }
    }

    int windowWidth = 400;
    int windowHeight = 50;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = screenHeight - windowHeight - 50;

    HWND newPopup = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PopupWindowClass",
        NULL,
        WS_POPUP,
        xPos, yPos, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL
    );

    if (newPopup) {
        SetWindowLongPtr(newPopup, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(text.c_str()));

        SetLayeredWindowAttributes(newPopup, 0, (BYTE)(255 * 0.75), LWA_ALPHA);
        ShowWindow(newPopup, SW_SHOWNA);
        UpdateWindow(newPopup);

        hwndPopupGlobal.store(newPopup);

        if (autoCloseDelaySeconds > 0) {
            if (popupCloseThread.joinable()) {
                popupCloseThread.detach();
            }
            popupCloseThread = std::thread(AutoClosePopupAfterDelay, newPopup, autoCloseDelaySeconds);
            popupCloseThread.detach();
        }
    }

    return newPopup;
}

static void RestartApplication(HWND hWnd) {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    PostQuitMessage(0);
    ShellExecuteW(NULL, L"open", szPath, NULL, NULL, SW_SHOWNORMAL);
}

// Read stored registry value
static bool ReadRegistry(const wchar_t* keyPath, const wchar_t* valueName) {
    HKEY hKey;
    DWORD dwType = REG_DWORD;
    DWORD dwData = 0;
    DWORD dwDataSize = sizeof(DWORD);

    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        result = RegQueryValueEx(hKey, valueName, nullptr, &dwType, reinterpret_cast<BYTE*>(&dwData), &dwDataSize);
        RegCloseKey(hKey);
    }

    return (result == ERROR_SUCCESS && dwData != 0);
}

// Write stored registry value
static bool WriteRegistry(options option, bool value) {
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
    case UseRawInput:
        valueName = L"UseRawInput";
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
        OutputDebugStringW(L"Failed to create registry key.\n");
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
        OutputDebugStringW(L"Failed to set registry value.\n");
        return false;
    }

    return true;
}

static std::wstring GetClipboardText(bool disable_new_line_param = true) {
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

    if (disable_new_line_param) { // Use the parameter here
        if (!text.empty() && text.back() == L'\n') {
            text.pop_back();
            if (!text.empty() && text.back() == L'\r') {
                text.pop_back();
            }
        }
    }

    if (text.length() > 50 && text != lastClipboardText) {
        CreatePopup(h_inst, L"There are more than 50 characters in your clipboard.\nPress CTRL-B again to continue.", 5);
        lastClipboardText = text;
        return L""; // Abort current operation
    }

    lastClipboardText = text;
    return text;
}

static bool IsNoKeyCurrentlyPressed()
{
    // Check relevant keys: Ctrl, Shift, Alt
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return false;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) return false;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) return false;

    return true;
}

static void SimulateKeyboardInput(const std::wstring& text) {
    cancelTyping = false;

    CreatePopup(h_inst, L"Typing content...\nPress ESC to abort.", 0);

    while (!IsNoKeyCurrentlyPressed()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (cancelTyping) {
            OutputDebugStringW(L"Typing cancelled during initial key check.\n");
            CloseExistingPopup();
            return;
        }
    }

    for (wchar_t c : text) {
        if (cancelTyping) {
            OutputDebugStringW(L"Typing cancelled by user during character input.\n");
            CloseExistingPopup();
            return;
        }

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

        // Ugly fix for preventing typing too fast
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (cancelTyping) {
        OutputDebugStringW(L"Typing cancelled before final enter.\n");
        CloseExistingPopup();
        return;
    }

    if (press_enter) {
        for (int i = 0; i < 50; ++i) { // 500ms total, 10ms per check
            if (cancelTyping) {
                OutputDebugStringW(L"Typing cancelled during final enter delay.\n");
                CloseExistingPopup();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RETURN;
        input.ki.dwFlags = 0;
        SendInput(1, &input, sizeof(INPUT));
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
    OutputDebugStringW(L"Typing finished normally.\n");
    CloseExistingPopup();
}

// Tray Icon
static void CreateTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(h_inst, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"TypeIt");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

static void ShowContextMenu(HWND hwnd) {
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

        // Input options
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        UINT uCheck4 = (!useRawInput) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck4, ID_TRAY_OPTION4, L"Use RegisterHotkey");

        UINT uCheck5 = (useRawInput) ? MF_CHECKED : MF_UNCHECKED;
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING | uCheck5, ID_TRAY_OPTION5, L"Use RawInput");

        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"Exit");

        // Display context menu
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);
    }
}

static void ProcessRawInput(LPARAM lParam) {
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

            bool escKeyPressed = (virtualKey == VK_ESCAPE) && !(flags & RI_KEY_BREAK);

            if (ctrlPressed && bKeyPressed) {
                std::wstring clipboardText = GetClipboardText();
                if (!clipboardText.empty()) {
                    std::thread typingThread(SimulateKeyboardInput, clipboardText);
                    typingThread.detach();
                    OutputDebugStringW(L"(RAW) CTRL-B: Typing thread started.\n");
                }
            }
            else if (escKeyPressed) {
                cancelTyping = true; // Set the flag to cancel
                OutputDebugStringW(L"(RAW) ESC hotkey pressed, setting cancelTyping to true.\n");
            }
        }
    }

    delete[] lpb;
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INPUT:
        ProcessRawInput(lParam);
        break;
    case WM_HOTKEY:
        if (wParam == 1) {
            std::wstring clipboardText = GetClipboardText();
            if (!clipboardText.empty()) {
                std::thread typingThread(SimulateKeyboardInput, clipboardText);
                typingThread.detach();
                OutputDebugStringW(L"CTRL-B hotkey: Typing thread started.\n");
            }
        }
        else if (wParam == HOTKEY_ID_STOP) {
            cancelTyping = true; 
            OutputDebugStringW(L"ESC hotkey pressed, setting cancelTyping to true.\n");
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
        case ID_TRAY_OPTION4:
            useRawInput = false;
            WriteRegistry(UseRawInput, false);
            MessageBox(nullptr, L"Changed to Hotkey Mode. TypeIt will now restart to apply changes.", L"TypeIt Information", MB_OK | MB_ICONINFORMATION);
            RestartApplication(hWnd);
            break;
        case ID_TRAY_OPTION5:
            useRawInput = true;
            WriteRegistry(UseRawInput, true);
            MessageBox(nullptr, L"Changed to Raw Input Mode. TypeIt will now restart to apply changes.", L"TypeIt Information", MB_OK | MB_ICONINFORMATION);
            RestartApplication(hWnd);
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
    OutputDebugStringW(L"Starting TypeIt\n");
	h_inst = hInstance;
    WNDCLASS wc = { 0 };
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TypeItClass";

    RegisterClass(&wc);

    press_enter = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"PressEnter");
    disable_new_line = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"DisableNewLine");
    useRawInput = ReadRegistry(L"SOFTWARE\\valnoxy\\TypeIt", L"UseRawInput");

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

    // Register Hotkey methods
    if (useRawInput)
    {
        RAWINPUTDEVICE rid[2]; // Array for multiple devices

        // Keyboard for Ctrl+B detection
        rid[0].usUsagePage = 0x01;          // Desktop control
        rid[0].usUsage = 0x06;              // Keyboard
        rid[0].dwFlags = RIDEV_INPUTSINK;   // Background
        rid[0].hwndTarget = h_wnd;

        if (!RegisterRawInputDevices(&rid[0], 1, sizeof(rid[0]))) { // Register only the first device
            MessageBox(nullptr, L"Failed to register raw input device!", L"TypeIt Error", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    else
    {
        if (!RegisterHotKey(h_wnd, 1, MOD_CONTROL, 'B')) {
            MessageBox(nullptr, L"Failed to register hotkey! Make sure that no other application is already using the CTRL-B hotkey.", L"TypeIt Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (!RegisterHotKey(h_wnd, HOTKEY_ID_STOP, 0, VK_ESCAPE)) {
           MessageBox(nullptr, L"Failed to register ESC hotkey.", L"TypeIt Warning", MB_OK | MB_ICONWARNING);
        }
    }

    CreateTrayIcon(h_wnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (!useRawInput)
    {
        UnregisterHotKey(h_wnd, 1);
        UnregisterHotKey(h_wnd, HOTKEY_ID_STOP);
    }
    return 0;
}