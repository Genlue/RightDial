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
// The low-level mouse hook must return almost immediately (it blocks the OS
// raw-input thread system-wide), so all real work is deferred to the message
// loop through these two.
constexpr UINT WM_APP_INJECT_CLICK = WM_APP + 5;  // synthesize the swallowed click
constexpr UINT WM_APP_SHOWWHEEL    = WM_APP + 6;  // open the wheel at lParam pos, wParam=1 if remote

// Stamped into dwExtraInfo of every INPUT we synthesize. The low-level hooks
// have to ignore *our own* injections (the re-emitted click would feed straight
// back into the gesture), but they must not ignore injections coming from other
// software: remote-control tools (UU远程 / ToDesk / RDP …) drive the pointer
// exclusively through SendInput, so their events arrive flagged
// LLMHF_INJECTED / LLKHF_INJECTED. Filtering on the flag alone therefore kills
// the wheel over a remote session; filtering on this tag keeps it working.
constexpr ULONG_PTR kInjectTag = 0x52444731;   // 'RDG1'

void RequestExecSlot(const Slot& s);   // thread-safe-ish: queues to main thread
