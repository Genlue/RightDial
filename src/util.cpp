#include "stdafx.h"

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
