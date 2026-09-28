#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <dwmapi.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

constexpr float kPiF = 3.14159265358979323846f;

inline float Deg2Rad(float d) { return d * kPiF / 180.0f; }
inline D2D1_POINT_2F MakePt(float x, float y) { return D2D1::Point2F(x, y); }

// UTF-8 <-> UTF-16
std::wstring Utf8ToUtf16(const std::string& s);
std::string  Utf16ToUtf8(const std::wstring& s);

// "#RRGGBB" or "#RRGGBBAA" -> D2D color; alphaMul multiplies parsed alpha (1 if absent)
D2D1_COLOR_F ParseColor(const std::wstring& hex, float alphaMul = 1.0f);
std::wstring ColorToHex(const D2D1_COLOR_F& c);   // "#RRGGBB"
std::wstring ColorToHexA(const D2D1_COLOR_F& c);  // "#RRGGBBAA"

std::wstring ExePathW();
std::wstring ExeDirW();
bool  PathFileExistsW_(const std::wstring& p);
bool  ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out);
bool  WriteFileBytes(const std::wstring& path, const void* data, size_t cb);

POINT  GetCursorPosSafe();
HMONITOR MonitorFromPointSafe(POINT pt);
UINT   DpiForPoint(POINT pt);          // 96 on failure
float  ScaleForPoint(POINT pt);        // dpi/96
RECT   WorkAreaForPoint(POINT pt);
