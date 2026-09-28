// standalone probe: opens the real settings window and drives every appearance
// trackbar through the WM_HSCROLL dispatch to verify dragging mutates its
// config value. Regression test for the range check that dropped
// IDC_P2_CUTRES / IDC_P2_CUTFPS, leaving those two sliders drag-dead.
// Runs without a message pump on purpose: SaveSoon's 250ms WM_TIMER save never
// dispatches, so the user's config.json on disk is never touched.
#include "stdafx.h"
#include "app.h"
#include "config.h"
#include "settings.h"
#include <commctrl.h>
#include <cstdio>

AppConfig g_cfg;
HWND      g_mainHwnd = nullptr;
HINSTANCE g_hInst = nullptr;

// wheel.cpp symbols referenced by hook.cpp; wheel.cpp is not linked here
void RegisterWheelClass() {}
void WheelShowAt(POINT) {}
bool WheelIsOpen() { return false; }
void WheelUpdateHover(POINT) {}
void WheelCommitHover() {}
void WheelCancel() {}
void WheelSwitchPage(int) {}

static HWND g_hwnd = nullptr, g_panel2 = nullptr;

// drag a trackbar the way the real control does: update pos, then notify parent
static bool DragSlider(int id, int pos) {
    HWND track = GetDlgItem(g_panel2, id);
    if (!track) { printf("FAIL: trackbar %d not found\n", id); return false; }
    SendMessageW(track, TBM_SETPOS, TRUE, pos);
    SendMessageW(g_hwnd, WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, pos), (LPARAM)track);
    return true;
}

static bool Expect(const char* what, double got, double want) {
    bool ok = (got == want);
    printf("%-34s %-10g %s (want %g)\n", what, got, ok ? "ok" : "FAIL", want);
    return ok;
}

static bool FindWindowAndPanel() {
    g_hwnd = FindWindowW(L"RightDialSettings", nullptr);
    if (!g_hwnd) { printf("FAIL: settings window not found\n"); return false; }
    HWND c = nullptr;
    while ((c = FindWindowExW(g_hwnd, c, L"Static", L"")) != nullptr) {
        if (GetDlgItem(c, IDC_P2_CUTRES)) { g_panel2 = c; break; }
    }
    if (!g_panel2) { printf("FAIL: appearance panel not found\n"); return false; }
    return true;
}

int main() {
    g_cfg = DefaultConfig();
    g_cfg.app.bgMode = 2;                    // glass page controls enabled
    OpenSettings();                          // synchronous window creation
    if (!FindWindowAndPanel()) return 1;

    int fails = 0;
    // the two sliders that were dead: capture clarity (25..100 -> 0.25..1.0)
    if (!DragSlider(IDC_P2_CUTRES, 50)) return 1;
    fails += !Expect("backdropScale @ slider 50", g_cfg.app.backdropScale, 0.5f);
    if (!DragSlider(IDC_P2_CUTRES, 25)) return 1;
    fails += !Expect("backdropScale @ slider 25", g_cfg.app.backdropScale, 0.25f);
    if (!DragSlider(IDC_P2_CUTRES, 100)) return 1;
    fails += !Expect("backdropScale @ slider 100", g_cfg.app.backdropScale, 1.0f);

    if (!DragSlider(IDC_P2_CUTFPS, 30)) return 1;
    fails += !Expect("backdropFps @ slider 30", g_cfg.app.backdropFps, 30);
    if (!DragSlider(IDC_P2_CUTFPS, 10)) return 1;
    fails += !Expect("backdropFps @ slider 10", g_cfg.app.backdropFps, 10);

    // spot-check sliders that already worked, to prove the dispatch path
    if (!DragSlider(IDC_P2_GAP, 8)) return 1;
    fails += !Expect("sectorGap @ slider 8", g_cfg.app.sectorGap, 8);
    if (!DragSlider(IDC_P2_SECTORS, 10)) return 1;
    fails += !Expect("sectorCount @ slider 10", g_cfg.app.sectorCount, 10);
    if (!DragSlider(IDC_P2_BORDW, 5)) return 1;
    fails += !Expect("borderWidth @ slider 5", g_cfg.app.borderWidth, 2.5f);

    // destroy without WM_CLOSE / WM_TIMER: SaveConfig must never run here
    DestroyWindow(g_hwnd);
    if (fails) { printf("test_slider: %d FAIL\n", fails); return 1; }
    printf("test_slider: all pass\n");
    return 0;
}
