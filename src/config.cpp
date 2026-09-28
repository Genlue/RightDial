#include "stdafx.h"
#include "config.h"

#include <windows.ui.viewmanagement.h>
#include <activation.h>          // MIDL C header: IActivationFactory (global ns)
#include <winstring.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::wstring JStr(const json& j, const char* key, const std::wstring& def = L"") {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return def;
    return Utf8ToUtf16(it->get<std::string>());
}
static float JNum(const json& j, const char* key, float def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    return (float)it->get<double>();
}
static int JInt(const json& j, const char* key, int def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return def;
    return it->get<int>();
}
static bool JBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return def;
    return it->get<bool>();
}

static bool IsPortableModeCalc() {
    std::wstring dir = ExeDirW();
    if (PathFileExistsW_(dir + L"\\portable.ini")) return true;
    if (PathFileExistsW_(dir + L"\\config.json"))  return true;
    return false;
}

bool IsPortableMode() {
    static int v = -1;
    if (v < 0) v = IsPortableModeCalc() ? 1 : 0;
    return v == 1;
}

std::wstring GetConfigDir() {
    if (IsPortableMode()) return ExeDirW();
    wchar_t* appdata = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        base = appdata;
        CoTaskMemFree(appdata);
    } else {
        base = ExeDirW();
    }
    return base + L"\\RightDial";
}

std::wstring GetConfigPath() { return GetConfigDir() + L"\\config.json"; }

static json SlotToJson(const Slot& s) {
    json j;
    j["type"] = (int)s.type;
    j["name"] = Utf16ToUtf8(s.name);
    j["keys"] = Utf16ToUtf8(s.keys);
    j["path"] = Utf16ToUtf8(s.path);
    j["url"]  = Utf16ToUtf8(s.url);
    j["icon"] = Utf16ToUtf8(s.iconPath);
    return j;
}

static Slot SlotFromJson(const json& j) {
    Slot s;
    s.type     = (SlotType)JInt(j, "type", 0);
    s.name     = JStr(j, "name");
    s.keys     = JStr(j, "keys");
    s.path     = JStr(j, "path");
    s.url      = JStr(j, "url");
    s.iconPath = JStr(j, "icon");
    return s;
}

static json PageToJson(const Page& p) {
    json j;
    j["name"]  = Utf16ToUtf8(p.name);
    json arr = json::array();
    for (const Slot& s : p.slots) arr.push_back(SlotToJson(s));
    j["slots"] = arr;
    return j;
}

static Page PageFromJson(const json& j) {
    Page p;
    p.name = JStr(j, "name");
    auto it = j.find("slots");
    if (it != j.end() && it->is_array())
        for (const json& e : *it) p.slots.push_back(SlotFromJson(e));
    return p;
}

static json CfgToJson(const AppConfig& c) {
    json j;
    j["version"] = 1;
    json ap, bh;
    ap["diameter"]     = c.app.diameter;
    ap["sectorCount"]  = c.app.sectorCount;
    ap["rotationDeg"]  = c.app.rotationDeg;
    ap["iconSize"]     = c.app.iconSize;
    ap["sectorGap"]    = c.app.sectorGap;
    ap["bgMode"]       = c.app.bgMode;
    ap["colorMode"]    = c.app.colorMode;
    ap["bgColor"]      = Utf16ToUtf8(c.app.bgColor);
    ap["bgOpacity"]    = c.app.bgOpacity;
    ap["hoverColor"]   = Utf16ToUtf8(c.app.hoverColor);
    ap["hoverAccent"]  = c.app.hoverAccent;
    ap["hoverOpacity"] = c.app.hoverOpacity;
    ap["borderColor"]  = Utf16ToUtf8(c.app.borderColor);
    ap["borderWidth"]  = c.app.borderWidth;
    ap["textColor"]    = Utf16ToUtf8(c.app.textColor);
    ap["bgColorLight"]     = Utf16ToUtf8(c.app.bgColorLight);
    ap["hoverColorLight"]  = Utf16ToUtf8(c.app.hoverColorLight);
    ap["borderColorLight"] = Utf16ToUtf8(c.app.borderColorLight);
    ap["textColorLight"]   = Utf16ToUtf8(c.app.textColorLight);
    ap["labelFont"]    = Utf16ToUtf8(c.app.labelFont);
    ap["labelSize"]    = c.app.labelSize;
    ap["showLabels"]   = c.app.showLabels;
    ap["centerMode"]   = c.app.centerMode;
    ap["centerText"]   = Utf16ToUtf8(c.app.centerText);
    ap["animation"]    = c.app.animation;
    ap["animMs"]       = c.app.animMs;
    ap["glassBlur"]    = c.app.glassBlur;
    ap["glassHeight"]  = c.app.glassHeight;
    ap["glassAmount"]  = c.app.glassAmount;
    ap["glassChroma"]    = c.app.glassChroma;
    ap["glassVibrancy"]  = c.app.glassVibrancy;
    ap["glassHighlight"] = c.app.glassHighlight;
    ap["backdropScale"] = c.app.backdropScale;
    ap["backdropFps"]   = c.app.backdropFps;
    bh["triggerButton"] = c.beh.triggerButton;
    bh["thresholdPx"]   = c.beh.thresholdPx;
    bh["wheelSensitivity"] = c.beh.wheelSensitivity;
    bh["enabled"]       = c.beh.enabled;
    bh["showTray"]      = c.beh.showTray;
    bh["autostart"]     = IsAutostartEnabled();   // registry is the source of truth
    bh["excludeFullscreen"] = c.beh.excludeFullscreen;
    json ex = json::array();
    for (const std::wstring& e : c.beh.exclusions) ex.push_back(Utf16ToUtf8(e));
    bh["exclusions"] = ex;
    j["appearance"] = ap;
    j["behavior"]   = bh;
    json pages = json::array();
    for (const Page& p : c.pages) pages.push_back(PageToJson(p));
    j["pages"] = pages;
    return j;
}

static void CfgFromJson(const json& j, AppConfig& c) {
    auto ap = j.find("appearance");
    if (ap != j.end() && ap->is_object()) {
        c.app.diameter     = JNum(*ap, "diameter", c.app.diameter);
        c.app.sectorCount  = JInt(*ap, "sectorCount", c.app.sectorCount);
        c.app.rotationDeg  = JNum(*ap, "rotationDeg", c.app.rotationDeg);
        c.app.iconSize     = JNum(*ap, "iconSize", c.app.iconSize);
        c.app.sectorGap    = JNum(*ap, "sectorGap", c.app.sectorGap);
        c.app.bgMode       = JInt(*ap, "bgMode", c.app.bgMode);
        c.app.colorMode    = JInt(*ap, "colorMode", c.app.colorMode);
        c.app.bgColor      = JStr(*ap, "bgColor", c.app.bgColor);
        c.app.bgOpacity    = JNum(*ap, "bgOpacity", c.app.bgOpacity);
        c.app.hoverColor   = JStr(*ap, "hoverColor", c.app.hoverColor);
        c.app.hoverAccent  = JBool(*ap, "hoverAccent", c.app.hoverAccent);
        c.app.hoverOpacity = JNum(*ap, "hoverOpacity", c.app.hoverOpacity);
        c.app.borderColor  = JStr(*ap, "borderColor", c.app.borderColor);
        c.app.borderWidth  = JNum(*ap, "borderWidth", c.app.borderWidth);
        c.app.textColor    = JStr(*ap, "textColor", c.app.textColor);
        c.app.bgColorLight     = JStr(*ap, "bgColorLight", c.app.bgColorLight);
        c.app.hoverColorLight  = JStr(*ap, "hoverColorLight", c.app.hoverColorLight);
        c.app.borderColorLight = JStr(*ap, "borderColorLight", c.app.borderColorLight);
        c.app.textColorLight   = JStr(*ap, "textColorLight", c.app.textColorLight);
        c.app.labelFont    = JStr(*ap, "labelFont", c.app.labelFont);
        c.app.labelSize    = JNum(*ap, "labelSize", c.app.labelSize);
        c.app.showLabels   = JBool(*ap, "showLabels", c.app.showLabels);
        c.app.centerMode   = JInt(*ap, "centerMode", c.app.centerMode);
        c.app.centerText   = JStr(*ap, "centerText", c.app.centerText);
        c.app.animation    = JBool(*ap, "animation", c.app.animation);
        c.app.animMs       = JNum(*ap, "animMs", c.app.animMs);
        c.app.glassBlur    = JNum(*ap, "glassBlur", c.app.glassBlur);
        c.app.glassHeight  = JNum(*ap, "glassHeight", c.app.glassHeight);
        c.app.glassAmount  = JNum(*ap, "glassAmount", c.app.glassAmount);
        c.app.glassChroma    = JBool(*ap, "glassChroma", c.app.glassChroma);
        c.app.glassVibrancy  = JBool(*ap, "glassVibrancy", c.app.glassVibrancy);
        c.app.glassHighlight = JBool(*ap, "glassHighlight", c.app.glassHighlight);
        c.app.backdropScale = JNum(*ap, "backdropScale", c.app.backdropScale);
        c.app.backdropFps   = JInt(*ap, "backdropFps", c.app.backdropFps);
    }
    auto bh = j.find("behavior");
    if (bh != j.end() && bh->is_object()) {
        c.beh.triggerButton = JInt(*bh, "triggerButton", c.beh.triggerButton);
        c.beh.thresholdPx   = JNum(*bh, "thresholdPx", c.beh.thresholdPx);
        c.beh.wheelSensitivity = JInt(*bh, "wheelSensitivity", c.beh.wheelSensitivity);
        c.beh.enabled       = JBool(*bh, "enabled", c.beh.enabled);
        c.beh.showTray      = JBool(*bh, "showTray", c.beh.showTray);
        c.beh.excludeFullscreen = JBool(*bh, "excludeFullscreen", c.beh.excludeFullscreen);
        auto ex = bh->find("exclusions");
        if (ex != bh->end() && ex->is_array())
            for (const json& e : *ex)
                if (e.is_string()) c.beh.exclusions.push_back(Utf8ToUtf16(e.get<std::string>()));
    }
    auto pg = j.find("pages");
    if (pg != j.end() && pg->is_array())
        for (const json& e : *pg) c.pages.push_back(PageFromJson(e));
}

AppConfig DefaultConfig() {
    AppConfig c;
    c.pages.clear();

    std::wstring downloads, desktop;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) { downloads = p; CoTaskMemFree(p); }
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)))   { desktop  = p; CoTaskMemFree(p); }

    Page pg; pg.name = L"常用";
    Slot s1; s1.type = SlotType::Hotkey; s1.name = L"粘贴"; s1.keys = L"Ctrl+V"; s1.iconPath = L"builtin:paste"; pg.slots.push_back(s1);
    Slot s2; s2.type = SlotType::Hotkey; s2.name = L"复制"; s2.keys = L"Ctrl+C"; s2.iconPath = L"builtin:copy";  pg.slots.push_back(s2);
    Slot s3; s3.type = SlotType::Hotkey; s3.name = L"撤销"; s3.keys = L"Ctrl+Z"; s3.iconPath = L"builtin:undo";  pg.slots.push_back(s3);
    Slot s4; s4.type = SlotType::Hotkey; s4.name = L"保存"; s4.keys = L"Ctrl+S"; s4.iconPath = L"builtin:save";  pg.slots.push_back(s4);
    if (!downloads.empty()) { Slot s; s.type = SlotType::File; s.name = L"下载"; s.path = downloads; s.iconPath = L"builtin:folder"; pg.slots.push_back(s); }
    if (!desktop.empty())   { Slot s; s.type = SlotType::File; s.name = L"桌面"; s.path = desktop;   s.iconPath = L"builtin:folder"; pg.slots.push_back(s); }
    Slot s7; s7.type = SlotType::Url; s7.name = L"百度";   s7.url = L"https://www.baidu.com";  s7.iconPath = L"builtin:browser"; pg.slots.push_back(s7);
    Slot s8; s8.type = SlotType::Url; s8.name = L"GitHub"; s8.url = L"https://github.com";     s8.iconPath = L"builtin:browser"; pg.slots.push_back(s8);
    c.pages.push_back(pg);
    return c;
}

bool LoadConfig(AppConfig& out) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(GetConfigPath(), bytes)) return false;
    try {
        json j = json::parse(bytes.data(), bytes.data() + bytes.size());
        CfgFromJson(j, out);
    } catch (...) {
        return false;
    }
    out.beh.autostart = IsAutostartEnabled();
    if (out.beh.wheelSensitivity < 20 || out.beh.wheelSensitivity > 500) out.beh.wheelSensitivity = 100;
    if (out.app.bgMode < 0 || out.app.bgMode > 2) out.app.bgMode = 0;
    if (out.app.colorMode < 0 || out.app.colorMode > 3) out.app.colorMode = 0;
    out.app.glassBlur   = std::clamp(out.app.glassBlur, 0.0f, 32.0f);
    out.app.glassHeight = std::clamp(out.app.glassHeight, 0.0f, 96.0f);
    out.app.glassAmount = std::clamp(out.app.glassAmount, 0.0f, 200.0f);
    if (out.app.backdropScale < 0.1f || out.app.backdropScale > 1.0f) out.app.backdropScale = 0.0f;  // anything odd = auto
    out.app.backdropFps = std::clamp(out.app.backdropFps, 10, 60);
    if (out.app.sectorCount < 1)  out.app.sectorCount = 1;
    if (out.app.sectorCount > 36) out.app.sectorCount = 36;
    while (out.app.rotationDeg < 0.0f)   out.app.rotationDeg += 360.0f;
    while (out.app.rotationDeg >= 360.0f) out.app.rotationDeg -= 360.0f;
    for (auto& pg : out.pages) {
        if (pg.name == L"甯哥敤") pg.name = L"常用";
    }
    if (out.pages.empty()) out.pages.push_back(Page{ L"常用", {} });
    return true;
}

bool SaveConfig(const AppConfig& cfg) {
    std::string body = CfgToJson(cfg).dump(2);
    std::wstring dir = GetConfigDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    return WriteFileBytes(GetConfigPath(), body.data(), body.size());
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool IsAutostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = 0;
    bool ok = RegQueryValueExW(k, L"RightDial", nullptr, &type, nullptr, &cb) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(k);
    return ok;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring cmd = L"\"" + ExePathW() + L"\"";
        RegSetValueExW(k, L"RightDial", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"RightDial");
    }
    RegCloseKey(k);
}

// system apps theme: HKCU\...\Themes\Personalize\AppsUseLightTheme (0 = dark)
int SystemAppTheme() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return 1;
    DWORD v = 1, type = 0, cb = sizeof(v);
    if (RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, &type, (BYTE*)&v, &cb) != ERROR_SUCCESS
        || type != REG_DWORD) v = 1;
    RegCloseKey(k);
    return v ? 1 : 0;
}

// Windows accent color, live even under Win11's "automatic accent" mode:
//   1) WinRT UISettings.GetColorValue(Accent) — the authoritative current value
//      (the DWM registry value goes stale when the accent is auto-derived)
//   2) HKCU DWM AccentColor (0xAABBGGRR)
//   3) DwmGetColorizationColor (0xAARRGGBB — reversed channel order)
// Cached for a few seconds; the render loop calls this every frame.
static std::wstring SystemAccentHexOnce() {
    std::wstring hex;
    {
        // combase.dll via GetProcAddress keeps the import surface unchanged
        using RoGetActivationFactory_t = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
        using WindowsCreateString_t = HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING*);
        using WindowsDeleteString_t = HRESULT(WINAPI*)(HSTRING);
        static HMODULE combase = LoadLibraryW(L"combase.dll");
        static auto pRoGet = combase ? (RoGetActivationFactory_t)GetProcAddress(combase, "RoGetActivationFactory") : nullptr;
        static auto pCreate = combase ? (WindowsCreateString_t)GetProcAddress(combase, "WindowsCreateString") : nullptr;
        static auto pDelete = combase ? (WindowsDeleteString_t)GetProcAddress(combase, "WindowsDeleteString") : nullptr;
        if (pRoGet && pCreate && pDelete) {
            // UISettings is a runtime class: activate an instance, then QI
            // IUISettings3 — the activation FACTORY itself does not expose it
            Microsoft::WRL::ComPtr<IActivationFactory> factory;
            HSTRING hcls = nullptr;
            if (SUCCEEDED(pCreate(RuntimeClass_Windows_UI_ViewManagement_UISettings,
                                  (UINT32)wcslen(RuntimeClass_Windows_UI_ViewManagement_UISettings), &hcls))) {
                pRoGet(hcls, __uuidof(IActivationFactory),
                       (void**)factory.ReleaseAndGetAddressOf());
                pDelete(hcls);
            }
            if (factory) {
                Microsoft::WRL::ComPtr<IInspectable> obj;
                if (SUCCEEDED(factory->ActivateInstance(&obj)) && obj) {
                    Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings3> settings;
                    if (SUCCEEDED(obj.As(&settings)) && settings) {
                        ABI::Windows::UI::Color c{};
                        if (SUCCEEDED(settings->GetColorValue(ABI::Windows::UI::ViewManagement::UIColorType_Accent, &c))) {
                            wchar_t buf[8];
                            swprintf_s(buf, L"#%02x%02x%02x", c.R, c.G, c.B);
                            hex = buf;
                        }
                    }
                }
            }
        }
    }
    if (hex.empty()) {
        DWORD v = 0, type = 0, cb = sizeof(v);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
                         L"AccentColor", RRF_RT_REG_DWORD, &type, &v, &cb) == ERROR_SUCCESS
            && type == REG_DWORD) {
            COLORREF c = v & 0x00FFFFFF;
            wchar_t buf[8];
            swprintf_s(buf, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
            hex = buf;
        }
    }
    if (hex.empty()) {
        DWORD col = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&col, &opaque))) {
            COLORREF c = RGB((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
            wchar_t buf[8];
            swprintf_s(buf, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
            hex = buf;
        }
    }
    return hex;
}

std::wstring SystemAccentHex() {
    static std::wstring cached;
    static ULONGLONG stamp = 0;
    ULONGLONG now = GetTickCount64();
    if (stamp == 0 || now - stamp > 5000) {
        cached = SystemAccentHexOnce();
        stamp = now;
    }
    return cached;
}

std::wstring SlotIconSource(const Slot& s) {
    if (!s.iconPath.empty()) return s.iconPath;
    switch (s.type) {
    case SlotType::Hotkey: {
        std::wstring k = s.keys;
        for (auto& c : k) c = (wchar_t)towlower(c);
        if (k == L"ctrl+v") return L"builtin:paste";
        if (k == L"ctrl+c") return L"builtin:copy";
        if (k == L"ctrl+x") return L"builtin:cut";
        if (k == L"ctrl+z") return L"builtin:undo";
        if (k == L"ctrl+s") return L"builtin:save";
        if (k.find(L"prtsc") != std::wstring::npos || k.find(L"printscreen") != std::wstring::npos) return L"builtin:screenshot";
        if (k == L"win+shift+s") return L"builtin:screenshot";
        if (k == L"win+up") return L"builtin:maximize";
        if (k == L"win+down") return L"builtin:minimize";
        if (k == L"win+d") return L"builtin:minimize";
        if (k == L"win+l") return L"builtin:lock";
        if (k == L"win+e") return L"builtin:folder";
        if (k == L"win+s") return L"builtin:search";
        if (k == L"f11") return L"builtin:fullscreen";
        return L"builtin:keys";
    }
    case SlotType::Url:    return L"builtin:browser";
    default:               return L"auto:" + s.path;
    }
}
