// test_hooklat.cpp — measures the real cost of RightDial's right-click
// "swallow-down / re-inject-on-up" pattern inside a WH_MOUSE_LL hook.
//
// The question: RightDial swallows WM_RBUTTONDOWN and, when the matching UP
// arrives, calls SendInput() *from inside the hook* to synthesize the click.
// A low-level hook blocks the OS raw-input thread system-wide, so anything
// expensive in that callback delays every mouse event.
//
// This probe reproduces the exact pattern with synthetic events (marked via
// dwExtraInfo so the "physical press" path can be exercised without hardware)
// and logs QPC timestamps around every step, comparing:
//
//   mode 0: SendInput() called from inside the hook        (current design)
//   mode 1: SendInput() deferred via PostMessage           (proposed fix)
//
// Build:  see build-hooklat.cmd

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// dwExtraInfo tags: lets the probe tell apart the simulated "hardware" press
// from the click the hook re-injects.
static const ULONG_PTR MARK_TRIGGER  = 0x52544431;  // 'RTD1'
static const ULONG_PTR MARK_REINJECT = 0x52544432;  // 'RTD2'

static const UINT WM_DEFER = WM_APP + 10;

static HHOOK         g_hook = nullptr;
static FILE*         g_log = nullptr;
static LARGE_INTEGER g_qpf{};
static LARGE_INTEGER g_t0{};
static HWND          g_wnd = nullptr;
static int           g_mode = 0;
static LONG          g_depth = 0;
static LONG          g_cycles = 12;
static LONG          g_movesSeen = 0;

static double Now() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
}

static const char* MsgName(UINT m) {
    switch (m) {
    case WM_MOUSEMOVE:   return "MOVE";
    case WM_RBUTTONDOWN: return "RDOWN";
    case WM_RBUTTONUP:   return "RUP";
    case WM_LBUTTONDOWN: return "LDOWN";
    case WM_LBUTTONUP:   return "LUP";
    case WM_MBUTTONDOWN: return "MDOWN";
    case WM_MBUTTONUP:   return "MUP";
    case WM_MOUSEWHEEL:  return "WHEEL";
    }
    return "OTHER";
}

static void Line(const char* tag, UINT msg, DWORD flags, ULONG_PTR extra, double dur, LONG depth) {
    if (!g_log) return;
    fprintf(g_log, "%.3f\t%-9s\t%-6s\tinj=%d\tmark=%llX\tdur=%.3f\tdepth=%ld\n",
            Now(), tag, MsgName(msg), (flags & LLMHF_INJECTED) ? 1 : 0,
            (unsigned long long)extra, dur, depth);
    fflush(g_log);
}

static void ReinjectClick() {
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    in[0].mi.dwExtraInfo = MARK_REINJECT;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    in[1].mi.dwExtraInfo = MARK_REINJECT;
    SendInput(2, in, sizeof(INPUT));
}

static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lp;
    UINT msg = (UINT)wp;

    LONG depth = InterlockedIncrement(&g_depth);
    double t0 = Now();

    const char* tag;
    LRESULT ret;

    if (m->dwExtraInfo == MARK_TRIGGER) {
        // simulated physical press: swallow the down, re-inject on the up
        if (msg == WM_RBUTTONDOWN) {
            tag = "trigDOWN"; ret = 1;
        } else if (msg == WM_RBUTTONUP) {
            tag = "trigUP";
            if (g_mode == 0) ReinjectClick();                  // inside the hook
            else             PostMessageW(g_wnd, WM_DEFER, 0, 0); // after the hook
            ret = 1;
        } else {
            tag = "trigOTHER"; ret = CallNextHookEx(nullptr, code, wp, lp);
        }
    } else if (m->dwExtraInfo == MARK_REINJECT) {
        tag = "reinj";
        ret = 1;   // swallowed on purpose: keeps the probe free of UI side effects
    } else {
        if (msg == WM_MOUSEMOVE) {
            InterlockedIncrement(&g_movesSeen);
            InterlockedDecrement(&g_depth);
            return CallNextHookEx(nullptr, code, wp, lp);   // skip logging noise
        }
        tag = "real"; ret = CallNextHookEx(nullptr, code, wp, lp);
    }

    Line(tag, msg, m->flags, m->dwExtraInfo, Now() - t0, depth);
    InterlockedDecrement(&g_depth);
    return ret;
}

// Cost of the work RightDial does on every right-button-down (exclusion check).
static void BenchFgExcluded() {
    double total = 0, worst = 0;
    const int N = 300;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);

    LARGE_INTEGER a, b;
    for (int i = 0; i < N; i++) {
        QueryPerformanceCounter(&a);
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t name[MAX_PATH] = L"";
            DWORD n = MAX_PATH;
            QueryFullProcessImageNameW(h, 0, name, &n);
            CloseHandle(h);
        }
        QueryPerformanceCounter(&b);
        double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        total += ms;
        if (ms > worst) worst = ms;
    }
    if (g_log) {
        fprintf(g_log, "# BENCH OpenProcess+QueryFullProcessImageNameW on fg pid=%lu : "
                       "avg=%.3fms max=%.3fms over %d calls\n",
                (unsigned long)pid, total / N, worst, N);
        fflush(g_log);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_DEFER:
        ReinjectClick();
        if (g_log) { fprintf(g_log, "%.3f\t%-9s\t-\n", Now(), "deferred"); fflush(g_log); }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static DWORD WINAPI Driver(LPVOID) {
    Sleep(800);
    for (LONG i = 0; i < g_cycles; i++) {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        in.mi.dwExtraInfo = MARK_TRIGGER;
        SendInput(1, &in, sizeof(INPUT));

        Sleep(50);   // press duration

        in.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        in.mi.dwExtraInfo = MARK_TRIGGER;
        SendInput(1, &in, sizeof(INPUT));

        Sleep(1200);
    }
    Sleep(400);
    if (g_wnd) PostMessageW(g_wnd, WM_CLOSE, 0, 0);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-mode") == 0 && i + 1 < argc) g_mode = atoi(argv[++i]);
        else if (strcmp(argv[i], "-cycles") == 0 && i + 1 < argc) g_cycles = atol(argv[++i]);
    }

    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_t0);

    char logpath[MAX_PATH];
    wsprintfA(logpath, "bin\\hooklat_mode%d.log", g_mode);
    g_log = fopen(logpath, "w");
    if (!g_log) { printf("cannot open log\n"); return 1; }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"HookLatProbe";
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(0, L"HookLatProbe", L"hooklat", WS_OVERLAPPEDWINDOW,
                            0, 0, 200, 100, nullptr, nullptr, wc.hInstance, nullptr);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, wc.hInstance, 0);
    if (!g_hook) {
        fprintf(g_log, "# SetWindowsHookEx failed err=%lu\n", GetLastError());
        fflush(g_log);
        fclose(g_log);
        return 1;
    }

    if (g_log) {
        fprintf(g_log, "# mode=%d (0=SendInput inside hook, 1=deferred PostMessage) cycles=%ld\n",
                g_mode, g_cycles);
        fflush(g_log);
    }

    BenchFgExcluded();

    HANDLE drv = CreateThread(nullptr, 0, Driver, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (drv) { WaitForSingleObject(drv, 5000); CloseHandle(drv); }
    UnhookWindowsHookEx(g_hook);
    if (g_log) {
        fprintf(g_log, "# done moves_seen=%ld\n", g_movesSeen);
        fclose(g_log);
    }
    printf("mode %d done, log: %s\n", g_mode, logpath);
    return 0;
}
