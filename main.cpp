#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

static constexpr LPCWSTR REG_PATH = L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore";
static constexpr LPCWSTR REG_NAME = L"ShowDlssIndicator";
static constexpr LPCWSTR REGEDIT_APPLET_PATH =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit";
static constexpr LPCWSTR REGEDIT_TARGET_PATH =
    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore";
static constexpr LPCWSTR AUTOSTART_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr LPCWSTR AUTOSTART_VALUE = L"DlssIndicatorTray";

static constexpr UINT WM_TRAY_ICON = WM_APP + 1;
static constexpr UINT ID_MODE_OFF = 1001;
static constexpr UINT ID_MODE_GREEN = 1002;
static constexpr UINT ID_MODE_TEXT = 1003;
static constexpr UINT ID_REFRESH = 1004;
static constexpr UINT ID_OPEN_REGEDIT = 1005;
static constexpr UINT ID_EXIT = 1006;
static constexpr UINT ID_ENABLE_AUTOSTART = 1007;
static constexpr UINT ID_DISABLE_AUTOSTART = 1008;

static HWND g_window;
static HICON g_trayIcon;
static UINT g_taskbarCreated;
static BOOL g_valueExists;
static DWORD g_value;

static BOOL ReadRegValue(DWORD* outValue) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return FALSE;
    DWORD value = 0, size = sizeof(value), type = 0;
    LONG result = RegQueryValueExW(hKey, REG_NAME, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(hKey);
    if (result == ERROR_SUCCESS && type == REG_DWORD) {
        *outValue = value;
        return TRUE;
    }
    return FALSE;
}

static BOOL WriteRegValue(DWORD value) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, REG_PATH, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return FALSE;
    LONG result = RegSetValueExW(hKey, REG_NAME, 0, REG_DWORD,
                                 reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

static BOOL IsAutoStartEnabled(BOOL* enabled) {
    *enabled = FALSE;
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_QUERY_VALUE, &hKey);
    if (result == ERROR_FILE_NOT_FOUND) return TRUE;
    if (result != ERROR_SUCCESS) return FALSE;

    DWORD type = 0;
    DWORD size = 0;
    result = RegQueryValueExW(hKey, AUTOSTART_VALUE, nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    if (result == ERROR_FILE_NOT_FOUND) return TRUE;
    if (result != ERROR_SUCCESS || type != REG_SZ) return FALSE;

    *enabled = TRUE;
    return TRUE;
}

static BOOL SetAutoStartEnabled(BOOL enabled) {
    HKEY hKey;
    if (!enabled) {
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_SET_VALUE, &hKey);
        if (result == ERROR_FILE_NOT_FOUND) return TRUE;
        if (result != ERROR_SUCCESS) return FALSE;

        result = RegDeleteValueW(hKey, AUTOSTART_VALUE);
        RegCloseKey(hKey);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    WCHAR executablePath[MAX_PATH];
    DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, ARRAYSIZE(executablePath));
    if (!pathLength || pathLength >= ARRAYSIZE(executablePath)) return FALSE;

    WCHAR command[MAX_PATH + 3];
    if (swprintf_s(command, ARRAYSIZE(command), L"\"%ls\"", executablePath) < 0) return FALSE;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return FALSE;
    }
    LONG result = RegSetValueExW(hKey, AUTOSTART_VALUE, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(command),
                                 (lstrlenW(command) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

static BOOL WriteRegValueElevated(DWORD value, BOOL* wasCancelled) {
    if (value != 0 && value != 1 && value != 1024) return FALSE;

    WCHAR executablePath[MAX_PATH];
    DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, ARRAYSIZE(executablePath));
    if (!pathLength || pathLength >= ARRAYSIZE(executablePath)) return FALSE;

    WCHAR parameters[64];
    swprintf_s(parameters, L"--write-registry %lu", value);

    SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = executablePath;
    executeInfo.lpParameters = parameters;
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo)) {
        *wasCancelled = GetLastError() == ERROR_CANCELLED;
        return FALSE;
    }

    *wasCancelled = FALSE;
    DWORD exitCode = 1;
    BOOL succeeded = WaitForSingleObject(executeInfo.hProcess, INFINITE) == WAIT_OBJECT_0
        && GetExitCodeProcess(executeInfo.hProcess, &exitCode)
        && exitCode == 0;
    CloseHandle(executeInfo.hProcess);
    return succeeded;
}

static const BYTE* GetGlyph(DWORD value, BOOL exists) {
    static const BYTE oneGlyph[7] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static const BYTE textGlyph[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const BYTE unknownGlyph[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const BYTE missingGlyph[7] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    if (!exists) return missingGlyph;
    if (value == 1) return oneGlyph;
    if (value == 1024) return textGlyph;
    return unknownGlyph;
}

static HICON CreateStatusIcon() {
    DWORD color;
    if (!g_valueExists) color = RGB(120, 120, 120);
    else if (g_value == 0) color = RGB(220, 50, 47);
    else if (g_value == 1) color = RGB(52, 152, 219);
    else if (g_value == 1024) color = RGB(52, 152, 219);
    else color = RGB(205, 135, 35);

    BYTE mask[16 * 2];
    DWORD pixels[16 * 16] = {};
    for (int i = 0; i < ARRAYSIZE(mask); ++i) mask[i] = 0xff;

    const BOOL showOffText = g_valueExists && g_value == 0;
    const BOOL showOnText = g_valueExists && g_value == 1024;
    const BOOL showDevText = g_valueExists && g_value == 1;
    const BOOL showText = showOffText || showOnText;
    const BYTE* glyph = showText ? nullptr : GetGlyph(g_value, g_valueExists);
    static const BYTE offGlyph[9] = { 0x06, 0x0F, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0F, 0x06 };
    static const BYTE fGlyph[9] = { 0x0F, 0x0F, 0x08, 0x08, 0x0E, 0x0E, 0x08, 0x08, 0x08 };
    static const BYTE nGlyph[9] = { 0x09, 0x0D, 0x0D, 0x0B, 0x0B, 0x09, 0x09, 0x09, 0x09 };
    static const BYTE dGlyph[7] = { 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06 };
    static const BYTE eGlyph[7] = { 0x07, 0x04, 0x06, 0x04, 0x04, 0x04, 0x07 };
    static const BYTE vGlyph[7] = { 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x02 };
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (showDevText) {
                int textX = x - 2;
                int letterIndex = textX / 4;
                int glyphX = textX % 4;
                const BYTE* letter = letterIndex == 0 ? dGlyph
                    : letterIndex == 1 ? eGlyph : vGlyph;
                if (y >= 4 && y <= 10 && letterIndex >= 0 && letterIndex < 3
                    && glyphX >= 0 && glyphX < 3
                    && (letter[y - 4] & (0x04 >> glyphX))) {
                    mask[y * 2 + x / 8] &= static_cast<BYTE>(~(0x80 >> (x % 8)));
                    pixels[y * 16 + x] = RGB(GetBValue(color), GetGValue(color), GetRValue(color));
                }
                continue;
            }

            if (showText) {
                int textX = x - 1;
                int letterIndex = showOffText ? textX / 5 : (x - 3) / 5;
                int glyphX = showOffText ? textX % 5 : (x - 3) % 5;
                const BYTE* letter = showOffText
                    ? (letterIndex == 0 ? offGlyph : fGlyph)
                    : (letterIndex == 0 ? offGlyph : nGlyph);
                int letterCount = showOffText ? 3 : 2;
                DWORD textColor = showOffText ? RGB(220, 50, 47) : RGB(46, 204, 64);
                if (y >= 3 && y <= 11 && letterIndex >= 0 && letterIndex < letterCount
                    && glyphX >= 0 && glyphX < 4
                    && (letter[y - 3] & (0x08 >> glyphX))) {
                    mask[y * 2 + x / 8] &= static_cast<BYTE>(~(0x80 >> (x % 8)));
                    pixels[y * 16 + x] = RGB(GetBValue(textColor), GetGValue(textColor), GetRValue(textColor));
                }
                continue;
            }

            int dx = x - 7;
            int dy = y - 7;
            if (dx * dx + dy * dy > 49) continue;

            mask[y * 2 + x / 8] &= static_cast<BYTE>(~(0x80 >> (x % 8)));
            pixels[y * 16 + x] = RGB(GetBValue(color), GetGValue(color), GetRValue(color));
            if (x >= 5 && x <= 9 && y >= 4 && y <= 10
                && (glyph[y - 4] & (0x10 >> (x - 5)))) {
                pixels[y * 16 + x] = RGB(255, 255, 255);
            }
        }
    }
    return CreateIcon(nullptr, 16, 16, 1, 32, mask, reinterpret_cast<const BYTE*>(pixels));
}

static void GetStatusText(WCHAR* text, size_t textCount) {
    if (!g_valueExists) wcscpy_s(text, textCount, L"OFF");
    else if (g_value == 0) wcscpy_s(text, textCount, L"OFF");
    else if (g_value == 1) wcscpy_s(text, textCount, L"DEV (1)");
    else if (g_value == 1024) wcscpy_s(text, textCount, L"ON (1024)");
    else swprintf_s(text, textCount, L"Value %lu", g_value);
}

static BOOL UpdateTrayIcon(BOOL add) {
    DWORD value = 0;
    g_valueExists = ReadRegValue(&value);
    g_value = value;

    HICON icon = CreateStatusIcon();
    if (!icon) return FALSE;

    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_TRAY_ICON;
    data.hIcon = icon;

    WCHAR status[32];
    GetStatusText(status, ARRAYSIZE(status));
    swprintf_s(data.szTip, ARRAYSIZE(data.szTip), L"DlssIndicator: %ls\nLeft-click: turn %ls",
               status, g_valueExists && g_value == 0 ? L"ON" : L"OFF");

    BOOL succeeded = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
    if (succeeded) {
        HICON previousIcon = g_trayIcon;
        g_trayIcon = icon;
        if (previousIcon) DestroyIcon(previousIcon);
        if (add) {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
            data.uFlags = NIF_TIP | NIF_SHOWTIP;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
    } else {
        DestroyIcon(icon);
    }
    return succeeded;
}

static void ShowNotification(LPCWSTR title, LPCWSTR message, DWORD flags) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    wcscpy_s(data.szInfoTitle, title);
    wcscpy_s(data.szInfo, message);
    data.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

struct TrayMenuItem {
    LPCWSTR text;
    BOOL separator;
    BOOL disabled;
};

static BOOL IsDarkSystemTheme() {
    DWORD appsUseLightTheme = 1;
    DWORD size = sizeof(appsUseLightTheme);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &appsUseLightTheme, &size) == ERROR_SUCCESS) {
        return appsUseLightTheme == 0;
    }

    COLORREF windowColor = GetSysColor(COLOR_WINDOW);
    return (GetRValue(windowColor) * 299 + GetGValue(windowColor) * 587
            + GetBValue(windowColor) * 114) / 1000 < 128;
}

static BOOL AddTrayMenuItem(HMENU menu, UINT command, TrayMenuItem* item) {
    MENUITEMINFOW menuItem = {};
    menuItem.cbSize = sizeof(menuItem);
    menuItem.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
    menuItem.fType = MFT_OWNERDRAW;
    menuItem.fState = item->disabled || item->separator
        ? MFS_DISABLED | MFS_GRAYED : MFS_ENABLED;
    menuItem.wID = command;
    menuItem.dwItemData = reinterpret_cast<ULONG_PTR>(item);
    return InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &menuItem);
}

static UINT GetWindowDpi() {
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    static GetDpiForWindowFn getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT dpi = getDpiForWindow ? getDpiForWindow(g_window) : 96;
    return dpi ? dpi : 96;
}

static void MeasureTrayMenuItem(MEASUREITEMSTRUCT* measure) {
    const TrayMenuItem* item = reinterpret_cast<const TrayMenuItem*>(measure->itemData);
    UINT dpi = GetWindowDpi();

    measure->itemWidth = MulDiv(180, dpi, 96);
    measure->itemHeight = MulDiv(item->separator ? 9 : 26, dpi, 96);
    if (item->separator) return;

    HDC dc = GetDC(g_window);
    if (!dc) return;
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ previousFont = SelectObject(dc, font);
    SIZE textSize = {};
    GetTextExtentPoint32W(dc, item->text, lstrlenW(item->text), &textSize);
    SelectObject(dc, previousFont);
    ReleaseDC(g_window, dc);
    LONG textWidth = textSize.cx + MulDiv(44, dpi, 96);
    LONG textHeight = textSize.cy + MulDiv(8, dpi, 96);
    if (measure->itemWidth < static_cast<UINT>(textWidth)) measure->itemWidth = textWidth;
    if (measure->itemHeight < static_cast<UINT>(textHeight)) measure->itemHeight = textHeight;
}

static void DrawTrayMenuItem(const DRAWITEMSTRUCT* draw) {
    const TrayMenuItem* item = reinterpret_cast<const TrayMenuItem*>(draw->itemData);
    const BOOL dark = IsDarkSystemTheme();
    const BOOL selected = (draw->itemState & ODS_SELECTED) != 0 && !item->disabled;
    const COLORREF background = dark
        ? (selected ? RGB(62, 62, 62) : RGB(32, 32, 32))
        : (selected ? RGB(220, 235, 252) : RGB(249, 249, 249));
    const COLORREF foreground = item->disabled
        ? (dark ? RGB(128, 128, 128) : RGB(109, 109, 109))
        : (dark ? RGB(240, 240, 240) : RGB(32, 32, 32));
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(draw->hDC, &draw->rcItem, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (item->separator) {
        int y = (draw->rcItem.top + draw->rcItem.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(72, 72, 72) : RGB(210, 210, 210));
        HGDIOBJ previousPen = SelectObject(draw->hDC, pen);
        MoveToEx(draw->hDC, draw->rcItem.left + 8, y, nullptr);
        LineTo(draw->hDC, draw->rcItem.right - 8, y);
        SelectObject(draw->hDC, previousPen);
        DeleteObject(pen);
        return;
    }

    if (draw->itemState & ODS_CHECKED) {
        UINT dpi = GetWindowDpi();
        HPEN pen = CreatePen(PS_SOLID, MulDiv(2, dpi, 96), foreground);
        HGDIOBJ previousPen = SelectObject(draw->hDC, pen);
        int x = draw->rcItem.left + MulDiv(11, dpi, 96);
        int y = (draw->rcItem.top + draw->rcItem.bottom) / 2;
        MoveToEx(draw->hDC, x, y, nullptr);
        LineTo(draw->hDC, x + MulDiv(3, dpi, 96), y + MulDiv(3, dpi, 96));
        LineTo(draw->hDC, x + MulDiv(9, dpi, 96), y - MulDiv(4, dpi, 96));
        SelectObject(draw->hDC, previousPen);
        DeleteObject(pen);
    }

    RECT textRect = draw->rcItem;
    textRect.left += MulDiv(28, GetWindowDpi(), 96);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, foreground);
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ previousFont = SelectObject(draw->hDC, font);
    DrawTextW(draw->hDC, item->text, -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(draw->hDC, previousFont);
}

static void ShowTrayMenu() {
    UpdateTrayIcon(FALSE);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    WCHAR status[96];
    WCHAR statusLine[128];
    BOOL autoStartEnabled = FALSE;
    BOOL autoStartStateKnown = IsAutoStartEnabled(&autoStartEnabled);
    if (!autoStartStateKnown) {
        ShowNotification(L"DLSS Indicator", L"Could not read Windows startup setting.", NIIF_ERROR);
    }
    GetStatusText(status, ARRAYSIZE(status));
    swprintf_s(statusLine, L"Current: %ls", status);
    TrayMenuItem items[] = {
        { statusLine, FALSE, TRUE },
        { nullptr, TRUE, FALSE },
        { L"OFF (0)", FALSE, FALSE },
        { L"DEV (1)", FALSE, FALSE },
        { L"ON (1024)", FALSE, FALSE },
        { nullptr, TRUE, FALSE },
        { L"Refresh", FALSE, FALSE },
        { L"Open in Registry Editor", FALSE, FALSE },
        { nullptr, TRUE, FALSE },
        { L"Enable startup with Windows", FALSE, FALSE },
        { L"Disable startup with Windows", FALSE, FALSE },
        { nullptr, TRUE, FALSE },
        { L"Exit", FALSE, FALSE },
    };
    const UINT commands[] = {
        0, 0, ID_MODE_OFF, ID_MODE_GREEN, ID_MODE_TEXT, 0,
        ID_REFRESH, ID_OPEN_REGEDIT, 0, ID_ENABLE_AUTOSTART, ID_DISABLE_AUTOSTART, 0, ID_EXIT
    };
    for (size_t i = 0; i < ARRAYSIZE(items); ++i) {
        if (!AddTrayMenuItem(menu, commands[i], &items[i])) {
            DestroyMenu(menu);
            return;
        }
    }

    UINT checkedCommand = 0;
    if (g_valueExists && g_value == 0) checkedCommand = ID_MODE_OFF;
    else if (g_valueExists && g_value == 1) checkedCommand = ID_MODE_GREEN;
    else if (g_valueExists && g_value == 1024) checkedCommand = ID_MODE_TEXT;
    if (checkedCommand) {
        CheckMenuRadioItem(menu, ID_MODE_OFF, ID_MODE_TEXT, checkedCommand, MF_BYCOMMAND);
    }
    if (autoStartStateKnown) {
        CheckMenuRadioItem(menu, ID_ENABLE_AUTOSTART, ID_DISABLE_AUTOSTART,
                           autoStartEnabled ? ID_ENABLE_AUTOSTART : ID_DISABLE_AUTOSTART,
                           MF_BYCOMMAND);
    }

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, 0, g_window, nullptr);
    DestroyMenu(menu);

    PostMessageW(g_window, WM_NULL, 0, 0);
    if (command) PostMessageW(g_window, WM_COMMAND, command, 0);
}

static void ApplyValue(DWORD value) {
    BOOL wasCancelled = FALSE;
    if (WriteRegValueElevated(value, &wasCancelled)) {
        UpdateTrayIcon(FALSE);
    } else if (wasCancelled) {
        ShowNotification(L"DLSS Indicator", L"Administrator approval was canceled; no changes made.", NIIF_WARNING);
    } else {
        ShowNotification(L"DLSS Indicator", L"Could not write the registry value.", NIIF_ERROR);
    }
}

static void ToggleBinaryValue() {
    UpdateTrayIcon(FALSE);
    ApplyValue(g_valueExists && g_value == 0 ? 1024 : 0);
}

static void SetAutoStartFromMenu(BOOL enabled) {
    if (!SetAutoStartEnabled(enabled)) {
        ShowNotification(L"DLSS Indicator",
                         enabled ? L"Could not enable startup with Windows."
                                 : L"Could not disable startup with Windows.",
                         NIIF_ERROR);
    }
}

static void CopyToClipboard(LPCWSTR text) {
    if (!OpenClipboard(g_window)) return;
    EmptyClipboard();
    SIZE_T bytes = (lstrlenW(text) + 1) * sizeof(WCHAR);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* data = GlobalLock(memory);
        if (data) {
            memcpy(data, text, bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

static void OpenRegedit() {
    CopyToClipboard(REGEDIT_TARGET_PATH);

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGEDIT_APPLET_PATH, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"LastKey", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(REGEDIT_TARGET_PATH),
                       (lstrlenW(REGEDIT_TARGET_PATH) + 1) * sizeof(WCHAR));
        RegFlushKey(hKey);
        RegCloseKey(hKey);
    }

    HINSTANCE result = ShellExecuteW(g_window, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ShowNotification(L"DLSS Indicator", L"Could not open Registry Editor.", NIIF_ERROR);
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreated) {
        UpdateTrayIcon(TRUE);
        return 0;
    }

    switch (message) {
        case WM_MEASUREITEM:
            if (reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlType == ODT_MENU) {
                MeasureTrayMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->CtlType == ODT_MENU) {
                DrawTrayMenuItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
                return TRUE;
            }
            break;
        case WM_TRAY_ICON:
            if (LOWORD(lParam) == WM_CONTEXTMENU) ShowTrayMenu();
            else if (LOWORD(lParam) == NIN_SELECT) ToggleBinaryValue();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_MODE_OFF: ApplyValue(0); return 0;
                case ID_MODE_GREEN: ApplyValue(1); return 0;
                case ID_MODE_TEXT: ApplyValue(1024); return 0;
                case ID_REFRESH: UpdateTrayIcon(FALSE); return 0;
                case ID_OPEN_REGEDIT: OpenRegedit(); return 0;
                case ID_EXIT: DestroyWindow(window); return 0;
                case ID_ENABLE_AUTOSTART: SetAutoStartFromMenu(TRUE); return 0;
                case ID_DISABLE_AUTOSTART: SetAutoStartFromMenu(FALSE); return 0;
            }
            break;
        case WM_DESTROY: {
            NOTIFYICONDATAW data = {};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &data);
            if (g_trayIcon) {
                DestroyIcon(g_trayIcon);
                g_trayIcon = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    if (wcscmp(commandLine, L"--write-registry 0") == 0) return WriteRegValue(0) ? 0 : 1;
    if (wcscmp(commandLine, L"--write-registry 1") == 0) return WriteRegValue(1) ? 0 : 1;
    if (wcscmp(commandLine, L"--write-registry 1024") == 0) return WriteRegValue(1024) ? 0 : 1;

    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\DlssIndicatorTray");
    if (!instanceMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(instanceMutex);
        return 0;
    }

    const WCHAR className[] = L"DlssIndicatorTrayWindow";
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass)) {
        CloseHandle(instanceMutex);
        return 1;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_window = CreateWindowExW(0, className, L"DLSS Indicator Tray", 0,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window || !UpdateTrayIcon(TRUE)) {
        MessageBoxW(nullptr, L"Could not create the notification area icon.", L"DLSS Indicator Tray",
                    MB_OK | MB_ICONERROR);
        if (g_window) DestroyWindow(g_window);
        UnregisterClassW(className, instance);
        CloseHandle(instanceMutex);
        return 1;
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnregisterClassW(className, instance);
    CloseHandle(instanceMutex);
    return static_cast<int>(message.wParam);
}
