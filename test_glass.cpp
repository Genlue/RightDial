// standalone harness: exercises the liquid-glass render path headlessly and
// dumps the result to a PNG. Not part of the product build.
#include "stdafx.h"
#include "app.h"
#include "render.h"
#include "config.h"
#include "glass.h"
#include <cstdio>
AppConfig g_cfg;
HWND      g_mainHwnd = nullptr;
HINSTANCE g_hInst = nullptr;

static void SavePng(const char* path, int w, int h, const uint8_t* bgra) {
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, (UINT)(w * h * 4), (BYTE*)bgra, &bmp);
    ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    stream->InitializeFromFilename(wpath, GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize((UINT)w, (UINT)h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(h, w * 4, (UINT)(w * h * 4), (BYTE*)bgra);
    frame->Commit();
    enc->Commit();
}

// copy the rendered DIB bits out of the surface
static void GrabDib(WheelSurface& s, std::vector<uint8_t>& out, int& ow, int& oh) {
    ow = (int)s.W(); oh = (int)s.H();
    out.resize((size_t)ow * oh * 4);
    HDC sdc = GetDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = ow; bi.bmiHeader.biHeight = -oh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(sdc, s.Dib(), 0, oh, out.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, sdc);
}

// CPU vs GPU glass pipeline: per-call timing and pixel diff stats
static void CompareCpuGpu(const std::vector<uint8_t>& snap, int w, int h, const Appearance& a) {
    GlassParams p = GlassParamsFromApp(a, 1.0f);
    float ccx = w / 2.0f, ccy = h / 2.0f, rO = 160.0f, rI = 160.0f * 0.18f;
    std::vector<uint8_t> cpu((size_t)w * h * 4), gpu((size_t)w * h * 4);
    if (!cpu.size() || !gpu.size()) return;

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);

    bool gpuOk = BuildGlassTextureGPU(gpu.data(), snap.data(), w, h, ccx, ccy, rO, rI, p);
    printf("gpu available: %s\n", gpuOk ? "yes" : "no (falling back to CPU in product)");
    if (!gpuOk) return;

    const int reps = 30;
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < reps; i++)
        BuildGlassTextureGPU(gpu.data(), snap.data(), w, h, ccx, ccy, rO, rI, p);
    QueryPerformanceCounter(&t1);
    double gpuMs = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / reps;

    QueryPerformanceCounter(&t0);
    for (int i = 0; i < reps; i++)
        BuildGlassTexture(cpu.data(), snap.data(), w, h, ccx, ccy, rO, rI, p);
    QueryPerformanceCounter(&t1);
    double cpuMs = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / reps;

    double sum = 0, mx = 0;
    size_t n = (size_t)w * h * 4;
    for (size_t i = 0; i < n; i++) {
        double d = fabs((double)cpu[i] - (double)gpu[i]);
        sum += d;
        if (d > mx) mx = d;
    }
    printf("glass %dx%d: cpu %.2f ms, gpu %.2f ms (%.1fx) | maxdiff %.0f, avgdiff %.2f\n",
           w, h, cpuMs, gpuMs, cpuMs / gpuMs, mx, sum / n);
    SavePng("bin/glass_test_cpu.png", w, h, cpu.data());
    SavePng("bin/glass_test_gpu.png", w, h, gpu.data());
}

// synthetic passthrough check: gradient in, all glass effects off; the GPU
// output must reproduce the input. Isolates coordinate/sampler/cbuffer bugs.
static void SyntheticCheck(const Appearance& a) {
    int w = 256, h = 256;
    std::vector<uint8_t> src((size_t)w * h * 4), gpu((size_t)w * h * 4, 0xAA);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = src.data() + ((size_t)y * w + x) * 4;
            p[0] = 0; p[1] = (uint8_t)y; p[2] = (uint8_t)x; p[3] = 255;   // BGRA
        }
    Appearance a2 = a;
    a2.glassBlur = 0; a2.glassHeight = 0; a2.glassAmount = 0;
    a2.glassChroma = a2.glassVibrancy = a2.glassHighlight = false;
    a2.bgOpacity = 0.0f;
    GlassParams p = GlassParamsFromApp(a2, 1.0f);
    bool ok = BuildGlassTextureGPU(gpu.data(), src.data(), w, h, w / 2.0f, h / 2.0f, 100.0f, 18.0f, p);
    printf("synthetic: ok=%d tl=(%d,%d) tr=(%d,%d) bl=(%d,%d) [expect tl=(0,0) tr=(255,0) bl=(0,255)]\n",
           ok,
           gpu[2], gpu[1],
           gpu[((size_t)0 * w + w - 1) * 4 + 2], gpu[((size_t)0 * w + w - 1) * 4 + 1],
           gpu[((size_t)(h - 1) * w) * 4 + 2], gpu[((size_t)(h - 1) * w) * 4 + 1]);
    SavePng("bin/glass_test_synth.png", w, h, gpu.data());
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Appearance& a = g_cfg.app;
    a.diameter = 320; a.sectorCount = 8; a.bgMode = 2;
    a.bgColor = L"#1e2430"; a.bgOpacity = 0.86f;
    a.iconSize = 46; a.showLabels = true; a.centerMode = 0;
    a.glassBlur = 5; a.glassHeight = 22; a.glassAmount = 44;
    a.glassChroma = true; a.glassVibrancy = true; a.glassHighlight = true;
    if (argc > 1 && argv[1][0] == 'p') { // plain: glass pipeline off
        a.glassBlur = 0; a.glassHeight = 0; a.glassAmount = 0;
        a.glassChroma = false; a.glassVibrancy = false; a.glassHighlight = false;
    }

    Page pg;
    const wchar_t* icons[] = { L"builtin:paste", L"builtin:copy", L"builtin:cut", L"builtin:undo",
                               L"builtin:save", L"builtin:search", L"builtin:folder", L"builtin:terminal" };
    for (int i = 0; i < 8; i++) {
        Slot s; s.type = SlotType::Hotkey; s.name = L"项目"; s.keys = L"Ctrl+V"; s.iconPath = icons[i];
        pg.slots.push_back(s);
    }

    int w = 400, h = 400;
    std::vector<uint8_t> snap((size_t)w * h * 4);
    HDC sdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(sdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, dib);
    BitBlt(mem, 0, 0, w, h, sdc, 200, 200, SRCCOPY | CAPTUREBLT);
    memcpy(snap.data(), bits, (size_t)w * h * 4);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, sdc);

    WheelSurface surf;
    fprintf(stderr, "cp: before Resize\n"); fflush(stderr);
    surf.Resize((float)w, (float)h);
    fprintf(stderr, "cp: after Resize\n"); fflush(stderr);
    WheelFrameState st{};
    st.app = &a;
    st.slots = &pg.slots;
    st.pageCount = 1; st.pageIdx = 0; st.hover = 0;
    st.animT = 1.0f; st.scale = 1.0f;
    st.backdrop = snap.data();
    st.backdropW = w; st.backdropH = h;
    st.backdropDispW = (float)w; st.backdropDispH = (float)h;
    st.backdropX = 0; st.backdropY = 0;
    st.backdropSeq = 1;

    fprintf(stderr, "cp: before Render\n"); fflush(stderr);
    surf.Render(st, 8.0f);
    fprintf(stderr, "cp: after Render\n"); fflush(stderr);

    std::vector<uint8_t> out;
    int ow = 0, oh = 0;
    fprintf(stderr, "cp: before Grab\n"); fflush(stderr);
    GrabDib(surf, out, ow, oh);
    fprintf(stderr, "cp: after Grab\n"); fflush(stderr);
    SavePng("bin/glass_test.png", ow, oh, out.data());
    printf("saved bin/glass_test.png %dx%d\n", ow, oh);
    SyntheticCheck(a);
    CompareCpuGpu(snap, w, h, a);
    CoUninitialize();
    return 0;
}
