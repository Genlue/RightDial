#include "stdafx.h"
#include "wheel.h"
#include "app.h"
#include "render.h"
#include "hook.h"

static const wchar_t* WC_WHEEL = L"RightDialWheel";

static HWND         s_hwnd = nullptr;
static WheelSurface s_surf;
static POINT        s_pos{ 0, 0 };    // window top-left (screen)
static float        s_size = 0;       // window size (square), physical px
static float        s_scale = 1.0f;
static float        s_pad = 0;
static int          s_page = 0;
static int          s_pageCount = 1;
static int          s_hover = -1;
static float        s_animT = 1.0f;
static bool         s_dark = true;   // resolved color theme for the current wheel session
static std::vector<Slot> s_slots;

static std::vector<uint8_t> s_backdrop;  // blurred snapshot for acrylic mode
static int s_bw = 0, s_bh = 0;
static float s_dispW = 0, s_dispH = 0;
static float s_bx = 0, s_by = 0;
static uint64_t s_backdropSeq = 0;

static HDC     s_capMem = nullptr;
static HBITMAP s_capDib = nullptr;
static void*   s_capBits = nullptr;
static int     s_capAllocW = 0, s_capAllocH = 0;

static LARGE_INTEGER s_lastCap{};   // QPC of the last backdrop capture
static LARGE_INTEGER s_qpf{};

// area-average downsample for arbitrary ratios (backdropScale below 1)
static void DownsampleArea(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    if (!src || !dst || sw < 1 || sh < 1 || dw < 1 || dh < 1) return;
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((int64_t)y * sh / dh), y1 = (int)((int64_t)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;
        uint8_t* d = dst + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((int64_t)x * sw / dw), x1 = (int)((int64_t)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;
            int b = 0, g = 0, r = 0, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                const uint8_t* row = src + (size_t)sy * sw * 4;
                for (int sx = x0; sx < x1; sx++, n++) {
                    b += row[sx * 4]; g += row[sx * 4 + 1]; r += row[sx * 4 + 2];
                }
            }
            if (!n) n = 1;
            d[x * 4]     = (uint8_t)(b / n);
            d[x * 4 + 1] = (uint8_t)(g / n);
            d[x * 4 + 2] = (uint8_t)(r / n);
            d[x * 4 + 3] = 255;
        }
    }
}

// true when the configured capture interval has elapsed (backdropFps cap)
static bool CaptureDue() {
    if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (s_lastCap.QuadPart) {
        double ms = (double)(now.QuadPart - s_lastCap.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
        double interval = 1000.0 / std::clamp(g_cfg.app.backdropFps, 10, 60);
        if (ms < interval) return false;
    }
    s_lastCap = now;
    return true;
}

static void FreeCaptureResources() {
    if (s_capMem) {
        if (s_capDib) SelectObject(s_capMem, nullptr);
        DeleteDC(s_capMem);
        s_capMem = nullptr;
    }
    if (s_capDib) {
        DeleteObject(s_capDib);
        s_capDib = nullptr;
    }
    s_capBits = nullptr;
    s_capAllocW = s_capAllocH = 0;
}

// capture the screen area behind the wheel; acrylic gets a half-res blurred
// snapshot, liquid glass keeps full detail for its refraction rim (half-res
// only for very large wheels). WDA_EXCLUDEFROMCAPTURE ensures the wheel
// window itself is excluded from the capture.
static void CaptureBackdrop() {
    int bg = g_cfg.app.bgMode;
    if ((bg != 1 && bg != 2) || s_size < 16) {
        s_backdrop.clear(); s_bw = s_bh = 0;
        return;
    }

    RECT want{ s_pos.x, s_pos.y,
               s_pos.x + (int)s_size, s_pos.y + (int)s_size };
    RECT vs{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
             GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
             GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    RECT cap;
    if (!IntersectRect(&cap, &want, &vs)) return;
    int cw = cap.right - cap.left, ch = cap.bottom - cap.top;
    if (cw < 8 || ch < 8) return;

    if (!s_capMem || cw != s_capAllocW || ch != s_capAllocH) {
        FreeCaptureResources();
        HDC sdc = GetDC(nullptr);
        s_capMem = CreateCompatibleDC(sdc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cw;
        bi.bmiHeader.biHeight = -ch;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        s_capDib = CreateDIBSection(s_capMem, &bi, DIB_RGB_COLORS, &s_capBits, nullptr, 0);
        if (s_capDib && s_capBits) {
            SelectObject(s_capMem, s_capDib);
            s_capAllocW = cw;
            s_capAllocH = ch;
        } else {
            FreeCaptureResources();
            ReleaseDC(nullptr, sdc);
            return;
        }
        ReleaseDC(nullptr, sdc);
    }

    HDC sdc = GetDC(nullptr);
    BitBlt(s_capMem, 0, 0, cw, ch, sdc, cap.left, cap.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, sdc);

    if (!s_capBits) return;

    if (bg == 1) {
        // 2x downsample for ultra-smooth acrylic blur and minimal CPU usage (< 0.3ms)
        int dw = std::max(4, cw / 2);
        int dh = std::max(4, ch / 2);
        if (s_backdrop.size() != (size_t)dw * dh * 4) {
            s_backdrop.assign((size_t)dw * dh * 4, 0);
        }
        Downsample2x((const uint8_t*)s_capBits, cw, ch, s_backdrop.data(), dw, dh);
        BlurBGRA(s_backdrop.data(), dw, dh, 6);
        s_bw = dw; s_bh = dh;
    } else {
        // liquid glass: sharp snapshot; the glass pipeline applies its own blur.
        // backdropScale trades capture resolution for speed (0 = auto: half-res
        // only for very large wheels, the classic behavior)
        float sc = g_cfg.app.backdropScale;
        int dw = cw, dh = ch;
        if (sc > 0.0f) {
            dw = std::max(4, (int)std::lround(cw * sc));
            dh = std::max(4, (int)std::lround(ch * sc));
            if (dw > 1280) dw = 1280;
            if (dh > 1280) dh = 1280;
        } else if (cw > 640 || ch > 640) {
            dw = std::max(4, cw / 2);
            dh = std::max(4, ch / 2);
        }
        if (s_backdrop.size() != (size_t)dw * dh * 4)
            s_backdrop.assign((size_t)dw * dh * 4, 0);
        if (dw == cw && dh == ch) {
            memcpy(s_backdrop.data(), s_capBits, (size_t)cw * ch * 4);
        } else {
            DownsampleArea((const uint8_t*)s_capBits, cw, ch, s_backdrop.data(), dw, dh);
        }
        s_bw = dw; s_bh = dh;
    }
    s_dispW = (float)cw; s_dispH = (float)ch;
    s_bx = (float)(cap.left - s_pos.x);
    s_by = (float)(cap.top - s_pos.y);
    s_backdropSeq++;
}

static void RenderNow() {
    if (!s_hwnd) return;
    WheelFrameState st{};
    st.app = &g_cfg.app;
    st.slots = &s_slots;
    st.pageCount = s_pageCount;
    st.pageIdx = s_page;
    st.hover = s_hover;
    st.animT = s_animT;
    st.scale = s_scale;
    st.dark = s_dark;
    if (!s_backdrop.empty()) {
        st.backdrop = s_backdrop.data();
        st.backdropW = s_bw;
        st.backdropH = s_bh;
        st.backdropDispW = s_dispW;
        st.backdropDispH = s_dispH;
        st.backdropX = s_bx;
        st.backdropY = s_by;
        st.backdropSeq = s_backdropSeq;
    }
    s_surf.Render(st, s_pad);

    HDC sdc = GetDC(s_hwnd);
    POINT dst{ s_pos.x, s_pos.y };
    POINT src{ 0, 0 };
    SIZE sz{ (int)s_surf.W(), (int)s_surf.H() };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(s_hwnd, sdc, &dst, &sz, s_surf.MemDC(), &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(s_hwnd, sdc);
}

static void RebuildSlots() {
    s_pageCount = g_cfg.pages.empty() ? 1 : (int)g_cfg.pages.size();
    if (s_page < 0 || s_page >= s_pageCount) s_page = 0;
    s_slots = g_cfg.pages[s_page].slots;
}

// average luminance (0..1) of the screen region, circle-masked, via a 32x32
// StretchBlt probe; called once per wheel show for the "auto" color mode
static float ScreenRegionLuminance(int x, int y, int w, int h) {
    if (w < 8 || h < 8) return 0.0f;
    const int sw = 32, sh = 32;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sw;
    bi.bmiHeader.biHeight = -sh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    float sum = 0.0f;
    int n = 0;
    HDC sdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(sdc);
    if (HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)) {
        HGDIOBJ old = SelectObject(mem, dib);
        if (StretchBlt(mem, 0, 0, sw, sh, sdc, x, y, w, h, SRCCOPY | CAPTUREBLT) && bits) {
            const uint8_t* px = (const uint8_t*)bits;
            for (int iy = 0; iy < sh; iy++) {
                for (int ix = 0; ix < sw; ix++) {
                    float dx = (ix - sw / 2 + 0.5f) / (sw / 2.0f);
                    float dy = (iy - sh / 2 + 0.5f) / (sh / 2.0f);
                    if (dx * dx + dy * dy > 1.0f) continue;   // wheel is a circle
                    size_t i = ((size_t)iy * sw + ix) * 4;
                    sum += 0.2126f * px[i + 2] + 0.7152f * px[i + 1] + 0.0722f * px[i];
                    n++;
                }
            }
        }
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, sdc);
    return n > 0 ? (sum / n) / 255.0f : 0.0f;
}

// resolve the palette for this wheel session from the configured color mode
static bool ResolveWheelDark(const POINT& center, int sizePx) {
    switch (g_cfg.app.colorMode) {
    case 1:  return false;      // light
    case 2: {                   // auto: brightness of the screen behind the wheel
        float luma = ScreenRegionLuminance(center.x - sizePx / 2, center.y - sizePx / 2,
                                           sizePx, sizePx);
        return luma < 0.5f;     // failure reads as 0 -> keeps the dark look
    }
    case 3:  return SystemAppTheme() == 0;   // follow system
    default: return true;       // dark (uses the classic configurable palette)
    }
}

LRESULT CALLBACK WheelWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_TIMER:
        if (wp == 1) {
            bool needRender = false;
            if (s_animT < 1.0f && g_cfg.app.animation) {
                float step = 16.0f / std::max(16.0f, g_cfg.app.animMs);
                s_animT = std::min(1.0f, s_animT + step);
                needRender = true;
            }
            if (g_cfg.app.bgMode != 0) {
                // acrylic / liquid glass: live backdrop, re-captured at the
                // configured rate cap (backdropFps); rendering happens on
                // capture ticks and while the open animation runs
                if (CaptureDue()) {
                    CaptureBackdrop();
                    needRender = true;
                }
            } else {
                if (s_animT >= 1.0f) KillTimer(h, 1);
            }
            if (needRender) {
                RenderNow();
            }
        }
        return 0;
    case WM_DESTROY:
        FreeCaptureResources();
        return 0;
    case WM_PAINT:
        // layered window paints itself via UpdateLayeredWindow
        ValidateRect(h, nullptr);
        return 0;
    default:
        return DefWindowProcW(h, m, wp, lp);
    }
}

void RegisterWheelClass() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WheelWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = WC_WHEEL;
    RegisterClassExW(&wc);
}

bool WheelIsOpen() {
    return s_hwnd && IsWindowVisible(s_hwnd);
}

void WheelShowAt(POINT center) {
    RebuildSlots();

    s_scale = ScaleForPoint(center);
    s_pad = 8.0f * s_scale;
    s_size = g_cfg.app.diameter * s_scale + s_pad * 2;

    if (!s_hwnd || !IsWindow(s_hwnd)) {
        s_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 WC_WHEEL, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_hInst, nullptr);
        if (!s_hwnd) return;
        SetWindowDisplayAffinity(s_hwnd, 0x00000011 /* WDA_EXCLUDEFROMCAPTURE */);
    } else {
        SetWindowDisplayAffinity(s_hwnd, 0x00000011 /* WDA_EXCLUDEFROMCAPTURE */);
    }

    int sz = (int)std::lround(s_size);
    int x = center.x - sz / 2, y = center.y - sz / 2;
    RECT wa = WorkAreaForPoint(center);
    if (x + sz > wa.right) x = wa.right - sz;
    if (y + sz > wa.bottom) y = wa.bottom - sz;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    s_pos = { x, y };

    s_hover = -1;
    s_animT = g_cfg.app.animation ? 0.0f : 1.0f;
    s_dark = ResolveWheelDark(POINT{ x + sz / 2, y + sz / 2 }, sz);

    if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
    QueryPerformanceCounter(&s_lastCap);
    CaptureBackdrop();
    s_surf.Resize(s_size, s_size);
    RenderNow();

    SetWindowPos(s_hwnd, HWND_TOPMOST, x, y, sz, sz, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (g_cfg.app.bgMode != 0 || s_animT < 1.0f) {
        SetTimer(s_hwnd, 1, 16, nullptr);
    }
    InstallKbdHook();
}

void WheelHide() {
    if (s_hwnd && IsWindow(s_hwnd)) {
        KillTimer(s_hwnd, 1);
        ShowWindow(s_hwnd, SW_HIDE);
    }
    RemoveKbdHook();
    ResetHookState();
    s_hover = -1;
}

void WheelUpdateHover(POINT screenPt) {
    if (!WheelIsOpen()) return;
    float lx = (float)(screenPt.x - s_pos.x);
    float ly = (float)(screenPt.y - s_pos.y);
    WheelLayout L = ComputeLayout(g_cfg.app, s_scale, s_pad, s_size, s_size);
    int idx = WheelHitTest(L, lx, ly);
    if (idx < 0 || idx >= g_cfg.app.sectorCount || idx >= (int)s_slots.size() || s_slots[idx].Empty())
        idx = -1;
    if (idx != s_hover) {
        s_hover = idx;
        RenderNow();
    }
}

void WheelCommitHover() {
    int idx = s_hover;
    WheelHide();
    if (idx >= 0 && idx < (int)s_slots.size())
        RequestExecSlot(s_slots[idx]);
}

void WheelCancel() { WheelHide(); }

void WheelSwitchPage(int dir) {
    if (!WheelIsOpen() || s_pageCount <= 0) return;
    s_page = ((s_page + dir) % s_pageCount + s_pageCount) % s_pageCount;
    RebuildSlots();
    s_hover = -1;
    s_animT = 1.0f;
    RenderNow();
}

