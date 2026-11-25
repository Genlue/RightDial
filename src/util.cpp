#include "stdafx.h"
#include <tlhelp32.h>

std::wstring Utf8ToUtf16(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Utf16ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

D2D1_COLOR_F ParseColor(const std::wstring& hex, float alphaMul) {
    float r = 0.15f, g = 0.15f, b = 0.15f, a = 1.0f;
    size_t i = (!hex.empty() && hex[0] == L'#') ? 1 : 0;
    auto take2 = [&](float& out) -> bool {
        if (i + 1 >= hex.size()) return false;
        int h = HexVal(hex[i]), l = HexVal(hex[i + 1]);
        if (h < 0 || l < 0) return false;
        out = (h * 16 + l) / 255.0f;
        i += 2;
        return true;
    };
    take2(r) && take2(g) && take2(b);
    if (i < hex.size()) { float aa; if (take2(aa)) a = aa; }
    return D2D1::ColorF(r, g, b, a * alphaMul);
}

static wchar_t HexDigit(int v) { return L"0123456789abcdef"[v & 15]; }

std::wstring ColorToHex(const D2D1_COLOR_F& c) {
    std::wstring s = L"#";
    int r = (int)std::lround(c.r * 255), g = (int)std::lround(c.g * 255), b = (int)std::lround(c.b * 255);
    s += HexDigit(r >> 4); s += HexDigit(r);
    s += HexDigit(g >> 4); s += HexDigit(g);
    s += HexDigit(b >> 4); s += HexDigit(b);
    return s;
}

std::wstring ColorToHexA(const D2D1_COLOR_F& c) {
    std::wstring s = ColorToHex(c);
    int a = (int)std::lround(c.a * 255);
    s += HexDigit(a >> 4); s += HexDigit(a);
    return s;
}

std::wstring ExePathW() {
    static std::wstring p;
    if (p.empty()) {
        wchar_t buf[MAX_PATH + 2] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        p = buf;
    }
    return p;
}

std::wstring ExeDirW() {
    static std::wstring d;
    if (d.empty()) {
        d = ExePathW();
        size_t s = d.find_last_of(L'\\');
        if (s != std::wstring::npos) d.resize(s);
    }
    return d;
}

bool PathFileExistsW_(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = false;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < 64 * 1024 * 1024) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t cb) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(h, data, (DWORD)cb, &wrote, nullptr) && wrote == cb;
    CloseHandle(h);
    return ok;
}

POINT GetCursorPosSafe() {
    POINT p{};
    GetCursorPos(&p);
    return p;
}

// ---------- Windows 10 1809+ immersive dark mode ----------
// uxtheme private ordinals, resolved dynamically so older systems no-op.

static FARPROC UxOrdinal(int ord) {
    HMODULE m = GetModuleHandleW(L"uxtheme.dll");
    return m ? GetProcAddress(m, MAKEINTRESOURCEA(ord)) : nullptr;
}

// SetPreferredAppMode + policy refresh: without RefreshImmersiveColorPolicyState
// the dark flag never reaches themed controls (buttons/checkboxes/combos keep
// painting light-theme BLACK text over our dark brushes). Call once per
// process before creating windows. mode: 1=AllowDark 2=ForceDark 3=ForceLight
void UxSetAppMode(int mode) {
    typedef int(WINAPI* FnMode)(int);
    typedef void(WINAPI* FnRefresh)(void);
    static FnMode fn = (FnMode)UxOrdinal(135);
    static FnRefresh refresh = (FnRefresh)UxOrdinal(104);
    if (fn) fn(mode);
    if (refresh) refresh();
}

// per-window dark mode for common controls; harmless on systems without it
bool UxAllowDarkForWindow(HWND h, bool dark) {
    typedef BOOL(WINAPI* Fn)(HWND, BOOL);
    static Fn fn = (Fn)UxOrdinal(133);
    if (!fn || !h) return false;
    return fn(h, dark) != FALSE;
}

// re-evaluate theme after a system color change (menus etc.)
void UxRefreshTheme() {
    typedef void(WINAPI* FnRefresh)(void);
    typedef void(WINAPI* FnFlush)(void);
    if (FnRefresh f = (FnRefresh)UxOrdinal(104)) f();
    if (FnFlush f = (FnFlush)UxOrdinal(136)) f();
}

// immersive dark titlebar / window frame (attribute 20, 19 on older builds)
void DwmDarkTitlebar(HWND h, bool dark) {
    BOOL v = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &v, sizeof(v))))
        DwmSetWindowAttribute(h, 19, &v, sizeof(v));
}

HMONITOR MonitorFromPointSafe(POINT pt) {
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

UINT DpiForPoint(POINT pt) {
    HMONITOR hm = MonitorFromPointSafe(pt);
    UINT dpi = 96;
    if (HMODULE sh = GetModuleHandleW(L"Shcore.dll")) {
        typedef HRESULT(WINAPI * Fn)(HMONITOR, int, UINT*);
        auto fn = (Fn)GetProcAddress(sh, "GetDpiForMonitor");
        if (fn && SUCCEEDED(fn(hm, 0 /*MDT_EFFECTIVE_DPI*/, &dpi))) return dpi;
    }
    return dpi;
}

float ScaleForPoint(POINT pt) { return DpiForPoint(pt) / 96.0f; }

RECT WorkAreaForPoint(POINT pt) {
    MONITORINFO mi{ sizeof(mi) };
    HMONITOR hm = MonitorFromPointSafe(pt);
    if (hm && GetMonitorInfoW(hm, &mi)) return mi.rcWork;
    RECT r{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
    return r;
}

// Remote-control software present in this session? Such a session is streaming
// the desktop through a capture API that honours WDA_EXCLUDEFROMCAPTURE, so a
// window carrying that affinity is simply missing from the remote picture.
// Substring match on the image name; the list only has to cover the products
// people actually drive RightDial with. Cached: the wheel asks on every show
// but the process list moves slowly.
static bool ScanForRemoteTool() {
    static const wchar_t* kTools[] = {
        L"uuremote",    // 网易UU远程 (client / agent / service share this stem)
        L"todesk", L"sunlogin", L"anydesk", L"teamviewer", L"rustdesk",
        L"parsec", L"winvnc", L"vncserver", L"tvnserver", L"radmin",
        L"truelink", L"awesun", L"splashtop", L"srservice", L"sragent",
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    wchar_t name[MAX_PATH];
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            size_t i = 0;
            for (; i < MAX_PATH - 1 && pe.szExeFile[i]; i++) name[i] = (wchar_t)towlower(pe.szExeFile[i]);
            name[i] = 0;
            for (const wchar_t* t : kTools) {
                if (wcsstr(name, t)) { found = true; break; }
            }
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool RemoteControlRunning() {
    static ULONGLONG last = 0;
    static bool cached = false;
    ULONGLONG now = GetTickCount64();
    if (!last || now - last > 3000) {   // 3 s is plenty: this only gates capture affinity
        last = now;
        cached = ScanForRemoteTool();
    }
    return cached;
}
