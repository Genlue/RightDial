#include "stdafx.h"
#include "glass.h"

// Liquid glass rendering pipeline.
//
// The refraction math is transplanted 1:1 from the AGSL shaders of
// Kyant0/AndroidLiquidGlass (https://github.com/Kyant0/AndroidLiquidGlass,
// Apache License 2.0, Copyright 2025 Kyant):
//   circleMap          - quarter-circle refraction profile (doc section 3.4)
//   sdRoundedRect      - signed distance, negative inside (doc section 3.2)
//   gradSdRoundedRect  - unit SDF gradient pointing outward (doc section 3.3)
//   7-tap dispersion   - spectral weights table (doc section 4.2)
//   vibrancy           - colorControls(saturation = 1.5) matrix (doc section 6.2)
//   lens semantics     - early-out at depth H, sd=min(sd,0), amount passed
//                        negative, sample pulled inward (doc sections 4.1/5.1)
// The doc's rounded rect specializes to the wheel's annulus: outer circle and
// center-hole circle SDFs combined with max(). depthEffect is omitted because
// on a circle the SDF gradient is already radial (the term would be a no-op).

static inline float circleMap(float x) {
    return 1.0f - sqrtf(std::max(0.0f, 1.0f - x * x));
}

// signed distance of the glass annulus: < 0 inside the glass
static inline float SdAnnulus(float dx, float dy, float rO, float rI) {
    float dist = sqrtf(dx * dx + dy * dy);
    float dOut = dist - rO;   // outer rim
    float dIn  = rI - dist;   // hole rim (outside counts positive)
    return dOut > dIn ? dOut : dIn;
}

// unit gradient pointing out of the glass
static inline void GradAnnulus(float dx, float dy, float rO, float rI, float& gx, float& gy) {
    float dist = sqrtf(dx * dx + dy * dy);
    float s = (dist - rO >= rI - dist) ? 1.0f : -1.0f;
    float inv = s / std::max(dist, 1e-4f);
    gx = dx * inv;
    gy = dy * inv;
}

static inline void SampleBilinear(const uint8_t* img, int w, int h, float x, float y, float* out) {
    x = std::min(std::max(x, 0.0f), (float)(w - 1));
    y = std::min(std::max(y, 0.0f), (float)(h - 1));
    int x0 = (int)x, y0 = (int)y;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float fx = x - x0, fy = y - y0;
    const uint8_t* p00 = img + ((size_t)y0 * w + x0) * 4;
    const uint8_t* p10 = img + ((size_t)y0 * w + x1) * 4;
    const uint8_t* p01 = img + ((size_t)y1 * w + x0) * 4;
    const uint8_t* p11 = img + ((size_t)y1 * w + x1) * 4;
    for (int c = 0; c < 4; c++) {
        float a = p00[c] + (p10[c] - p00[c]) * fx;
        float b = p01[c] + (p11[c] - p01[c]) * fx;
        out[c] = a + (b - a) * fy;
    }
}

// 3 box passes approximate a gaussian with sigma ~= box radius
static void GaussianApproxBGRA(uint8_t* img, uint8_t* tmp, int w, int h, float sigma) {
    int r = (int)std::lround(sigma);
    if (r < 1 || w < 2 || h < 2) return;
    if (r > 96) r = 96;
    int win = r * 2 + 1;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            const uint8_t* row = img + (size_t)y * w * 4;
            uint8_t* dst = tmp + (size_t)y * w * 4;
            int sum[3] = { 0, 0, 0 };
            for (int i = -r; i <= r; i++) {
                int x = std::min(std::max(i, 0), w - 1);
                sum[0] += row[(size_t)x * 4];
                sum[1] += row[(size_t)x * 4 + 1];
                sum[2] += row[(size_t)x * 4 + 2];
            }
            for (int x = 0; x < w; x++) {
                dst[(size_t)x * 4]     = (uint8_t)(sum[0] / win);
                dst[(size_t)x * 4 + 1] = (uint8_t)(sum[1] / win);
                dst[(size_t)x * 4 + 2] = (uint8_t)(sum[2] / win);
                dst[(size_t)x * 4 + 3] = 255;
                int add = std::min(x + r + 1, w - 1), sub = std::max(x - r, 0);
                sum[0] += row[(size_t)add * 4]     - row[(size_t)sub * 4];
                sum[1] += row[(size_t)add * 4 + 1] - row[(size_t)sub * 4 + 1];
                sum[2] += row[(size_t)add * 4 + 2] - row[(size_t)sub * 4 + 2];
            }
        }
        for (int x = 0; x < w; x++) {
            int sum[3] = { 0, 0, 0 };
            for (int i = -r; i <= r; i++) {
                int y = std::min(std::max(i, 0), h - 1);
                sum[0] += tmp[(size_t)y * w * 4 + (size_t)x * 4];
                sum[1] += tmp[(size_t)y * w * 4 + (size_t)x * 4 + 1];
                sum[2] += tmp[(size_t)y * w * 4 + (size_t)x * 4 + 2];
            }
            for (int y = 0; y < h; y++) {
                uint8_t* dst = img + (size_t)y * w * 4 + (size_t)x * 4;
                dst[0] = (uint8_t)(sum[0] / win);
                dst[1] = (uint8_t)(sum[1] / win);
                dst[2] = (uint8_t)(sum[2] / win);
                dst[3] = 255;
                int add = std::min(y + r + 1, h - 1), sub = std::max(y - r, 0);
                sum[0] += tmp[(size_t)add * w * 4 + (size_t)x * 4]     - tmp[(size_t)sub * w * 4 + (size_t)x * 4];
                sum[1] += tmp[(size_t)add * w * 4 + (size_t)x * 4 + 1] - tmp[(size_t)sub * w * 4 + (size_t)x * 4 + 1];
                sum[2] += tmp[(size_t)add * w * 4 + (size_t)x * 4 + 2] - tmp[(size_t)sub * w * 4 + (size_t)x * 4 + 2];
            }
        }
    }
}

// colorControls(saturation = 1.5): rows computed from the doc's matrix
static void VibrancyBGRA(uint8_t* img, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t* px = img + i * 4;
        float b = px[0], g = px[1], r = px[2];
        float oR = 1.3935f * r - 0.3575f * g - 0.036f * b;
        float oG = -0.1065f * r + 1.1425f * g - 0.036f * b;
        float oB = -0.1065f * r - 0.3575f * g + 1.464f * b;
        px[0] = (uint8_t)std::min(255.0f, std::max(0.0f, oB));
        px[1] = (uint8_t)std::min(255.0f, std::max(0.0f, oG));
        px[2] = (uint8_t)std::min(255.0f, std::max(0.0f, oR));
    }
}

GlassParams GlassParamsFromApp(const Appearance& a, float physScale) {
    GlassParams p;
    p.blur      = a.glassBlur * physScale;
    p.height    = a.glassHeight * physScale;
    p.amount    = a.glassAmount * physScale;
    p.chroma    = a.glassChroma;
    p.highlight = a.glassHighlight;
    p.vibrancy  = a.glassVibrancy;
    D2D1_COLOR_F c = ParseColor(a.bgColor, 1.0f);
    p.tintR = c.r; p.tintG = c.g; p.tintB = c.b;
    p.tintAlpha = std::min(1.0f, a.bgOpacity * 0.62f);
    p.scale = physScale;
    return p;
}

static uint64_t HashMix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}
static uint64_t HashF(uint64_t h, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return HashMix(h, u);
}

uint64_t GlassParamKey(const GlassParams& p, int w, int h,
                       float ccx, float ccy, float rOuter, float rInner,
                       uint64_t seq) {
    uint64_t k = 0x5147c0ffeeULL;
    k = HashF(k, p.blur);      k = HashF(k, p.height);   k = HashF(k, p.amount);
    k = HashMix(k, p.chroma);  k = HashMix(k, p.highlight); k = HashMix(k, p.vibrancy);
    k = HashF(k, p.tintR);     k = HashF(k, p.tintG);    k = HashF(k, p.tintB);
    k = HashF(k, p.tintAlpha); k = HashF(k, p.scale);
    k = HashMix(k, (uint64_t)(uint32_t)w << 32 | (uint32_t)h);
    k = HashF(k, ccx); k = HashF(k, ccy); k = HashF(k, rOuter); k = HashF(k, rInner);
    k = HashMix(k, seq);
    return k;
}

void BuildGlassTexture(uint8_t* dst, const uint8_t* src, int w, int h,
                       float ccx, float ccy, float rOuter, float rInner,
                       const GlassParams& p) {
    if (!dst || !src || w < 8 || h < 8) return;
    size_t bytes = (size_t)w * h * 4;

    static std::vector<uint8_t> s_work, s_tmp;
    if (s_work.size() < bytes) { s_work.resize(bytes); s_tmp.resize(bytes); }
    uint8_t* work = s_work.data();
    uint8_t* tmp  = s_tmp.data();
    memcpy(work, src, bytes);

    if (p.vibrancy) VibrancyBGRA(work, (size_t)w * h);
    GaussianApproxBGRA(work, tmp, w, h, p.blur);

    float H = p.height;
    float A = p.amount;
    bool  doLens = H >= 0.5f && A >= 0.5f;
    float hlW   = std::max(1.5f, 2.5f * p.scale);   // rim highlight width
    float hlA   = 0.5f;
    float isOff = 10.0f * p.scale;                  // top inner shadow offset
    float isSoft= 8.0f * p.scale;
    float isA   = 0.14f;
    float lx = 0.70710678f, ly = 0.70710678f;       // doc Default highlight: 45 deg

    for (int y = 0; y < h; y++) {
        float dy = y + 0.5f - ccy;
        uint8_t* drow = dst + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            float dx = x + 0.5f - ccx;
            float sd = SdAnnulus(dx, dy, rOuter, rInner);
            float fr, fg, fb;

            if (!doLens || -sd >= H) {
                // band interior: passthrough of the blurred content
                const uint8_t* s = work + ((size_t)y * w + x) * 4;
                fr = s[2]; fg = s[1]; fb = s[0];
            } else {
                float sdc = std::min(sd, 0.0f);
                // doc: amount is passed negative so the sample is pulled inward
                float d = circleMap(1.0f - (-sdc) / H) * (-A);
                float gx, gy;
                GradAnnulus(dx, dy, rOuter, rInner, gx, gy);
                float rx = x + 0.5f + d * gx;
                float ry = y + 0.5f + d * gy;
                if (p.chroma) {
                    // doc section 4.2: dispersion is strongest at the corners and
                    // flips sign across diagonal quadrants (deliberately no abs)
                    float di = (dx * dy) / (rOuter * rOuter);
                    float ox = d * gx * di, oy = d * gy * di;
                    float c[4], r = 0, g = 0, b = 0;
                    SampleBilinear(work, w, h, rx + ox,         ry + oy,         c);
                    r += c[2] / 3.5f; g += 0; b += 0;
                    SampleBilinear(work, w, h, rx + ox * (2/3.0f), ry + oy * (2/3.0f), c);
                    r += c[2] / 3.5f; g += c[1] / 7.0f; b += 0;
                    SampleBilinear(work, w, h, rx + ox / 3.0f,  ry + oy / 3.0f,  c);
                    r += c[2] / 3.5f; g += c[1] / 3.5f; b += 0;
                    SampleBilinear(work, w, h, rx,              ry,              c);
                    g += c[1] / 3.5f; b += 0; r += 0;
                    SampleBilinear(work, w, h, rx - ox / 3.0f,  ry - oy / 3.0f,  c);
                    g += c[1] / 3.5f; b += c[0] / 3.0f; r += 0;
                    SampleBilinear(work, w, h, rx - ox * (2/3.0f), ry - oy * (2/3.0f), c);
                    b += c[0] / 3.0f; r += 0; g += 0;
                    SampleBilinear(work, w, h, rx - ox,         ry - oy,         c);
                    r += c[2] / 7.0f; b += c[0] / 3.0f; g += 0;
                    fr = r; fg = g; fb = b;
                } else {
                    float c[4];
                    SampleBilinear(work, w, h, rx, ry, c);
                    fr = c[2]; fg = c[1]; fb = c[0];
                }
            }

            // onDrawSurface: tint membrane (SrcOver)
            float ta = p.tintAlpha;
            fr = fr + (p.tintR * 255.0f - fr) * ta;
            fg = fg + (p.tintG * 255.0f - fg) * ta;
            fb = fb + (p.tintB * 255.0f - fb) * ta;

            // rim highlight (Plus blend), doc Default style at 45 deg
            if (p.highlight && sd < 0.0f && sd > -hlW) {
                float gx, gy;
                GradAnnulus(dx, dy, rOuter, rInner, gx, gy);
                float lit = fabsf(gx * lx + gy * ly);
                float mask = 1.0f + sd / hlW;
                float add = lit * mask * hlA * 255.0f;
                fr = std::min(255.0f, fr + add);
                fg = std::min(255.0f, fg + add);
                fb = std::min(255.0f, fb + add);
            }

            // top inner shadow: dark where the shape shifted up leaves the glass
            if (sd < 0.0f) {
                float sdUp = SdAnnulus(dx, dy - isOff, rOuter, rInner);
                float m = std::min(1.0f, std::max(0.0f, sdUp / isSoft)) * isA;
                fr *= 1.0f - m; fg *= 1.0f - m; fb *= 1.0f - m;
            }

            uint8_t* o = drow + (size_t)x * 4;
            o[0] = (uint8_t)std::min(255.0f, std::max(0.0f, fb));
            o[1] = (uint8_t)std::min(255.0f, std::max(0.0f, fg));
            o[2] = (uint8_t)std::min(255.0f, std::max(0.0f, fr));
            o[3] = 255;
        }
    }
}
