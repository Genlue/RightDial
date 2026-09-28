#include "stdafx.h"
#include "render.h"
#include "glass.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// ---------- shared factories ----------

ComPtr<ID2D1Factory> GdiD2DFactory() {
    static ComPtr<ID2D1Factory> f;
    if (!f) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&f));
    return f;
}
static ComPtr<IWICImagingFactory> GdiWicFactory() {
    static ComPtr<IWICImagingFactory> f;
    if (!f) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}
ComPtr<IDWriteFactory> GdiDWriteFactory() {
    static ComPtr<IDWriteFactory> f;
    if (!f) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)f.GetAddressOf());
    return f;
}

IDWriteTextFormat* GetTextFormat(const std::wstring& font, float sizePx, IDWriteFactory* dw) {
    static std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> cache;
    wchar_t key[512];
    swprintf_s(key, L"%s|%.0f", font.c_str(), sizePx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> tf;
    std::wstring fam = font.empty() ? L"Microsoft YaHei UI" : font;
    HRESULT hr = dw->CreateTextFormat(fam.c_str(), nullptr,
                                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, sizePx, L"zh-cn", &tf);
    if (FAILED(hr)) {
        hr = dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                  sizePx, L"zh-cn", &tf);
    }
    if (FAILED(hr)) return nullptr;
    if (tf) {
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    cache[key] = tf;
    return tf.Get();
}

// ---------- builtin svg glyphs ----------

static const wchar_t* FindBuiltinSvg(const std::wstring& name) {
    static const struct { const wchar_t* n; const wchar_t* s; } k[] = {
        { L"keys", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<rect x='2' y='6' width='20' height='12' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                   L"<path d='M5.5 9.5h1M8.5 9.5h1M11.5 9.5h1M14.5 9.5h1M17.5 9.5h1M5.5 12.5h1M8.5 12.5h1M11.5 12.5h1M14.5 12.5h1M17.5 12.5h1M7.5 15.2h9' "
                   L"stroke='#e8edf5' stroke-width='1.5' stroke-linecap='round' fill='none'/></svg>" },
        { L"copy", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<rect x='8' y='8' width='13' height='13' rx='2' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                   L"<path d='M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
        { L"paste", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                    L"<rect x='5' y='4' width='14' height='17' rx='2' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                    L"<path d='M9 2h6a1 1 0 0 1 1 1v2a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1z' fill='none' stroke='#e8edf5' stroke-width='1.6'/>"
                    L"<path d='M8 10h8M8 14h8M8 18h5' stroke='#e8edf5' stroke-width='1.6' stroke-linecap='round'/></svg>" },
        { L"cut", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                  L"<circle cx='6' cy='6' r='3' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                  L"<circle cx='6' cy='18' r='3' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                  L"<path d='M20 4L8.5 15.5M20 20L8.5 8.5' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round'/></svg>" },
        { L"undo", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<path d='M3 7v6h6' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/>"
                   L"<path d='M3 13a9 9 0 1 1 2.6 6.4' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round'/></svg>" },
        { L"save", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<path d='M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                   L"<path d='M17 21v-8H7v8M7 3v5h8' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/></svg>" },
        { L"screenshot", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                         L"<path d='M4 8V4h4M16 4h4v4M20 16v4h-4M8 20H4v-4' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/>"
                         L"<circle cx='12' cy='12' r='3.2' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                         L"<path d='M12 7v2M12 15v2M7 12h2M15 12h2' stroke='#e8edf5' stroke-width='1.6' stroke-linecap='round'/></svg>" },
        { L"terminal", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                       L"<rect x='3' y='4' width='18' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                       L"<path d='M7 9l4 3-4 3M13 15h4' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
        { L"cmd", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                  L"<rect x='3' y='4' width='18' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                  L"<path d='M7 9l4 3-4 3M13 15h4' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
        { L"search", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                     L"<circle cx='11' cy='11' r='7' fill='none' stroke='#e8edf5' stroke-width='2'/>"
                     L"<path d='M21 21l-4.35-4.35' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round'/></svg>" },
        { L"browser", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                      L"<rect x='2' y='4' width='20' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                      L"<path d='M2 9h20M6 6.5h.01M9 6.5h.01M12 6.5h.01' stroke='#e8edf5' stroke-width='2' stroke-linecap='round'/></svg>" },
        { L"globe", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                    L"<circle cx='12' cy='12' r='9' fill='none' stroke='#e8edf5' stroke-width='1.6'/>"
                    L"<ellipse cx='12' cy='12' rx='4.2' ry='9' fill='none' stroke='#e8edf5' stroke-width='1.3'/>"
                    L"<path d='M3.5 12h17M4.8 7h14.4M4.8 17h14.4' stroke='#e8edf5' stroke-width='1.3' fill='none'/></svg>" },
        { L"folder", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                     L"<path d='M3 7a2 2 0 0 1 2-2h4.2l2 2H19a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z' "
                     L"fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/></svg>" },
        { L"file", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<path d='M6 3h8l5 5v13a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                   L"<path d='M14 3v5h5M9 13h6M9 17h4' fill='none' stroke='#e8edf5' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
        { L"settings", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                       L"<circle cx='12' cy='12' r='3.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                       L"<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/></svg>" },
        { L"gear", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<circle cx='12' cy='12' r='3.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                   L"<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/></svg>" },
        { L"trash", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                    L"<path d='M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/>"
                    L"<path d='M10 11v6M14 11v6' stroke='#e8edf5' stroke-width='1.6' stroke-linecap='round'/></svg>" },
        { L"delete", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                     L"<path d='M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/>"
                     L"<path d='M10 11v6M14 11v6' stroke='#e8edf5' stroke-width='1.6' stroke-linecap='round'/></svg>" },
        { L"minimize", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                       L"<rect x='3' y='4' width='18' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                       L"<path d='M7 15h10' stroke='#e8edf5' stroke-width='2.5' stroke-linecap='round'/></svg>" },
        { L"maximize", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                       L"<rect x='3' y='4' width='18' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                       L"<rect x='7' y='8' width='10' height='8' rx='1' fill='none' stroke='#e8edf5' stroke-width='1.8'/></svg>" },
        { L"max", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                  L"<rect x='3' y='4' width='18' height='16' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                  L"<rect x='7' y='8' width='10' height='8' rx='1' fill='none' stroke='#e8edf5' stroke-width='1.8'/></svg>" },
        { L"fullscreen", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                         L"<path d='M8 3H5a2 2 0 0 0-2 2v3M16 3h3a2 2 0 0 1 2 2v3M8 21H5a2 2 0 0 1-2-2v-3M16 21h3a2 2 0 0 0 2-2v-3' fill='none' stroke='#e8edf5' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
        { L"lock", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<rect x='5' y='11' width='14' height='10' rx='2' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                   L"<path d='M8 11V7a4 4 0 0 1 8 0v4' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round'/>"
                   L"<circle cx='12' cy='16' r='1.5' fill='#e8edf5'/></svg>" },
        { L"volume", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                     L"<path d='M11 5L6 9H2v6h4l5 4V5z' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linejoin='round'/>"
                     L"<path d='M15.5 8.5a5 5 0 0 1 0 7M18.5 6a8.5 8.5 0 0 1 0 12' fill='none' stroke='#e8edf5' stroke-width='1.8' stroke-linecap='round'/></svg>" },
        { L"calc", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                   L"<rect x='4' y='2' width='16' height='20' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                   L"<rect x='7' y='5' width='10' height='4' rx='1' fill='none' stroke='#e8edf5' stroke-width='1.5'/>"
                   L"<path d='M7 12h2M11 12h2M15 12h2M7 15h2M11 15h2M15 15h2M7 18h2M11 18h2M15 18h2' stroke='#e8edf5' stroke-width='2' stroke-linecap='round'/></svg>" },
        { L"calculator", L"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                         L"<rect x='4' y='2' width='16' height='20' rx='2.5' fill='none' stroke='#e8edf5' stroke-width='1.8'/>"
                         L"<rect x='7' y='5' width='10' height='4' rx='1' fill='none' stroke='#e8edf5' stroke-width='1.5'/>"
                         L"<path d='M7 12h2M11 12h2M15 12h2M7 15h2M11 15h2M15 15h2M7 18h2M11 18h2M15 18h2' stroke='#e8edf5' stroke-width='2' stroke-linecap='round'/></svg>" },
    };
    for (auto& e : k) if (name == e.n) return e.s;
    return nullptr;
}

// ---------- IconCache ----------

IconCache& IconCache::I() {
    static IconCache c;
    return c;
}

void IconCache::Clear() {
    for (auto& kv : m_d2d) if (kv.second) kv.second->Release();
    m_d2d.clear();
    m_wic.clear();
    for (auto& kv : m_hb) if (kv.second) DeleteObject(kv.second);
    m_hb.clear();
}

static bool RasterizeSvgBuffer(std::vector<char>& buf, int sizePx, std::vector<uint8_t>& outPbgra) {
    NSVGimage* img = nsvgParse(buf.data(), "px", 96.0f);
    if (!img || img->width <= 0 || img->height <= 0) { if (img) nsvgDelete(img); return false; }
    NSVGrasterizer* r = nsvgCreateRasterizer();
    if (!r) { nsvgDelete(img); return false; }
    outPbgra.assign((size_t)sizePx * sizePx * 4, 0);
    float s = sizePx / (img->width >= img->height ? img->width : img->height);
    nsvgRasterize(r, img, 0, 0, s, outPbgra.data(), sizePx, sizePx, sizePx * 4);
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
    // straight RGBA -> premultiplied BGRA
    for (size_t i = 0; i + 3 < outPbgra.size(); i += 4) {
        uint8_t r8 = outPbgra[i], g8 = outPbgra[i + 1], b8 = outPbgra[i + 2], a8 = outPbgra[i + 3];
        outPbgra[i]     = (uint8_t)(b8 * a8 / 255);
        outPbgra[i + 1] = (uint8_t)(g8 * a8 / 255);
        outPbgra[i + 2] = (uint8_t)(r8 * a8 / 255);
        outPbgra[i + 3] = a8;
    }
    return true;
}

static bool DecodeImageFile(const std::wstring& path, int sizePx, ComPtr<IWICBitmap>& out) {
    auto wic = GdiWicFactory();
    if (!wic) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
        return false;
    ComPtr<IWICBitmapScaler> scaler;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w > (UINT)sizePx || h > (UINT)sizePx) {
        if (SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
            double f = (double)sizePx / (double)(w > h ? w : h);
            UINT nw = (UINT)std::llround(w * f), nh = (UINT)std::llround(h * f);
            if (nw < 1) nw = 1;
            if (nh < 1) nh = 1;
            if (SUCCEEDED(scaler->Initialize(conv.Get(), nw, nh, WICBitmapInterpolationModeFant)))
                return SUCCEEDED(wic->CreateBitmapFromSource(scaler.Get(), WICBitmapCacheOnDemand, &out));
        }
    }
    return SUCCEEDED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnDemand, &out));
}

static ComPtr<IWICBitmap> IconFromHicon(HICON hicon) {
    ComPtr<IWICBitmap> bmp;
    if (!hicon) return bmp;
    GdiWicFactory()->CreateBitmapFromHICON(hicon, &bmp);
    DestroyIcon(hicon);
    return bmp;
}

static ComPtr<IWICBitmap> ScaleWic(const ComPtr<IWICBitmap>& src, int sizePx) {
    auto wic = GdiWicFactory();
    if (!wic || !src) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    if ((int)w == sizePx && (int)h == sizePx) return src;
    ComPtr<IWICBitmapScaler> sc;
    if (FAILED(wic->CreateBitmapScaler(&sc))) return nullptr;
    if (FAILED(sc->Initialize(src.Get(), sizePx, sizePx, WICBitmapInterpolationModeFant))) return nullptr;
    ComPtr<IWICBitmap> out;
    wic->CreateBitmapFromSource(sc.Get(), WICBitmapCacheOnDemand, &out);
    return out;
}

// HBITMAP (32bpp premultiplied BGRA as returned by IShellItemImageFactory) -> WIC bitmap
static ComPtr<IWICBitmap> WicFromShellHbitmap(HBITMAP hb) {
    BITMAP bm{};
    if (!GetObjectW(hb, sizeof(bm), &bm)) { DeleteObject(hb); return nullptr; }
    int w = bm.bmWidth, h = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { DeleteObject(hb); return nullptr; }
    std::vector<uint8_t> px((size_t)w * h * 4);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(nullptr);
    int got = GetDIBits(sdc, hb, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, sdc);
    DeleteObject(hb);
    if (!got) return nullptr;
    ComPtr<IWICBitmap> out;
    GdiWicFactory()->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA,
                                            w * 4, (UINT)px.size(), px.data(), &out);
    return out;
}

static ComPtr<IWICBitmap> ExtractAutoIcon(const std::wstring& path, int sizePx) {
    // modern shell path: works for files, folders and packages, alpha handled correctly
    IShellItemImageFactory* fac = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&fac))) && fac) {
        HBITMAP hb = nullptr;
        HRESULT hr = fac->GetImage(SIZE{ sizePx, sizePx },
                                   SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb);
        fac->Release();
        if (SUCCEEDED(hr) && hb) {
            ComPtr<IWICBitmap> out = WicFromShellHbitmap(hb);
            if (out) return out;
        }
    }
    // fallback: classic icon extraction
    HICON h = nullptr;
    if (SUCCEEDED(SHDefExtractIconW(path.c_str(), 0, 0, &h, nullptr, (UINT)sizePx)) && h)
        return IconFromHicon(h);
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
        return ScaleWic(IconFromHicon(sfi.hIcon), sizePx);
    return nullptr;
}

// builtin glyph strokes are authored light (#e8edf5); on the light theme swap
// them for a dark ink so they stay visible. User image/SVG files keep their own colors.
static std::wstring ThemeBuiltinSvg(const wchar_t* svg, bool dark) {
    std::wstring s = svg;
    if (!dark) {
        const std::wstring from = L"#e8edf5", to = L"#242a35";
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return s;
}

bool IconCache::GetWic(const std::wstring& source, int sizePx, bool dark, IWICBitmap** out) {
    *out = nullptr;
    std::wstring key = source + L"|" + std::to_wstring(sizePx) + (dark ? L"|d" : L"|l");
    auto it = m_wic.find(key);
    if (it != m_wic.end()) { *out = it->second.Get(); if (*out) (*out)->AddRef(); return true; }

    ComPtr<IWICBitmap> bmp;
    auto wic = GdiWicFactory();
    if (!wic) return false;

    if (source.rfind(L"builtin:", 0) == 0) {
        std::wstring name = source.substr(8);
        if (const wchar_t* svg = FindBuiltinSvg(name)) {
            std::wstring themed = ThemeBuiltinSvg(svg, dark);
            std::vector<char> buf;
            std::string s8 = Utf16ToUtf8(themed);
            buf.assign(s8.begin(), s8.end());
            buf.push_back('\0');
            std::vector<uint8_t> px;
            if (RasterizeSvgBuffer(buf, sizePx, px)) {
                wic->CreateBitmapFromMemory(sizePx, sizePx, GUID_WICPixelFormat32bppPBGRA,
                                            sizePx * 4, (UINT)px.size(), px.data(), &bmp);
            }
        }
    } else if (source.rfind(L"auto:", 0) == 0) {
        bmp = ExtractAutoIcon(source.substr(5), sizePx);
    } else {
        std::wstring actualPath = source;
        if (PathIsRelativeW(actualPath.c_str())) {
            std::wstring try1 = ExeDirW() + L"\\" + actualPath;
            if (PathFileExistsW_(try1)) actualPath = try1;
        }
        std::wstring ext = PathFindExtensionW(actualPath.c_str());
        if (_wcsicmp(ext.c_str(), L".svg") == 0) {
            std::vector<uint8_t> bytes;
            if (ReadFileBytes(actualPath, bytes)) {
                std::vector<char> buf(bytes.begin(), bytes.end());
                buf.push_back('\0');
                std::vector<uint8_t> px;
                if (RasterizeSvgBuffer(buf, sizePx, px))
                    wic->CreateBitmapFromMemory(sizePx, sizePx, GUID_WICPixelFormat32bppPBGRA,
                                                sizePx * 4, (UINT)px.size(), px.data(), &bmp);
            }
        } else {
            DecodeImageFile(actualPath, sizePx, bmp);
            if (bmp) {
                UINT w = 0, h = 0;
                bmp->GetSize(&w, &h);
                if ((int)w != sizePx && (int)h != sizePx) bmp = ScaleWic(bmp, sizePx);
            }
        }
    }
    if (!bmp) return false;
    m_wic[key] = bmp;
    *out = bmp.Get();
    (*out)->AddRef();
    return true;
}

ID2D1Bitmap* IconCache::GetD2D(ID2D1RenderTarget* rt, const std::wstring& source, float sizePx, bool dark) {
    int size = (int)std::lround(sizePx);
    wchar_t k[64];
    swprintf_s(k, L"%p|", (void*)rt);
    std::wstring key = k + source + L"|" + std::to_wstring(size) + (dark ? L"|d" : L"|l");
    auto it = m_d2d.find(key);
    if (it != m_d2d.end()) return it->second;
    ComPtr<IWICBitmap> wicBmp;
    if (!GetWic(source, size, dark, &wicBmp)) return nullptr;
    ID2D1Bitmap* bmp = nullptr;
    if (FAILED(rt->CreateBitmapFromWicBitmap(wicBmp.Get(), nullptr, &bmp))) return nullptr;
    m_d2d[key] = bmp;
    return bmp;
}

HBITMAP IconCache::GetHbitmap(const std::wstring& source, int sizePx) {
    std::wstring key = source + L"|" + std::to_wstring(sizePx);
    auto it = m_hb.find(key);
    if (it != m_hb.end()) return it->second;
    ComPtr<IWICBitmap> wicBmp;
    if (!GetWic(source, sizePx, true, &wicBmp)) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sizePx;
    bi.bmiHeader.biHeight = -sizePx;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb) return nullptr;
    WICRect rect{ 0, 0, sizePx, sizePx };
    if (FAILED(wicBmp->CopyPixels(&rect, sizePx * 4, (UINT)(sizePx * sizePx * 4), (BYTE*)bits))) {
        DeleteObject(hb);
        return nullptr;
    }
    m_hb[key] = hb;
    return hb;
}

void BlurBGRA(uint8_t* px, int w, int h, int radius) {
    if (radius < 1 || w < 2 || h < 2) return;
    static std::vector<uint8_t> s_tmp;
    if (s_tmp.size() < (size_t)w * h * 4) s_tmp.resize((size_t)w * h * 4);
    uint8_t* tmp = s_tmp.data();
    int win = radius * 2 + 1;
    // two separable box-blur passes (H then V), repeated twice for smoothness
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            const uint8_t* row = px + (size_t)y * w * 4;
            uint8_t* dst = tmp + (size_t)y * w * 4;
            int sum[3] = { 0, 0, 0 };
            for (int i = -radius; i <= radius; i++) {
                int x = std::min(std::max(i, 0), w - 1);
                sum[0] += row[(size_t)x * 4]; sum[1] += row[(size_t)x * 4 + 1]; sum[2] += row[(size_t)x * 4 + 2];
            }
            for (int x = 0; x < w; x++) {
                dst[(size_t)x * 4]     = (uint8_t)(sum[0] / win);
                dst[(size_t)x * 4 + 1] = (uint8_t)(sum[1] / win);
                dst[(size_t)x * 4 + 2] = (uint8_t)(sum[2] / win);
                dst[(size_t)x * 4 + 3] = 255;
                int add = std::min(x + radius + 1, w - 1), sub = std::max(x - radius, 0);
                sum[0] += row[(size_t)add * 4]     - row[(size_t)sub * 4];
                sum[1] += row[(size_t)add * 4 + 1] - row[(size_t)sub * 4 + 1];
                sum[2] += row[(size_t)add * 4 + 2] - row[(size_t)sub * 4 + 2];
            }
        }
        for (int x = 0; x < w; x++) {
            int sum[3] = { 0, 0, 0 };
            for (int i = -radius; i <= radius; i++) {
                int y = std::min(std::max(i, 0), h - 1);
                sum[0] += tmp[(size_t)y * w * 4 + (size_t)x * 4];
                sum[1] += tmp[(size_t)y * w * 4 + (size_t)x * 4 + 1];
                sum[2] += tmp[(size_t)y * w * 4 + (size_t)x * 4 + 2];
            }
            for (int y = 0; y < h; y++) {
                uint8_t* dst = px + (size_t)y * w * 4 + (size_t)x * 4;
                dst[0] = (uint8_t)(sum[0] / win);
                dst[1] = (uint8_t)(sum[1] / win);
                dst[2] = (uint8_t)(sum[2] / win);
                dst[3] = 255;
                int add = std::min(y + radius + 1, h - 1), sub = std::max(y - radius, 0);
                sum[0] += tmp[(size_t)add * w * 4 + (size_t)x * 4]     - tmp[(size_t)sub * w * 4 + (size_t)x * 4];
                sum[1] += tmp[(size_t)add * w * 4 + (size_t)x * 4 + 1] - tmp[(size_t)sub * w * 4 + (size_t)x * 4 + 1];
                sum[2] += tmp[(size_t)add * w * 4 + (size_t)x * 4 + 2] - tmp[(size_t)sub * w * 4 + (size_t)x * 4 + 2];
            }
        }
    }
}

void Downsample2x(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    if (!src || !dst || sw < 2 || sh < 2 || dw < 1 || dh < 1) return;
    for (int y = 0; y < dh; y++) {
        int sy0 = std::min(y * 2, sh - 1);
        int sy1 = std::min(y * 2 + 1, sh - 1);
        const uint8_t* s0 = src + (size_t)sy0 * sw * 4;
        const uint8_t* s1 = src + (size_t)sy1 * sw * 4;
        uint8_t* d = dst + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++) {
            int sx0 = std::min(x * 2, sw - 1) * 4;
            int sx1 = std::min(x * 2 + 1, sw - 1) * 4;
            d[x * 4 + 0] = (uint8_t)(((int)s0[sx0] + s0[sx1] + s1[sx0] + s1[sx1]) >> 2);
            d[x * 4 + 1] = (uint8_t)(((int)s0[sx0 + 1] + s0[sx1 + 1] + s1[sx0 + 1] + s1[sx1 + 1]) >> 2);
            d[x * 4 + 2] = (uint8_t)(((int)s0[sx0 + 2] + s0[sx1 + 2] + s1[sx0 + 2] + s1[sx1 + 2]) >> 2);
            d[x * 4 + 3] = 255;
        }
    }
}

// ---------- geometry ----------

WheelLayout ComputeLayout(const Appearance& a, float scale, float pad, float winW, float winH) {
    WheelLayout L;
    float d = a.diameter * scale;
    L.r = d / 2;
    L.cx = winW / 2;
    L.cy = winH / 2;
    L.r0 = L.r * 0.18f;
    L.iconR = L.r * 0.62f;
    L.labelR = L.r * 0.85f;
    L.sectors = a.sectorCount;
    L.gapDeg = a.sectorGap;
    L.rotationDeg = a.rotationDeg;
    return L;
}

static D2D1_POINT_2F CirclePt(float cx, float cy, float r, float deg) {
    float a = Deg2Rad(deg);
    return D2D1::Point2F(cx + r * cosf(a), cy + r * sinf(a));
}

D2D1_POINT_2F SectorIconCenter(const WheelLayout& L, int idx) {
    float mid = (L.sectors <= 1) ? (-90.0f + L.rotationDeg)
                                 : (-90.0f + L.rotationDeg + (idx + 0.5f) * 360.0f / L.sectors);
    return CirclePt(L.cx, L.cy, L.iconR, mid);
}

D2D1_POINT_2F SectorLabelCenter(const WheelLayout& L, int idx) {
    float mid = (L.sectors <= 1) ? (-90.0f + L.rotationDeg)
                                 : (-90.0f + L.rotationDeg + (idx + 0.5f) * 360.0f / L.sectors);
    return CirclePt(L.cx, L.cy, L.labelR, mid);
}

int WheelHitTest(const WheelLayout& L, float x, float y) {
    float dx = x - L.cx, dy = y - L.cy;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= L.r0) return -2;   // center deadzone (cancel)
    if (L.sectors <= 1) return 0;  // only 1 sector: any angle outside deadzone activates sector 0
    float ang = atan2f(dy, dx) * 180.0f / kPiF - (-90.0f + L.rotationDeg);
    while (ang < 0.0f) ang += 360.0f;
    while (ang >= 360.0f) ang -= 360.0f;
    int idx = (int)(ang / (360.0f / L.sectors));
    if (idx >= L.sectors) idx = L.sectors - 1;
    return idx;
}

static void AddArc(ID2D1GeometrySink* sink, const D2D1_POINT_2F& end, float r, D2D1_SWEEP_DIRECTION dir) {
    D2D1_ARC_SEGMENT arc{};
    arc.point = end;
    arc.size = D2D1::SizeF(r, r);
    arc.rotationAngle = 0;
    arc.sweepDirection = dir;
    arc.arcSize = D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
}

ComPtr<ID2D1PathGeometry> AnnulusSectorGeometry(const WheelLayout& L, int idx, float gapDeg) {
    auto f = GdiD2DFactory();
    if (!f) return nullptr;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(f->CreatePathGeometry(&geo)) || !geo) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink)) || !sink) return nullptr;

    if (L.sectors <= 1) {
        sink->BeginFigure(CirclePt(L.cx, L.cy, L.r, 0), D2D1_FIGURE_BEGIN_FILLED);
        AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r, 180), L.r, D2D1_SWEEP_DIRECTION_CLOCKWISE);
        AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r, 360), L.r, D2D1_SWEEP_DIRECTION_CLOCKWISE);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(CirclePt(L.cx, L.cy, L.r0, 0), D2D1_FIGURE_BEGIN_FILLED);
        AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r0, 180), L.r0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE);
        AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r0, 360), L.r0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return nullptr;
        return geo;
    }

    float a0 = -90.0f + L.rotationDeg + idx * 360.0f / L.sectors + gapDeg / 2;
    float a1 = -90.0f + L.rotationDeg + (idx + 1) * 360.0f / L.sectors - gapDeg / 2;
    sink->BeginFigure(CirclePt(L.cx, L.cy, L.r0, a0), D2D1_FIGURE_BEGIN_FILLED);
    AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r0, a1), L.r0, D2D1_SWEEP_DIRECTION_CLOCKWISE);
    sink->AddLine(CirclePt(L.cx, L.cy, L.r, a1));
    AddArc(sink.Get(), CirclePt(L.cx, L.cy, L.r, a0), L.r, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

// ---------- WheelSurface ----------

WheelSurface::~WheelSurface() { Release(); }

void WheelSurface::Resize(float wPx, float hPx) {
    int w = (int)std::lround(wPx), h = (int)std::lround(hPx);
    if (w < 8 || h < 8) return;
    if (m_wicBmp && (int)m_w == w && (int)m_h == h) return;
    Release();
    auto wic = GdiWicFactory();
    auto d2d = GdiD2DFactory();
    if (!wic || !d2d) return;
    if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &m_wicBmp))) return;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);  // 1 DIP == 1 px: all geometry is computed in pixels
    if (FAILED(d2d->CreateWicBitmapRenderTarget(m_wicBmp.Get(), &props, &m_rt))) {
        m_wicBmp.Reset();
        return;
    }
    m_rt->CreateLayer(&m_layer);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (m_dib) {
        m_memDC = CreateCompatibleDC(nullptr);
        SelectObject(m_memDC, m_dib);
    }
    m_w = (float)w;
    m_h = (float)h;
}

void WheelSurface::Release() {
    m_rt.Reset();
    m_layer.Reset();
    m_backdrop.Reset();
    m_backdropKey = nullptr;
    m_backdropW = m_backdropH = 0;
    m_glass.Reset();
    m_glassBits.clear();
    m_glassBits.shrink_to_fit();
    m_glassKey = 0;
    m_glassValid = false;
    m_glassW = m_glassH = 0;
    m_wicBmp.Reset();
    if (m_memDC) { if (m_dib) SelectObject(m_memDC, nullptr); DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_dib) { DeleteObject(m_dib); m_dib = nullptr; }
    m_bits = nullptr;
    m_w = m_h = 0;
}

// create or update the D2D bitmap for the acrylic snapshot
void WheelSurface::EnsureBackdrop(const WheelFrameState& st) {
    if (!st.backdrop || st.backdropW <= 0 || st.backdropH <= 0) {
        m_backdrop.Reset();
        m_backdropKey = nullptr;
        m_backdropSeq = 0;
        m_backdropW = m_backdropH = 0;
        return;
    }
    if (m_backdrop && m_backdropW == st.backdropW && m_backdropH == st.backdropH) {
        if (m_backdropSeq != st.backdropSeq) {
            D2D1_RECT_U rc = { 0, 0, (UINT32)st.backdropW, (UINT32)st.backdropH };
            m_backdrop->CopyFromMemory(&rc, st.backdrop, (UINT32)st.backdropW * 4);
            m_backdropSeq = st.backdropSeq;
            m_backdropKey = st.backdrop;
        }
        return;
    }

    m_backdrop.Reset();
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (SUCCEEDED(m_rt->CreateBitmap(D2D1::SizeU((UINT32)st.backdropW, (UINT32)st.backdropH),
                                     st.backdrop, (UINT32)st.backdropW * 4, &bp, &m_backdrop))) {
        m_backdropW = st.backdropW;
        m_backdropH = st.backdropH;
        m_backdropSeq = st.backdropSeq;
        m_backdropKey = st.backdrop;
    }
}

// build or refresh the finished liquid-glass texture (bgMode 2); cached on
// (snapshot seq, glass params, geometry) so hover-only redraws reuse it
void WheelSurface::EnsureGlass(const WheelFrameState& st, const WheelLayout& L) {
    if (!st.backdrop || st.backdropW <= 0 || st.backdropH <= 0 || !m_rt) {
        m_glassValid = false;
        return;
    }
    float f = (st.backdropDispW > 0) ? (float)st.backdropW / st.backdropDispW : 1.0f;
    float ccx = (m_w / 2 - st.backdropX) * f;
    float ccy = (m_h / 2 - st.backdropY) * f;
    float rO = L.r * f, rI = L.r0 * f;
    GlassParams p = GlassParamsFromApp(*st.app, st.scale * f);
    if (!st.dark) {
        D2D1_COLOR_F c = ParseColor(st.app->bgColorLight, 1.0f);
        p.tintR = c.r; p.tintG = c.g; p.tintB = c.b;
    }
    uint64_t key = GlassParamKey(p, st.backdropW, st.backdropH, ccx, ccy, rO, rI, st.backdropSeq);
    if (m_glassValid && m_glass && key == m_glassKey) return;

    size_t bytes = (size_t)st.backdropW * st.backdropH * 4;
    if (m_glassBits.size() != bytes) m_glassBits.resize(bytes);
    // GPU-first: the D3D11 path keeps the live backdrop affordable; the CPU
    // loop stays as fallback for machines without a usable hardware device
    if (!BuildGlassTextureGPU(m_glassBits.data(), st.backdrop, st.backdropW, st.backdropH,
                              ccx, ccy, rO, rI, p))
        BuildGlassTexture(m_glassBits.data(), st.backdrop, st.backdropW, st.backdropH,
                          ccx, ccy, rO, rI, p);

    if (!m_glass || m_glassW != st.backdropW || m_glassH != st.backdropH) {
        m_glass.Reset();
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(m_rt->CreateBitmap(D2D1::SizeU((UINT32)st.backdropW, (UINT32)st.backdropH),
                                      m_glassBits.data(), (UINT32)st.backdropW * 4, &bp, &m_glass))) {
            m_glassValid = false;
            return;
        }
        m_glassW = st.backdropW;
        m_glassH = st.backdropH;
    } else {
        D2D1_RECT_U rc = { 0, 0, (UINT32)st.backdropW, (UINT32)st.backdropH };
        m_glass->CopyFromMemory(&rc, m_glassBits.data(), (UINT32)st.backdropW * 4);
    }
    m_glassKey = key;
    m_glassValid = true;
}

static ComPtr<ID2D1GeometryGroup> CreateRingGeometry(const WheelLayout& L) {
    auto f = GdiD2DFactory();
    if (!f) return nullptr;
    ID2D1Geometry* geoms[2] = { nullptr, nullptr };
    if (FAILED(f->CreateEllipseGeometry(D2D1::Ellipse(MakePt(L.cx, L.cy), L.r, L.r), (ID2D1EllipseGeometry**)&geoms[0]))) return nullptr;
    if (FAILED(f->CreateEllipseGeometry(D2D1::Ellipse(MakePt(L.cx, L.cy), L.r0, L.r0), (ID2D1EllipseGeometry**)&geoms[1]))) {
        geoms[0]->Release();
        return nullptr;
    }
    ComPtr<ID2D1GeometryGroup> ring;
    f->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, geoms, 2, &ring);
    geoms[0]->Release();
    geoms[1]->Release();
    return ring;
}

void WheelSurface::Render(const WheelFrameState& st, float pad) {
    if (!m_rt || !m_bits) return;
    const Appearance& a = *st.app;
    // resolved palette: dark set, or the light set when the frame says so
    const std::wstring& bgColor     = st.dark ? a.bgColor     : a.bgColorLight;
    std::wstring hoverColor = st.dark ? a.hoverColor : a.hoverColorLight;
    if (a.hoverAccent) {
        std::wstring accent = SystemAccentHex();
        if (!accent.empty()) hoverColor = accent;   // follow system theme color
    }
    const std::wstring& borderColor = st.dark ? a.borderColor : a.borderColorLight;
    const std::wstring& textColor   = st.dark ? a.textColor   : a.textColorLight;
    ID2D1RenderTarget* rt = m_rt.Get();
    auto dw = GdiDWriteFactory();
    WheelLayout L = ComputeLayout(a, st.scale, pad, m_w, m_h);

    rt->BeginDraw();
    rt->Clear(nullptr);

    ComPtr<ID2D1SolidColorBrush> br;
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &br);
    if (!br) { rt->EndDraw(); return; }

    float t = st.animT < 0 ? 0 : (st.animT > 1 ? 1 : st.animT);

    ComPtr<ID2D1GeometryGroup> ring = CreateRingGeometry(L);

    bool drewBackdrop = false;

    // background ring: acrylic = blurred screen snapshot clipped to the ring, tinted
    bool acrylic = (a.bgMode == 1 && st.backdrop && st.backdropW > 0 && st.backdropH > 0 && m_layer);
    if (acrylic) {
        EnsureBackdrop(st);
        if (m_backdrop && ring) {
            D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(
                D2D1::RectF(0, 0, m_w, m_h), ring.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE);
            rt->PushLayer(lp, m_layer.Get());
            float dispW = (st.backdropDispW > 0) ? st.backdropDispW : (float)st.backdropW;
            float dispH = (st.backdropDispH > 0) ? st.backdropDispH : (float)st.backdropH;
            rt->DrawBitmap(m_backdrop.Get(),
                           D2D1::RectF(st.backdropX, st.backdropY,
                                       st.backdropX + dispW, st.backdropY + dispH),
                           t, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            rt->PopLayer();
            br->SetColor(ParseColor(bgColor, a.bgOpacity * 0.55f * t));
            rt->FillGeometry(ring.Get(), br.Get());
            drewBackdrop = true;
        }
    }

    // background ring: liquid glass = refracted screen snapshot, tint/highlight
    // already baked into the texture
    if (!drewBackdrop && a.bgMode == 2 && st.backdrop && st.backdropW > 0 && st.backdropH > 0 && m_layer) {
        EnsureGlass(st, L);
        if (m_glassValid && m_glass && ring) {
            D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(
                D2D1::RectF(0, 0, m_w, m_h), ring.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE);
            rt->PushLayer(lp, m_layer.Get());
            float dispW = (st.backdropDispW > 0) ? st.backdropDispW : (float)st.backdropW;
            float dispH = (st.backdropDispH > 0) ? st.backdropDispH : (float)st.backdropH;
            rt->DrawBitmap(m_glass.Get(),
                           D2D1::RectF(st.backdropX, st.backdropY,
                                       st.backdropX + dispW, st.backdropY + dispH),
                           t, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            rt->PopLayer();
            drewBackdrop = true;
        }
    }

    if (!drewBackdrop) {
        if (ring) {
            br->SetColor(ParseColor(bgColor, a.bgOpacity * t));
            rt->FillGeometry(ring.Get(), br.Get());
        }
    }

    // hover sector
    if (st.hover >= 0 && st.slots && st.hover < (int)st.slots->size()) {
        // hover covers the sector's full angle so the highlight reaches the
        // separator lines; gapDeg=0
        if (ComPtr<ID2D1PathGeometry> geo = AnnulusSectorGeometry(L, st.hover, 0)) {
            br->SetColor(ParseColor(hoverColor, a.hoverOpacity * t));
            rt->FillGeometry(geo.Get(), br.Get());
        }
    }

    // sector separator lines
    if (a.sectorGap >= 0.01f && L.sectors > 1) {
        br->SetColor(ParseColor(borderColor, 0.55f * t));
        for (int i = 0; i < L.sectors; i++) {
            float ang = -90.0f + L.rotationDeg + i * 360.0f / L.sectors + L.gapDeg / 2;
            rt->DrawLine(CirclePt(L.cx, L.cy, L.r0, ang), CirclePt(L.cx, L.cy, L.r, ang), br.Get(), 1.0f * st.scale);
        }
    }

    // inner border around the hollow center hole
    float innerBw = std::max(1.0f * st.scale, a.borderWidth * st.scale);
    br->SetColor(ParseColor(borderColor, 0.85f * t));
    rt->DrawEllipse(D2D1::Ellipse(MakePt(L.cx, L.cy), L.r0 + innerBw / 2, L.r0 + innerBw / 2),
                    br.Get(), innerBw);

    // outer border
    if (a.borderWidth > 0.01f) {
        br->SetColor(ParseColor(borderColor, 0.9f * t));
        rt->DrawEllipse(D2D1::Ellipse(MakePt(L.cx, L.cy), L.r - a.borderWidth * st.scale / 2, L.r - a.borderWidth * st.scale / 2),
                        br.Get(), a.borderWidth * st.scale);
    }

    // icons + labels
    int n = L.sectors;
    if (st.slots) n = (int)std::min((size_t)L.sectors, st.slots->size());
    float iconPx = a.iconSize * st.scale;
    for (int i = 0; i < n; i++) {
        const Slot& s = (*st.slots)[i];
        if (s.Empty()) continue;
        D2D1_POINT_2F ic = SectorIconCenter(L, i);
        if (ID2D1Bitmap* bmp = IconCache::I().GetD2D(rt, SlotIconSource(s), iconPx, st.dark)) {
            D2D1_RECT_F dst{ ic.x - iconPx / 2, ic.y - iconPx / 2, ic.x + iconPx / 2, ic.y + iconPx / 2 };
            rt->DrawBitmap(bmp, dst, t, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            br->SetColor(ParseColor(textColor, 0.35f * t));
            rt->DrawEllipse(D2D1::Ellipse(ic, iconPx / 2, iconPx / 2), br.Get(), 1.5f * st.scale);
        }
        if (a.showLabels && !s.name.empty()) {
            D2D1_POINT_2F lc = SectorLabelCenter(L, i);
            float chord = (L.sectors <= 1) ? (160.0f * st.scale)
                                           : (2 * (L.labelR + 10) * sinf(kPiF / L.sectors) * 0.9f);
            D2D1_RECT_F tr{ lc.x - chord / 2, lc.y - 11 * st.scale, lc.x + chord / 2, lc.y + 11 * st.scale };
            if (IDWriteTextFormat* tf = GetTextFormat(a.labelFont, a.labelSize * st.scale, dw.Get())) {
                br->SetColor(ParseColor(textColor, 0.95f * t));
                rt->DrawTextW(s.name.c_str(), (UINT32)s.name.size(), tf, tr, br.Get());
            }
        }
    }

    // center text
    if (a.centerMode == 0 && st.pageCount > 1) {
        wchar_t txt[32];
        swprintf_s(txt, L"%d/%d", st.pageIdx + 1, st.pageCount);
        std::wstring s = txt;
        D2D1_RECT_F tr{ L.cx - L.r0, L.cy - L.r0, L.cx + L.r0, L.cy + L.r0 };
        if (IDWriteTextFormat* tf = GetTextFormat(a.labelFont, 11 * st.scale, dw.Get())) {
            br->SetColor(D2D1::ColorF(0, 0, 0, 0.65f * t));
            D2D1_RECT_F trSh = tr;
            trSh.top += 1.0f * st.scale; trSh.bottom += 1.0f * st.scale;
            rt->DrawTextW(s.c_str(), (UINT32)s.size(), tf, trSh, br.Get());
            br->SetColor(ParseColor(textColor, 0.95f * t));
            rt->DrawTextW(s.c_str(), (UINT32)s.size(), tf, tr, br.Get());
        }
    } else if (a.centerMode == 1 && !a.centerText.empty()) {
        float r0 = L.r0 + 2 * st.scale;
        D2D1_RECT_F tr{ L.cx - r0, L.cy - r0, L.cx + r0, L.cy + r0 };
        if (IDWriteTextFormat* tf = GetTextFormat(a.labelFont, 10 * st.scale, dw.Get())) {
            br->SetColor(D2D1::ColorF(0, 0, 0, 0.65f * t));
            D2D1_RECT_F trSh = tr;
            trSh.top += 1.0f * st.scale; trSh.bottom += 1.0f * st.scale;
            rt->DrawTextW(a.centerText.c_str(), (UINT32)a.centerText.size(), tf, trSh, br.Get());
            br->SetColor(ParseColor(textColor, 0.95f * t));
            rt->DrawTextW(a.centerText.c_str(), (UINT32)a.centerText.size(), tf, tr, br.Get());
        }
    }

    rt->EndDraw();

    // flush WIC pixels into the DIB
    WICRect rect{ 0, 0, (int)m_w, (int)m_h };
    m_wicBmp->CopyPixels(&rect, (UINT)m_w * 4, (UINT)(m_w * m_h * 4), (BYTE*)m_bits);
}
