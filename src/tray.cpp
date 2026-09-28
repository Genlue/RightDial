#include "stdafx.h"
#include "tray.h"
#include "app.h"
#include "settings.h"

static NOTIFYICONDATAW g_nid = {};
static bool g_added = false;

void TrayInit(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wcscpy_s(g_nid.szTip, L"RightDial");
}

void TrayUpdate(bool show) {
    if (show && !g_added) {
        g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    } else if (!show && g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_added = false;
    }
    if (show && g_added) {
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

void TrayRemove() {
    if (g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_added = false;
    }
}

static void ShowHelp(HWND h) {
    MessageBoxW(h,
        L"使用方法：\n\n"
        L"  按住鼠标右键并拖动 —— 轮盘出现，移到目标后松开执行\n"
        L"  轮盘打开时滚动滚轮 —— 切换页面\n"
        L"  轮盘打开时按 Esc —— 取消\n"
        L"  普通右键单击（不拖动）—— 不受影响，正常弹出菜单\n\n"
        L"隐藏托盘图标后：再次运行 RightDial 即可打开设置面板。\n\n"
        L"提示：若目标窗口以管理员权限运行，请同样以管理员身份运行本程序。\n"
        L"Win 组合键请在设置的槽位编辑中勾选 Win 后录入；Win+L 会直接锁屏（系统保留组合，无法注入按键）。",
        L"RightDial 使用说明", MB_OK | MB_ICONINFORMATION);
}

void TrayHandle(WPARAM wp, LPARAM lp) {
    UNREFERENCED_PARAMETER(wp);
    UINT msg = (UINT)lp;
    if (msg == WM_LBUTTONUP) {
        OpenSettings();
        return;
    }
    if (msg != WM_RBUTTONUP && msg != WM_CONTEXTMENU) return;

    POINT p = GetCursorPosSafe();
    SetForegroundWindow(g_nid.hWnd);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 2001, L"打开设置…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_cfg.beh.enabled ? MF_CHECKED : 0), 2002, L"启用轮盘");
    AppendMenuW(m, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), 2003, L"开机自启");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2004, L"使用说明");
    AppendMenuW(m, MF_STRING, 2005, L"退出");
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, g_nid.hWnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_nid.hWnd, WM_NULL, 0, 0);

    switch (cmd) {
    case 2001: OpenSettings(); break;
    case 2002:
        g_cfg.beh.enabled = !g_cfg.beh.enabled;
        SaveConfig(g_cfg);
        break;
    case 2003: {
        bool on = !IsAutostartEnabled();
        SetAutostart(on);
        g_cfg.beh.autostart = on;
        SaveConfig(g_cfg);
        break;
    }
    case 2004: ShowHelp(g_nid.hWnd); break;
    case 2005: PostMessageW(g_nid.hWnd, WM_CLOSE, 0, 0); break;
    }
}
