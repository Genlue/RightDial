// standalone probe: verifies DownsampleArea (static in wheel.cpp) preserves
// the BGRA channel order at arbitrary downsample ratios. Regression test for
// the R/B swap that tinted the wheel blue when capture clarity (backdropScale)
// was below 100% — pure red came out pure blue. Not part of the product build.
#include "stdafx.h"
#include "app.h"
#include "wheel.cpp"
#include <cstdio>

AppConfig g_cfg;
HWND      g_mainHwnd = nullptr;
HINSTANCE g_hInst = nullptr;

// normally lives in main.cpp; not linked into this probe
void RequestExecSlot(const Slot&) {}

static bool CheckSolid(int sb, int sg, int sr) {
    const int sw = 64, sh = 64, dw = 17, dh = 13;   // odd ratio to hit rounding
    std::vector<uint8_t> src((size_t)sw * sh * 4, 0), dst((size_t)dw * dh * 4, 0);
    for (int i = 0; i < sw * sh; i++) {
        src[i * 4] = (uint8_t)sb; src[i * 4 + 1] = (uint8_t)sg;
        src[i * 4 + 2] = (uint8_t)sr; src[i * 4 + 3] = 255;
    }
    DownsampleArea(src.data(), sw, sh, dst.data(), dw, dh);
    for (int i = 0; i < dw * dh; i++) {
        if (dst[i * 4] != sb || dst[i * 4 + 1] != sg || dst[i * 4 + 2] != sr) {
            printf("FAIL solid BGR(%d,%d,%d): px %d got BGR(%d,%d,%d)\n",
                   sb, sg, sr, i, dst[i * 4], dst[i * 4 + 1], dst[i * 4 + 2]);
            return false;
        }
    }
    return true;
}

// channels must stay independent through the area average: a horizontal ramp
// in byte 0 with flat bytes 1/2 must produce a ramp in output byte 0 only
static bool CheckChannelIndependence() {
    const int sw = 64, sh = 64, dw = 16, dh = 16;
    std::vector<uint8_t> src((size_t)sw * sh * 4, 0), dst((size_t)dw * dh * 4, 0);
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            uint8_t* p = src.data() + ((size_t)y * sw + x) * 4;
            p[0] = (uint8_t)(x * 4);   // blue ramp 0..252
            p[1] = 200;                // flat green
            p[2] = 30;                 // flat red
            p[3] = 255;
        }
    DownsampleArea(src.data(), sw, sh, dst.data(), dw, dh);
    for (int y = 0; y < dh; y++) {
        const uint8_t* row = dst.data() + (size_t)y * dw * 4;
        if (row[1] != 200 || row[2] != 30 || row[(dw - 1) * 4 + 1] != 200 ||
            row[(dw - 1) * 4 + 2] != 30) {
            printf("FAIL independence: flat channel drifted (B=%d G=%d R=%d)\n",
                   row[0], row[1], row[2]);
            return false;
        }
        if (row[0] > 8 || row[(dw - 1) * 4] < 240) {
            printf("FAIL independence: blue ramp ends wrong (%d .. %d)\n",
                   row[0], row[(dw - 1) * 4]);
            return false;
        }
    }
    return true;
}

int main() {
    int fails = 0;
    fails += !CheckSolid(255, 0, 0);    // pure blue must stay pure blue
    fails += !CheckSolid(0, 0, 255);    // pure red must stay pure red
    fails += !CheckSolid(0, 255, 0);    // pure green
    fails += !CheckSolid(10, 200, 30);  // arbitrary mix
    fails += !CheckChannelIndependence();
    if (fails) { printf("test_downsample: %d FAIL\n", fails); return 1; }
    printf("test_downsample: all pass\n");
    return 0;
}
