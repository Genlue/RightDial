#include "stdafx.h"
#include "hook.h"
#include "app.h"
#include "wheel.h"

static HHOOK s_mouse = nullptr;
static HHOOK s_kbd = nullptr;

enum { G_IDLE = 0, G_PEND = 1 };
static int   s_state = G_IDLE;
static POINT s_downPt{ 0, 0 };
static int   s_wheelAcc = 0;

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static bool FgProcessExcluded() {
    if (g_cfg.beh.exclusions.empty()) return false;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t name[MAX_PATH] = L"";
    DWORD n = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(h, 0, name, &n);
    CloseHandle(h);
    if (!ok) return false;
    std::wstring base = ToLower(PathFindFileNameW(name));
    for (const std::wstring& e : g_cfg.beh.exclusions)
        if (_wcsicmp(e.c_str(), base.c_str()) == 0) return true;
    return false;
}

// borderless fullscreen foreground window (covers its whole monitor, no caption)
static bool IsFgFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg) || !IsWindowVisible(fg)) return false;
    if (fg == GetDesktopWindow() || fg == GetShellWindow()) return false;

    // Desktop, taskbar, and shell windows must NEVER be treated as fullscreen game exclusions!
    wchar_t cls[64] = L"";
    if (GetClassNameW(fg, cls, 64) > 0) {
        if (_wcsicmp(cls, L"Progman") == 0 ||
            _wcsicmp(cls, L"WorkerW") == 0 ||
            _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
            _wcsicmp(cls, L"#32769") == 0 ||
            _wcsicmp(cls, L"Windows.UI.Core.CoreWindow") == 0 ||
            _wcsicmp(cls, L"ApplicationFrameWindow") == 0) {
            return false;
        }
    }

    LONG style = GetWindowLongW(fg, GWL_STYLE);
    if (style & WS_MAXIMIZE) return false;   // Normal maximized windows are NEVER fullscreen games!
    if (style & WS_CAPTION) return false;    // Normal windows with caption

    RECT wr;
    if (!GetWindowRect(fg, &wr)) return false;
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

static bool FgExcluded() {
    if (FgProcessExcluded()) return true;
    return g_cfg.beh.excludeFullscreen && IsFgFullscreen();
}

static void InjectTriggerClick() {
    bool mid = (g_cfg.beh.triggerButton == 2);
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = mid ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_RIGHTDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = mid ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_RIGHTUP;
    SendInput(2, in, sizeof(INPUT));
}

static LRESULT Pass(int code, WPARAM wp, LPARAM lp) { return CallNextHookEx(nullptr, code, wp, lp); }

static LRESULT CALLBACK MouseProcLL(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return Pass(code, wp, lp);
    MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lp;
    UINT msg = (UINT)wp;

    if (m->flags & LLMHF_INJECTED) return Pass(code, wp, lp);

    if (WheelIsOpen()) {
        switch (msg) {
        case WM_MOUSEMOVE:
            WheelUpdateHover(m->pt);
            return Pass(code, wp, lp);  // never eat moves: eating them freezes the cursor
        case WM_MOUSEWHEEL: {
            short delta = HIWORD(m->mouseData);
            if (delta) {
                float sens = (float)g_cfg.beh.wheelSensitivity;
                if (sens < 10.0f) sens = 10.0f;
                if (sens > 500.0f) sens = 500.0f;
                float threshold = 120.0f * (100.0f / sens);

                if ((delta > 0 && s_wheelAcc < 0) || (delta < 0 && s_wheelAcc > 0)) {
                    s_wheelAcc = 0;
                }

                s_wheelAcc += delta;

                int steps = (int)(s_wheelAcc / threshold);
                if (steps != 0) {
                    s_wheelAcc -= (int)(steps * threshold);
                    WheelSwitchPage(steps);
                }
            }
            return 1;
        }
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_LBUTTONUP:
            WheelCommitHover();
            return 1;
        default:
            return 1;  // modal: swallow button-downs and other events
        }
    }

    if (!g_cfg.beh.enabled) {
        s_state = G_IDLE;
        return Pass(code, wp, lp);
    }

    UINT down = (g_cfg.beh.triggerButton == 2) ? WM_MBUTTONDOWN : WM_RBUTTONDOWN;
    UINT up   = (g_cfg.beh.triggerButton == 2) ? WM_MBUTTONUP   : WM_RBUTTONUP;

    if (msg == down) {
        if (FgExcluded()) {
            s_state = G_IDLE;
            return Pass(code, wp, lp);
        }
        s_downPt = m->pt;
        s_state = G_PEND;
        return 1;  // hold the button; re-inject as a click if it turns out to be one
    }
    if (s_state == G_PEND) {
        if (msg == up) {
            s_state = G_IDLE;
            InjectTriggerClick();  // plain right-click: normal context menu
            return 1;
        }
        if (msg == WM_MOUSEMOVE) {
            float thr = g_cfg.beh.thresholdPx * ScaleForPoint(s_downPt);
            float dx = (float)(m->pt.x - s_downPt.x), dy = (float)(m->pt.y - s_downPt.y);
            if (dx * dx + dy * dy >= thr * thr) {
                s_state = G_IDLE;
                s_wheelAcc = 0;
                WheelShowAt(s_downPt);
                return 1;
            }
            return Pass(code, wp, lp);
        }
        if (msg == WM_LBUTTONDOWN) {
            s_state = G_IDLE;
            return Pass(code, wp, lp);
        }
    }
    return Pass(code, wp, lp);
}

static LRESULT CALLBACK KbdProcLL(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    if (WheelIsOpen()) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        if (k->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, wp, lp);
        if ((wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && k->vkCode == VK_ESCAPE) {
            WheelCancel();
            return 1;
        }
        return 1;  // modal while the wheel is open
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void InstallMouseHook() {
    if (!s_mouse)
        s_mouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProcLL, g_hInst, 0);
}
void RemoveMouseHook() {
    if (s_mouse) { UnhookWindowsHookEx(s_mouse); s_mouse = nullptr; }
    s_state = G_IDLE;
    s_wheelAcc = 0;
}
void EnsureMouseHook() {
    s_state = G_IDLE;
    s_wheelAcc = 0;
    if (s_mouse) {
        UnhookWindowsHookEx(s_mouse);
        s_mouse = nullptr;
    }
    InstallMouseHook();
}
void ResetHookState() {
    s_state = G_IDLE;
    s_wheelAcc = 0;
}
void InstallKbdHook() {
    if (!s_kbd)
        s_kbd = SetWindowsHookExW(WH_KEYBOARD_LL, KbdProcLL, g_hInst, 0);
}
void RemoveKbdHook() {
    if (s_kbd) { UnhookWindowsHookEx(s_kbd); s_kbd = nullptr; }
}

