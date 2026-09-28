#pragma once
#include "stdafx.h"
#include "config.h"

// Liquid glass theme (bgMode 2): turns a raw screen snapshot into a finished
// glass texture. Pipeline mirrors Kyant0/AndroidLiquidGlass (Apache 2.0):
//   vibrancy (saturation x1.5) -> gaussian blur -> edge refraction (lens,
//   circleMap profile on the annulus SDF) -> optional 7-tap chromatic
//   dispersion -> tint membrane -> rim highlight -> top inner shadow.
// The doc's rounded-rect SDF specializes to circles here: the wheel's glass
// shape is the annulus between the outer rim and the center dead-zone hole,
// i.e. max(outer circle SDF, hole circle SDF).

struct GlassParams {
    float blur        = 5.0f;   // gaussian sigma, physical px
    float height      = 22.0f;  // refraction band depth H, physical px
    float amount      = 44.0f;  // refraction strength A, physical px
    bool  chroma      = true;
    bool  highlight   = true;
    bool  vibrancy    = true;
    float tintR = 0, tintG = 0, tintB = 0;
    float tintAlpha   = 0.5f;
    float scale       = 1.0f;   // physical px per logical px (for fixed widths)
};

GlassParams GlassParamsFromApp(const Appearance& a, float physScale);

// Stable hash of everything that changes the produced texture.
uint64_t GlassParamKey(const GlassParams& p, int w, int h,
                       float ccx, float ccy, float rOuter, float rInner,
                       uint64_t seq);

// dst and src are both w*h*4 opaque BGRA. (ccx, ccy) is the glass center in
// bitmap coordinates; the glass fills the annulus rInner..rOuter.
void BuildGlassTexture(uint8_t* dst, const uint8_t* src, int w, int h,
                       float ccx, float ccy, float rOuter, float rInner,
                       const GlassParams& p);

// GPU-first path (D3D11), same contract as BuildGlassTexture. Tries a hardware
// device once per process; returns false on any failure so the caller falls
// back to the CPU implementation above.
bool GlassGpuAvailable();
bool BuildGlassTextureGPU(uint8_t* dst, const uint8_t* src, int w, int h,
                          float ccx, float ccy, float rOuter, float rInner,
                          const GlassParams& p);
