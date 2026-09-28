#pragma once
#include "stdafx.h"

enum class SlotType { Hotkey = 0, File = 1, Url = 2 };

struct Slot {
    SlotType     type = SlotType::Hotkey;
    std::wstring name;       // display label
    std::wstring keys;       // hotkey chord, e.g. L"Ctrl+Shift+V"
    std::wstring path;       // file / folder target
    std::wstring url;        // url target
    std::wstring iconPath;   // image/ico/svg path, or L"builtin:xxx"; empty = auto

    bool Empty() const {
        if (type == SlotType::Hotkey) return keys.empty();
        if (type == SlotType::File)   return path.empty();
        return url.empty();
    }
    std::wstring TargetText() const {
        switch (type) {
        case SlotType::Hotkey: return keys;
        case SlotType::File:   return path;
        default:               return url;
        }
    }
};

struct Page {
    std::wstring      name;
    std::vector<Slot> slots;
};

struct Appearance {
    float        diameter     = 300.0f;  // logical px @96dpi
    int          sectorCount  = 8;
    float        rotationDeg  = 0.0f;    // 0..360 rotation angle offset (degrees)
    float        iconSize     = 46.0f;
    float        sectorGap    = 2.0f;    // degrees between sectors
    int          bgMode       = 0;       // 0 solid, 1 acrylic(try), 2 liquid glass
    int          colorMode    = 0;       // 0 dark, 1 light, 2 auto(screen behind wheel), 3 follow system
    // dark palette
    std::wstring bgColor      = L"#1e2430";
    float        bgOpacity    = 0.86f;
    std::wstring hoverColor   = L"#3d6fb4";
    bool         hoverAccent  = false;    // hover color follows the system accent (overrides hoverColor/hoverColorLight)
    float        hoverOpacity = 0.92f;
    std::wstring borderColor  = L"#4a5568";
    float        borderWidth  = 1.5f;
    std::wstring textColor    = L"#f0f4fa";
    // light palette (used when the resolved theme is light)
    std::wstring bgColorLight      = L"#eef1f5";
    std::wstring hoverColorLight   = L"#cdd9ea";
    std::wstring borderColorLight  = L"#a9b4c4";
    std::wstring textColorLight    = L"#1b2029";
    std::wstring labelFont    = L"Microsoft YaHei UI";
    float        labelSize    = 13.0f;
    bool         showLabels   = true;
    int          centerMode   = 0;       // 0 page num, 1 text, 2 none
    std::wstring centerText;
    bool         animation    = true;
    float        animMs       = 130.0f;

    // liquid glass theme (bgMode 2), all sizes logical px @96dpi
    float        glassBlur    = 5.0f;    // gaussian blur before refraction
    float        glassHeight  = 22.0f;   // refraction band depth H
    float        glassAmount  = 44.0f;   // refraction strength A
    bool         glassChroma    = true;  // chromatic aberration (7-tap dispersion)
    bool         glassVibrancy  = true;  // saturation x1.5
    bool         glassHighlight = true;  // rim highlight (45 deg, plus blend)
    // live backdrop capture cost knobs (bgMode 1/2); lower = cheaper
    float        backdropScale = 0.0f;   // capture resolution fraction, 0 = auto (half-res above 640px)
    int          backdropFps   = 60;     // capture rate cap in Hz (10..60)
};

struct Behavior {
    int   triggerButton = 1;   // 1 right, 2 middle
    float thresholdPx   = 12.0f;
    int   wheelSensitivity = 100; // page flip sensitivity percentage (20..300, default 100)
    bool  enabled       = true;
    bool  showTray      = true;
    bool  autostart     = false;
    bool  excludeFullscreen = true;   // disable gesture while a borderless-fullscreen window is foreground
    std::vector<std::wstring> exclusions;  // process names, lowercase, e.g. "game.exe"
};

struct AppConfig {
    int               version = 1;
    Appearance        app;
    Behavior          beh;
    std::vector<Page> pages;
};

bool         IsPortableMode();
std::wstring GetConfigDir();
std::wstring GetConfigPath();
AppConfig    DefaultConfig();
bool         LoadConfig(AppConfig& out);          // false: file missing or invalid
bool         SaveConfig(const AppConfig& cfg);

bool IsAutostartEnabled();
void SetAutostart(bool on);

// 0 dark / 1 light, from the system "Apps use light theme" setting (1 on failure)
int  SystemAppTheme();

// Windows accent color as "#rrggbb" (registry first, DwmGetColorizationColor
// fallback); empty string when neither is available
std::wstring SystemAccentHex();

// icon source string for a slot when iconPath is empty (auto)
std::wstring SlotIconSource(const Slot& s);
