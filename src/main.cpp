#include "stdafx.h"
#include "app.h"
#include "config.h"
#include "hook.h"
#include "wheel.h"
#include "tray.h"
#include "settings.h"
#include "actions.h"

AppConfig g_cfg;
HWND      g_mainHwnd = nullptr;
HINSTANCE g_hInst = nullptr;

static const wchar_t* MAIN_CLASS = L"RightDialMain";
static HANDLE g_mutex = nullptr;

void RequestExecSlot(const Slot& s) {
    Slot* p = new Slot(s);
    if (!PostMessageW(g_mainHwnd, WM_APP_EXEC, 0, (LPARAM)p))
        delete p;
}

static bool CmdHas(const wchar_t* name) {
    int n = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < n; i++)
        if (_wcsicmp(argv[i], name) == 0) found = true;
    LocalFree(argv);
    return found;
}

static LRESULT CALLBACK MainWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_APP_SHOWSETTINGS:
        OpenSettings();
        EnsureMouseHook();
        return 0;
    case WM_APP_TESTWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        WheelShowAt(pt);
        return 0;
    }
    case WM_APP_INJECT_CLICK:
        // emitted here, not in the hook: SendInput from inside a low-level hook
        // stalls it for the hook timeout (~300ms) and the whole system's mouse
        // input with it
        InjectTriggerClickNow();
        return 0;
    case WM_APP_SHOWWHEEL:
        if (HookWheelLaunchReady()) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            WheelShowAt(pt);
        }
        return 0;
    case WM_APP_EXEC:
        if (lp) {
            Slot* s = (Slot*)lp;
            ExecuteSlot(*s);
            delete s;
        }
        return 0;
    case WM_APP_TRAY:
        TrayHandle(wp, lp);
        return 0;
    case WM_CLOSE:
        SettingsFlushAndClose();
        WheelCancel();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, m, wp, lp);
    }
}

static void RegisterMainClass() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    RegisterClassExW(&wc);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    UNREFERENCED_PARAMETER(nCmdShow);
    g_hInst = hInst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\RightDial.SingleInstance");
    bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;

    RegisterMainClass();

    if (alreadyRunning) {
        HWND prev = FindWindowW(MAIN_CLASS, nullptr);
        if (prev) {
            if (CmdHas(L"/exit")) {
                PostMessageW(prev, WM_CLOSE, 0, 0);
            } else if (CmdHas(L"/testwheel")) {
                RECT rc = WorkAreaForPoint(GetCursorPosSafe());
                PostMessageW(prev, WM_APP_TESTWHEEL, 0,
                             MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2));
            } else {
                PostMessageW(prev, WM_APP_SHOWSETTINGS, 0, 0);
            }
        }
        CloseHandle(g_mutex);
        CoUninitialize();
        return 0;
    }

    if (!LoadConfig(g_cfg)) {
        g_cfg = DefaultConfig();
        SaveConfig(g_cfg);
    }
    g_cfg.beh.autostart = IsAutostartEnabled();

    g_mainHwnd = CreateWindowExW(0, MAIN_CLASS, L"RightDial", WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 200, 100,
                                 nullptr, nullptr, hInst, nullptr);

    InstallMouseHook();
    RegisterWheelClass();
    TrayInit(g_mainHwnd);
    TrayUpdate(g_cfg.beh.showTray);

    if (CmdHas(L"/settings") || CmdHas(L"-settings"))
        PostMessageW(g_mainHwnd, WM_APP_SHOWSETTINGS, 0, 0);
    if (CmdHas(L"/testwheel")) {
        RECT rc = WorkAreaForPoint(GetCursorPosSafe());
        PostMessageW(g_mainHwnd, WM_APP_TESTWHEEL, 0,
                     MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2));
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveMouseHook();
    RemoveKbdHook();
    TrayRemove();
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = nullptr; }
    CoUninitialize();
    return 0;
}
