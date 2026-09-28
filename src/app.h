#pragma once
#include "stdafx.h"
#include "config.h"

// app-wide globals
extern AppConfig g_cfg;
extern HWND      g_mainHwnd;
extern HINSTANCE g_hInst;

// main-window private messages
constexpr UINT WM_APP_SHOWSETTINGS = WM_APP + 1;  // 2nd instance -> show settings
constexpr UINT WM_APP_TRAY         = WM_APP + 2;  // tray icon callback
constexpr UINT WM_APP_EXEC         = WM_APP + 3;  // lParam carries heap Slot*
constexpr UINT WM_APP_TESTWHEEL    = WM_APP + 4;  // /testwheel: show wheel at lParam pos

void RequestExecSlot(const Slot& s);   // thread-safe-ish: queues to main thread
