#include "stdafx.h"
#include "glass.h"
#include <d3d11.h>

// GPU implementation of BuildGlassTexture (bgMode 2), used before the CPU path.
// Same math as glass.cpp, expressed as D3D11 draws:
//   pass 1  vibrancy (saturation x1.5) folded into the first box-blur H pass
//   pass 2+ 3x separable box blur (H/V alternating, ping-pong targets)
//   final   refraction lens + 7-tap dispersion + tint + rim highlight +
//           top inner shadow in one pixel shader, then staging readback.
// Everything is created lazily on first use; any failure (no hardware device,
// d3dcompiler missing, shader compile error, device removed) flips the
// singleton off for the rest of the session and the caller falls back to CPU.

namespace {

// matches cbuffer GlassCb in the HLSL below
struct alignas(16) GlassCb {
    float texSize[4];   // w, h, 1/w, 1/h
    float geom[4];      // ccx, ccy, rOuter, rInner
    float lens[4];      // H, A, doLens, chroma
    float hl[4];        // hlW, hlA, highlightOn, -
    float sh[4];        // isOff, isSoft, isA, -
    float tint[4];      // tintR, tintG, tintB, tintAlpha
    float blur[4];      // r, vibrancy, dirX, dirY
};

const char kHlsl[] = R"HLSL(
Texture2D g_tex : register(t0);
SamplerState g_samPoint  : register(s0);
SamplerState g_samLinear : register(s1);

cbuffer GlassCb : register(b0) {
    float4 g_texSize;   // w, h, 1/w, 1/h
    float4 g_geom;      // ccx, ccy, rOuter, rInner
    float4 g_lens;      // H, A, doLens, chroma
    float4 g_hl;        // hlW, hlA, highlightOn, -
    float4 g_sh;        // isOff, isSoft, isA, -
    float4 g_tint;      // tintR, tintG, tintB, tintAlpha
    float4 g_blur;      // r, vibrancy, dirX, dirY
};

struct VsOut { float4 pos : SV_POSITION; };

VsOut VsMain(uint id : SV_VertexID) {
    VsOut o;
    float2 xy = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);
    o.pos = float4(xy, 0, 1);
    return o;
}

// colorControls(saturation = 1.5), same matrix as VibrancyBGRA
float3 Vibrancy(float3 c) {
    float r =  1.3935 * c.r - 0.3575 * c.g - 0.036  * c.b;
    float g = -0.1065 * c.r + 1.1425 * c.g - 0.036  * c.b;
    float b = -0.1065 * c.r - 0.3575 * c.g + 1.464  * c.b;
    return clamp(float3(r, g, b), 0.0, 1.0);
}

// separable box blur; integer offsets land on exact texel centers so the
// point sampler reads the same pixels as the CPU window, and clamp addressing
// reproduces the CPU's edge clamping. pass 1 also applies vibrancy (a linear
// matrix, so it commutes with the blur average).
float4 PsBlur(float4 pos : SV_POSITION) : SV_Target {
    float2 uv = pos.xy * g_texSize.zw;
    float3 acc = 0;
    for (int i = -(int)g_blur.x; i <= (int)g_blur.x; i++) {
        float2 suv = uv + float2(i * g_blur.z * g_texSize.z, i * g_blur.w * g_texSize.w);
        float3 c = g_tex.SampleLevel(g_samPoint, suv, 0).rgb;
        if (g_blur.y > 0.5) c = Vibrancy(c);
        acc += c;
    }
    float win = 2.0 * g_blur.x + 1.0;
    return float4(acc / win, 1.0);
}

float SdAnnulus(float2 d, float rO, float rI) {
    float dist = length(d);
    return max(dist - rO, rI - dist);
}
float2 GradAnnulus(float2 d, float rO, float rI) {
    float dist = length(d);
    float s = (dist - rO >= rI - dist) ? 1.0 : -1.0;
    return d * (s / max(dist, 1e-4));
}
float CircleMap(float x) { return 1.0 - sqrt(max(0.0, 1.0 - x * x)); }

float4 PsComposite(float4 pos : SV_POSITION) : SV_Target {
    float2 p = pos.xy;
    float2 d = p - g_geom.xy;
    float rO = g_geom.z, rI = g_geom.w;
    float sd = SdAnnulus(d, rO, rI);
    float H = g_lens.x, A = g_lens.y;
    float3 fr;
    if (g_lens.z < 0.5 || -sd >= H) {
        // band interior: passthrough of the blurred content
        fr = g_tex.SampleLevel(g_samLinear, p * g_texSize.zw, 0).rgb;
    } else {
        float sdc = min(sd, 0.0);
        float dd = CircleMap(1.0 - (-sdc) / H) * (-A);
        float2 g = GradAnnulus(d, rO, rI);
        float2 rp = p + dd * g;
        if (g_lens.w > 0.5) {
            // 7-tap spectral dispersion, weights as in glass.cpp
            float di = (d.x * d.y) / (rO * rO);
            float2 ox = dd * g * di;
            float r = 0, gg = 0, b = 0;
            float3 c;
            c = g_tex.SampleLevel(g_samLinear, (rp + ox) * g_texSize.zw, 0).rgb;
            r += c.r / 3.5;
            c = g_tex.SampleLevel(g_samLinear, (rp + ox * (2.0 / 3.0)) * g_texSize.zw, 0).rgb;
            r += c.r / 3.5; gg += c.g / 7.0;
            c = g_tex.SampleLevel(g_samLinear, (rp + ox / 3.0) * g_texSize.zw, 0).rgb;
            r += c.r / 3.5; gg += c.g / 3.5;
            c = g_tex.SampleLevel(g_samLinear, rp * g_texSize.zw, 0).rgb;
            gg += c.g / 3.5;
            c = g_tex.SampleLevel(g_samLinear, (rp - ox / 3.0) * g_texSize.zw, 0).rgb;
            gg += c.g / 3.5; b += c.b / 3.0;
            c = g_tex.SampleLevel(g_samLinear, (rp - ox * (2.0 / 3.0)) * g_texSize.zw, 0).rgb;
            b += c.b / 3.0;
            c = g_tex.SampleLevel(g_samLinear, (rp - ox) * g_texSize.zw, 0).rgb;
            b += c.b / 3.0; r += c.r / 7.0;
            fr = float3(r, gg, b);
        } else {
            fr = g_tex.SampleLevel(g_samLinear, rp * g_texSize.zw, 0).rgb;
        }
    }

    // tint membrane (SrcOver), content and tint both normalized 0..1
    fr = fr + (g_tint.rgb - fr) * g_tint.w;

    // rim highlight (Plus blend), 45 deg, per-channel clamp like std::min
    if (g_hl.z > 0.5 && sd < 0.0 && sd > -g_hl.x) {
        float2 g = GradAnnulus(d, rO, rI);
        float lit = abs(g.x * 0.70710678 + g.y * 0.70710678);
        float add = lit * (1.0 + sd / g_hl.x) * g_hl.y;
        fr = min(fr + add, 1.0);
    }

    // top inner shadow
    if (sd < 0.0) {
        float sdUp = SdAnnulus(float2(d.x, d.y - g_sh.x), rO, rI);
        float m = clamp(sdUp / g_sh.y, 0.0, 1.0) * g_sh.z;
        fr *= 1.0 - m;
    }
    return float4(clamp(fr, 0.0, 1.0), 1.0);
}
)HLSL";

class GlassGpu {
public:
    bool Available() {
        if (!m_tried) { m_tried = true; m_ok = Init(); if (!m_ok) ReleaseAll(); }
        return m_ok;
    }

    // same contract as BuildGlassTexture; returns false on any failure
    bool Build(uint8_t* dst, const uint8_t* src, int w, int h,
               float ccx, float ccy, float rO, float rI, const GlassParams& p) {
        if (!Available() || w < 8 || h < 8) return false;
        if (FAILED(EnsureTextures(w, h))) { Fail(); return false; }

        GlassCb cb{};
        cb.texSize[0] = (float)w; cb.texSize[1] = (float)h;
        cb.texSize[2] = 1.0f / w; cb.texSize[3] = 1.0f / h;
        cb.geom[0] = ccx; cb.geom[1] = ccy; cb.geom[2] = rO; cb.geom[3] = rI;
        float H = p.height, A = p.amount;
        cb.lens[0] = H; cb.lens[1] = A;
        cb.lens[2] = (H >= 0.5f && A >= 0.5f) ? 1.0f : 0.0f;
        cb.lens[3] = p.chroma ? 1.0f : 0.0f;
        cb.hl[0] = std::max(1.5f, 2.5f * p.scale);   // rim highlight width
        cb.hl[1] = 0.5f;
        cb.hl[2] = p.highlight ? 1.0f : 0.0f;
        cb.sh[0] = 10.0f * p.scale;                  // top inner shadow offset
        cb.sh[1] = 8.0f * p.scale;                   // softness
        cb.sh[2] = 0.14f;
        cb.tint[0] = p.tintR; cb.tint[1] = p.tintG; cb.tint[2] = p.tintB;
        cb.tint[3] = std::min(1.0f, p.tintAlpha);
        long rl = std::lround(p.blur);
        float r = (rl < 1) ? 0.0f : std::min(96.0f, (float)rl);
        cb.blur[0] = r;
        m_cbData = cb;
        UploadCb();
        m_ctx->UpdateSubresource(m_texSrc.Get(), 0, nullptr, src, (UINT)w * 4, 0);

        bool blurOn = r >= 1.0f;
        const float dirH[2] = { 1, 0 }, dirV[2] = { 0, 1 };
        ID3D11ShaderResourceView* finalSrv = srvA.Get();
        DrawBlur(rtvA.Get(), srvSrc.Get(), p.vibrancy ? 1.0f : 0.0f, dirH, r);  // optional vibrancy + H
        if (m_ok && blurOn) {
            DrawBlur(rtvB.Get(), srvA.Get(), 0.0f, dirV, r);
            DrawBlur(rtvA.Get(), srvB.Get(), 0.0f, dirH, r);
            DrawBlur(rtvB.Get(), srvA.Get(), 0.0f, dirV, r);
            DrawBlur(rtvA.Get(), srvB.Get(), 0.0f, dirH, r);
            DrawBlur(rtvB.Get(), srvA.Get(), 0.0f, dirV, r);      // 3rd box pass ends in B
            finalSrv = srvB.Get();
        }
        if (!m_ok) return false;
        DrawPass(m_psComp.Get(), rtvOut.Get(), finalSrv);

        m_ctx->CopyResource(m_texStage.Get(), m_texOut.Get());
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(m_ctx->Map(m_texStage.Get(), 0, D3D11_MAP_READ, 0, &ms))) { Fail(); return false; }
        // staging rows can be padded; respect RowPitch
        if (ms.RowPitch == (UINT)w * 4) {
            memcpy(dst, ms.pData, (size_t)w * h * 4);
        } else {
            const uint8_t* s = (const uint8_t*)ms.pData;
            for (int y = 0; y < h; y++)
                memcpy(dst + (size_t)y * w * 4, s + (size_t)y * ms.RowPitch, (size_t)w * 4);
        }
        m_ctx->Unmap(m_texStage.Get(), 0);
        return true;
    }

private:
    bool Init() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     levels, 3, D3D11_SDK_VERSION,
                                     m_dev.GetAddressOf(), nullptr, m_ctx.GetAddressOf())))
            return false;

        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!dc) return false;
        using PFN_D3DCompile_ = HRESULT(WINAPI*)(LPCSTR, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                                 ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                                 ID3DBlob**, ID3DBlob**);
        auto compile = (PFN_D3DCompile_)GetProcAddress(dc, "D3DCompile");
        if (!compile) return false;
        ComPtr<ID3DBlob> vsb, psb, pcb, err;
        if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "glass_gpu.hlsl", nullptr, nullptr,
                           "VsMain", "vs_4_0", 0, 0, vsb.GetAddressOf(), err.GetAddressOf()))) return false;
        if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "glass_gpu.hlsl", nullptr, nullptr,
                           "PsBlur", "ps_4_0", 0, 0, psb.GetAddressOf(), err.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "glass_gpu.hlsl", nullptr, nullptr,
                           "PsComposite", "ps_4_0", 0, 0, pcb.GetAddressOf(), err.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_vs.GetAddressOf()))) return false;
        if (FAILED(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_psBlur.GetAddressOf()))) return false;
        if (FAILED(m_dev->CreatePixelShader(pcb->GetBufferPointer(), pcb->GetBufferSize(), nullptr, m_psComp.GetAddressOf()))) return false;

        D3D11_SAMPLER_DESC sd{};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(m_dev->CreateSamplerState(&sd, m_samPoint.GetAddressOf()))) return false;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (FAILED(m_dev->CreateSamplerState(&sd, m_samLinear.GetAddressOf()))) return false;

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(GlassCb);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, m_cb.GetAddressOf()))) return false;

        // the fullscreen triangle from SV_VertexID is wound CCW; keep it
        // independent of the default back-face cull
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        if (FAILED(m_dev->CreateRasterizerState(&rd, m_rs.GetAddressOf()))) return false;

        m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11SamplerState* sams[2] = { m_samPoint.Get(), m_samLinear.Get() };
        m_ctx->PSSetSamplers(0, 2, sams);
        m_ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_ctx->RSSetState(m_rs.Get());
        m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
        return true;
    }

    HRESULT EnsureTextures(int w, int h) {
        if (m_texSrc && m_w == w && m_h == h) return S_OK;
        ReleaseTextures();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w; td.Height = (UINT)h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;

        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, m_texSrc.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        hr = m_dev->CreateTexture2D(&td, nullptr, m_texA.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateTexture2D(&td, nullptr, m_texB.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateTexture2D(&td, nullptr, m_texOut.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = m_dev->CreateTexture2D(&td, nullptr, m_texStage.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;

        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = td.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = m_dev->CreateShaderResourceView(m_texSrc.Get(), &sv, srvSrc.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateShaderResourceView(m_texA.Get(), &sv, srvA.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateShaderResourceView(m_texB.Get(), &sv, srvB.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateRenderTargetView(m_texA.Get(), nullptr, rtvA.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateRenderTargetView(m_texB.Get(), nullptr, rtvB.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        hr = m_dev->CreateRenderTargetView(m_texOut.Get(), nullptr, rtvOut.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;

        m_w = w; m_h = h;
        return S_OK;
    }

    void DrawPass(ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* srv) {
        ID3D11ShaderResourceView* none = nullptr;
        m_ctx->PSSetShaderResources(0, 1, &none);
        m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{ 0, 0, (FLOAT)m_w, (FLOAT)m_h, 0.0f, 1.0f };
        m_ctx->RSSetViewports(1, &vp);
        m_ctx->PSSetShader(ps, nullptr, 0);
        m_ctx->PSSetShaderResources(0, 1, &srv);
        m_ctx->Draw(3, 0);
    }

    void DrawBlur(ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* srv, float vib, const float* dir, float r) {
        m_cbData.blur[1] = vib;
        m_cbData.blur[2] = dir[0];
        m_cbData.blur[3] = dir[1];
        UploadCb();
        DrawPass(m_psBlur.Get(), rtv, srv);
    }

    // WRITE_DISCARD invalidates the whole buffer, so always upload the full struct
    void UploadCb() {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(m_ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { Fail(); return; }
        memcpy(ms.pData, &m_cbData, sizeof(m_cbData));
        m_ctx->Unmap(m_cb.Get(), 0);
    }

    void Fail() { m_ok = false; ReleaseAll(); }
    void ReleaseTextures() {
        srvSrc.Reset(); srvA.Reset(); srvB.Reset(); rtvA.Reset(); rtvB.Reset(); rtvOut.Reset();
        m_texSrc.Reset(); m_texA.Reset(); m_texB.Reset(); m_texOut.Reset(); m_texStage.Reset();
        m_w = m_h = 0;
    }
    void ReleaseAll() {
        ReleaseTextures();
        m_cb.Reset(); m_samPoint.Reset(); m_samLinear.Reset(); m_rs.Reset();
        m_psComp.Reset(); m_psBlur.Reset(); m_vs.Reset();
        m_ctx.Reset(); m_dev.Reset();
    }

    ComPtr<ID3D11Device> m_dev;
    ComPtr<ID3D11DeviceContext> m_ctx;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_psBlur, m_psComp;
    ComPtr<ID3D11SamplerState> m_samPoint, m_samLinear;
    ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11RasterizerState> m_rs;
    ComPtr<ID3D11Texture2D> m_texSrc, m_texA, m_texB, m_texOut, m_texStage;
    ComPtr<ID3D11ShaderResourceView> srvSrc, srvA, srvB;
    ComPtr<ID3D11RenderTargetView> rtvA, rtvB, rtvOut;
    GlassCb m_cbData{};
    int m_w = 0, m_h = 0;
    bool m_tried = false, m_ok = false;
};

GlassGpu s_gpu;

} // namespace

bool GlassGpuAvailable() {
    return s_gpu.Available();
}

bool BuildGlassTextureGPU(uint8_t* dst, const uint8_t* src, int w, int h,
                          float ccx, float ccy, float rOuter, float rInner,
                          const GlassParams& p) {
    return s_gpu.Build(dst, src, w, h, ccx, ccy, rOuter, rInner, p);
}
