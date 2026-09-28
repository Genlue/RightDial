#pragma once
#include "stdafx.h"
#include "config.h"

struct WheelLayout {
    float cx = 0, cy = 0;   // window-space center
    float r  = 0;           // outer radius (physical px)
    float r0 = 0;           // dead-zone radius
    float iconR = 0;        // icon ring radius
    float labelR = 0;       // label ring radius
    int   sectors = 8;
    float gapDeg = 2;
    float rotationDeg = 0;
};

WheelLayout     ComputeLayout(const Appearance& a, float scale, float pad, float winW, float winH);
int             WheelHitTest(const WheelLayout& L, float x, float y); // -2 center deadzone, >= 0 sector index
D2D1_POINT_2F   SectorIconCenter(const WheelLayout& L, int idx);
D2D1_POINT_2F   SectorLabelCenter(const WheelLayout& L, int idx);

// separable box blur on opaque BGRA pixels (for the acrylic backdrop)
void BlurBGRA(uint8_t* px, int w, int h, int radius);
void Downsample2x(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh);

struct WheelFrameState {
    const Appearance*     app = nullptr;
    const std::vector<Slot>* slots = nullptr;
    int   pageCount = 1;
    int   pageIdx   = 0;
    int   hover     = -1;   // slot index or -1
    float animT     = 1.0f; // 0..1
    float scale     = 1.0f; // dpi scale
    bool  dark      = true; // resolved color theme: true = dark palette, false = light palette
    // acrylic backdrop: pre-blurred BGRA snapshot of what is behind the wheel;
    // position of its top-left corner in window space
    const uint8_t* backdrop = nullptr;
    int   backdropW = 0, backdropH = 0;
    float backdropDispW = 0, backdropDispH = 0;
    float backdropX = 0, backdropY = 0;
    uint64_t backdropSeq = 0;
};

// Renders the wheel into a WIC-backed bitmap, exposes a DIB/memDC for
// UpdateLayeredWindow (popup) or AlphaBlend (settings preview).
class WheelSurface {
public:
    WheelSurface() = default;
    ~WheelSurface();
    void Resize(float wPx, float hPx);
    void Render(const WheelFrameState& st, float pad);
    void Release();
    float W() const { return m_w; }
    float H() const { return m_h; }
    HDC    MemDC() const { return m_memDC; }
    HBITMAP Dib()  const { return m_dib; }

private:
    void EnsureBackdrop(const WheelFrameState& st);
    void EnsureGlass(const WheelFrameState& st, const WheelLayout& L);
    ComPtr<IWICBitmap>        m_wicBmp;
    ComPtr<ID2D1RenderTarget> m_rt;
    ComPtr<ID2D1Layer>        m_layer;
    ComPtr<ID2D1Bitmap>       m_backdrop;   // blurred acrylic snapshot
    const uint8_t*            m_backdropKey = nullptr;
    int m_backdropW = 0, m_backdropH = 0;
    uint64_t                  m_backdropSeq = 0;
    ComPtr<ID2D1Bitmap>       m_glass;      // liquid glass texture (bgMode 2)
    std::vector<uint8_t>      m_glassBits;
    uint64_t                  m_glassKey = 0;
    bool                      m_glassValid = false;
    int m_glassW = 0, m_glassH = 0;
    HBITMAP m_dib = nullptr;
    HDC     m_memDC = nullptr;
    void*   m_bits = nullptr;
    float   m_w = 0, m_h = 0;
};

class IconCache {
public:
    static IconCache& I();
    // D2D bitmaps are per render target; safe to call every frame.
    // dark picks the builtin glyph tint (light strokes on dark, dark on light)
    ID2D1Bitmap* GetD2D(ID2D1RenderTarget* rt, const std::wstring& source, float sizePx, bool dark = true);
    // ARGB premultiplied DIB for GDI AlphaBlend; owned by the cache
    HBITMAP GetHbitmap(const std::wstring& source, int sizePx);
    void Clear();

private:
    bool GetWic(const std::wstring& source, int sizePx, bool dark, IWICBitmap** out);
    std::unordered_map<std::wstring, HBITMAP> m_hb;
    std::unordered_map<std::wstring, ID2D1Bitmap*> m_d2d;
    std::unordered_map<std::wstring, ComPtr<IWICBitmap>> m_wic;
};

IDWriteTextFormat* GetTextFormat(const std::wstring& font, float sizePx, IDWriteFactory* dw);
