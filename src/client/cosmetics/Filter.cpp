// Camera filters and screenshots (client build, docs/spec/cosmetics.md §3/§7): a pixel-shader pass
// on the swap chain image before it is presented, through hooks on IDXGISwapChain::Present and
// ID3D12CommandQueue::ExecuteCommandLists (to know the game's direct queue). Every system DLL is
// loaded at run time, so the mod needs no extra import libraries.
#ifdef LP_CLIENT

// The Direct3D headers come first: LeviLamina's predefine header declares placeholders for a few
// D3D12 structs unless they are already known (the macros below tell it they are).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wincodec.h>

#define D3D12_RAYTRACING_GEOMETRY_DESC   D3D12_RAYTRACING_GEOMETRY_DESC
#define D3D12_FEATURE_DATA_ARCHITECTURE  D3D12_FEATURE_DATA_ARCHITECTURE
#define D3D12_FEATURE_DATA_D3D12_OPTIONS D3D12_FEATURE_DATA_D3D12_OPTIONS

#include "client/cosmetics/Filter.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mod/Log.h"

namespace lp::client::cosmetics::filter {

// ---- presets ---------------------------------------------------------------------------------------

namespace {

constexpr char const* kNames[kCount]{
    "none",       "grayscale",  "sepia",      "desaturate",  "oversaturate", "cool",          "warm",
    "conjugate",  "redfocus",   "greenfocus", "bluefocus",   "yellowfocus",  "creeper",       "enderman",
    "bits",       "monochrome", "deuteranopia", "protanopia", "tritanopia",  "achromatopsia",
};

Params matrix(std::array<float, 3> r, std::array<float, 3> g, std::array<float, 3> b) {
    Params p;
    p.rows = {{{r[0], r[1], r[2], 0}, {g[0], g[1], g[2], 0}, {b[0], b[1], b[2], 0}}};
    return p;
}

/// Saturation matrix: s = 0 grey, 1 unchanged, > 1 more saturated (Rec. 601 luma).
Params saturation(float s) {
    constexpr float lr = 0.299f, lg = 0.587f, lb = 0.114f;
    float           k = 1 - s;
    return matrix({k * lr + s, k * lg, k * lb}, {k * lr, k * lg + s, k * lb}, {k * lr, k * lg, k * lb + s});
}

Params focus(float hue) {
    Params p;
    p.mode = 1;
    p.hue  = hue;
    return p;
}

} // namespace

char const* name(int index) { return index >= 0 && index < kCount ? kNames[index] : "none"; }

std::optional<Params> preset(int index) {
    switch (index) {
    case 1: return saturation(0);
    case 2: return matrix({0.393f, 0.769f, 0.189f}, {0.349f, 0.686f, 0.168f}, {0.272f, 0.534f, 0.131f});
    case 3: return saturation(0.4f);
    case 4: return saturation(1.9f);
    case 5: return matrix({0.85f, 0, 0}, {0, 0.95f, 0.05f}, {0, 0.05f, 1.15f});
    case 6: return matrix({1.15f, 0.05f, 0}, {0, 1.0f, 0}, {0, 0, 0.82f});
    case 7: return matrix({0, 1, 1}, {1, 0, 1}, {1, 1, 0});
    case 8: return focus(0);
    case 9: return focus(120);
    case 10: return focus(225);
    case 11: return focus(55);
    case 12: { // Creeper-Vision: green monochrome in big pixels
        auto p   = matrix({0.1f, 0.15f, 0.05f}, {0.35f, 0.55f, 0.15f}, {0.05f, 0.1f, 0.05f});
        p.mosaic = 4;
        p.levels = 12;
        return p;
    }
    case 13: { // Enderman-Vision: purple-black, pixelated
        auto p   = matrix({0.6f, 0.2f, 0.2f}, {0, 0, 0}, {0.2f, 0.2f, 0.2f});
        p.mosaic = 4;
        p.levels = 16;
        return p;
    }
    case 14: {
        Params p;
        p.mosaic = 4;
        p.levels = 16;
        return p;
    }
    case 15: {
        Params p;
        p.mode = 2;
        return p;
    }
    // Colour-blindness simulation (Machado, Oliveira and Fernandes 2009, severity 1).
    case 16: return matrix({0.367322f, 0.860646f, -0.227968f}, {0.280085f, 0.672501f, 0.047413f}, {-0.011820f, 0.042940f, 0.968881f});
    case 17: return matrix({0.152286f, 1.052583f, -0.204868f}, {0.114503f, 0.786281f, 0.099216f}, {-0.003882f, -0.048116f, 1.051998f});
    case 18: return matrix({1.255528f, -0.076749f, -0.178779f}, {-0.078411f, 0.930809f, 0.147602f}, {0.004733f, 0.691367f, 0.303900f});
    case 19: return saturation(0);
    default: return std::nullopt;
    }
}

// ---- state ------------------------------------------------------------------------------------------

namespace {

struct Capture {
    std::filesystem::path                              path;
    std::function<void(bool, std::string const&)>      done;
};

std::mutex               gMutex;
std::optional<Params>    gParams;
std::optional<Capture>   gCapture;
std::atomic<bool>        gInstalled{false};
std::atomic<bool>        gAvailable{false};
std::atomic<bool>        gBroken{false};

using PresentFn  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, DXGI_PRESENT_PARAMETERS const*);
using ExecFn     = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

void*      gPresentTarget  = nullptr;
void*      gPresent1Target = nullptr;
void*      gExecTarget     = nullptr;
PresentFn  gPresentOrig    = nullptr;
Present1Fn gPresent1Orig   = nullptr;
ExecFn     gExecOrig       = nullptr;

std::atomic<ID3D12CommandQueue*> gQueue{nullptr}; ///< last direct queue the game executed on

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// ---- shader -----------------------------------------------------------------------------------------

constexpr char kShader[] = R"(
cbuffer P : register(b0) { float4 r0; float4 r1; float4 r2; float4 opts; };
Texture2D src : register(t0);
struct V { float4 pos : SV_Position; };
V vs(uint id : SV_VertexID) {
    V o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float3 hue2(float3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float h = 0;
    if (d > 1e-5) {
        if (mx == c.r) h = fmod((c.g - c.b) / d + 6, 6);
        else if (mx == c.g) h = (c.b - c.r) / d + 2;
        else h = (c.r - c.g) / d + 4;
    }
    return float3(h * 60, mx > 0 ? d / mx : 0, mx);
}
float4 ps(V i) : SV_Target {
    int2 px = int2(i.pos.xy);
    float3 c;
    if (opts.y > 1) {
        int m = (int)opts.y; int2 b = (px / m) * m; c = 0;
        for (int y = 0; y < m; y++) for (int x = 0; x < m; x++) c += src.Load(int3(b + int2(x, y), 0)).rgb;
        c /= (float)(m * m);
    } else c = src.Load(int3(px, 0)).rgb;
    float l = dot(c, float3(0.299, 0.587, 0.114));
    float3 o;
    if (opts.x == 1) {
        float3 h = hue2(c);
        float dh = abs(h.x - opts.w); dh = min(dh, 360 - dh);
        float w = saturate(1 - dh / 35) * saturate(h.y * 3);
        o = lerp(l.xxx, c, w);
    } else if (opts.x == 2) {
        o = (l > 0.5 ? 1 : 0).xxx;
    } else {
        o = float3(dot(r0.xyz, c) + r0.w, dot(r1.xyz, c) + r1.w, dot(r2.xyz, c) + r2.w);
    }
    if (opts.z > 1) o = floor(saturate(o) * (opts.z - 1) + 0.5) / (opts.z - 1);
    return float4(saturate(o), 1);
}
)";

// ---- renderer ---------------------------------------------------------------------------------------

struct Renderer {
    ID3D12Device*              device   = nullptr;
    ID3D12RootSignature*       root     = nullptr;
    ID3D12PipelineState*       pso      = nullptr;
    DXGI_FORMAT                psoFormat = DXGI_FORMAT_UNKNOWN;
    ID3D12DescriptorHeap*      rtvHeap  = nullptr;
    ID3D12DescriptorHeap*      srvHeap  = nullptr;
    ID3D12Resource*            copy     = nullptr;
    UINT                       copyW = 0, copyH = 0;
    DXGI_FORMAT                copyFormat = DXGI_FORMAT_UNKNOWN;
    ID3D12Resource*            readback = nullptr;
    UINT64                     readbackSize = 0;
    std::vector<ID3D12CommandAllocator*> allocators;
    std::vector<UINT64>        allocatorFence;
    ID3D12GraphicsCommandList* list  = nullptr;
    ID3D12Fence*               fence = nullptr;
    UINT64                     fenceValue = 0;
    HANDLE                     event = nullptr;
    void*                      vsBlob = nullptr; ///< ID3DBlob*
    void*                      psBlob = nullptr;

    void waitFor(UINT64 value) {
        if (fence->GetCompletedValue() >= value) return;
        fence->SetEventOnCompletion(value, event);
        WaitForSingleObject(event, 2000);
    }

    void reset() {
        if (fence && fenceValue) waitFor(fenceValue);
        for (auto*& a : allocators) release(a);
        allocators.clear();
        allocatorFence.clear();
        release(list);
        release(copy);
        release(readback);
        release(rtvHeap);
        release(srvHeap);
        release(pso);
        release(root);
        release(fence);
        if (event) CloseHandle(event);
        event = nullptr;
        release(device);
        psoFormat = copyFormat = DXGI_FORMAT_UNKNOWN;
        copyW = copyH = 0;
        readbackSize = 0;
        fenceValue = 0;
    }
} gR;

bool compileShaders() {
    if (gR.vsBlob && gR.psBlob) return true;
    HMODULE mod = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!mod) return false;
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(mod, "D3DCompile"));
    if (!compile) return false;
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
    if (FAILED(compile(kShader, sizeof(kShader) - 1, "lepton_filter", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err))) {
        if (err) log().error("cosmetics: filter vs: {}", static_cast<char const*>(err->GetBufferPointer()));
        release(err);
        return false;
    }
    if (FAILED(compile(kShader, sizeof(kShader) - 1, "lepton_filter", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err))) {
        if (err) log().error("cosmetics: filter ps: {}", static_cast<char const*>(err->GetBufferPointer()));
        release(err);
        release(vs);
        return false;
    }
    gR.vsBlob = vs;
    gR.psBlob = ps;
    return true;
}

DXGI_FORMAT typed(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool setup(IDXGISwapChain3* sc, UINT count, UINT w, UINT h, DXGI_FORMAT format) {
    if (!gR.device) {
        if (FAILED(sc->GetDevice(IID_PPV_ARGS(&gR.device)))) return false;
        if (FAILED(gR.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gR.fence)))) return false;
        gR.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    auto* dev = gR.device;
    if (!gR.root) {
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 16;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc{2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        HMODULE mod = GetModuleHandleW(L"d3d12.dll");
        auto serialize = mod ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(GetProcAddress(mod, "D3D12SerializeRootSignature")) : nullptr;
        if (!serialize) return false;
        ID3DBlob *blob = nullptr, *err = nullptr;
        if (FAILED(serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
            release(err);
            return false;
        }
        HRESULT hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gR.root));
        release(blob);
        if (FAILED(hr)) return false;
    }
    DXGI_FORMAT rtFormat = typed(format);
    if (!gR.pso || gR.psoFormat != rtFormat) {
        release(gR.pso);
        if (!compileShaders()) return false;
        auto* vs = static_cast<ID3DBlob*>(gR.vsBlob);
        auto* ps = static_cast<ID3DBlob*>(gR.psBlob);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature                        = gR.root;
        d.VS                                    = {vs->GetBufferPointer(), vs->GetBufferSize()};
        d.PS                                    = {ps->GetBufferPointer(), ps->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
        d.SampleMask                            = UINT_MAX;
        d.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable       = TRUE;
        d.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets                      = 1;
        d.RTVFormats[0]                         = rtFormat;
        d.SampleDesc.Count                      = 1;
        if (FAILED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&gR.pso)))) return false;
        gR.psoFormat = rtFormat;
    }
    if (!gR.rtvHeap || gR.allocators.size() != count) {
        if (gR.fenceValue) gR.waitFor(gR.fenceValue);
        release(gR.rtvHeap);
        for (auto*& a : gR.allocators) release(a);
        gR.allocators.assign(count, nullptr);
        gR.allocatorFence.assign(count, 0);
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, count, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gR.rtvHeap)))) return false;
        for (UINT i = 0; i < count; ++i)
            if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gR.allocators[i])))) return false;
        if (!gR.list) {
            if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gR.allocators[0], nullptr, IID_PPV_ARGS(&gR.list)))) return false;
            gR.list->Close();
        }
    }
    if (!gR.srvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gR.srvHeap)))) return false;
    }
    if (!gR.copy || gR.copyW != w || gR.copyH != h || gR.copyFormat != format) {
        if (gR.fenceValue) gR.waitFor(gR.fenceValue);
        release(gR.copy);
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC   rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = format;
        rd.SampleDesc.Count = 1;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gR.copy))))
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                  = rtFormat;
        sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels     = 1;
        dev->CreateShaderResourceView(gR.copy, &sd, gR.srvHeap->GetCPUDescriptorHandleForHeapStart());
        gR.copyW = w;
        gR.copyH = h;
        gR.copyFormat = format;
    }
    return true;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter  = to;
    return b;
}

// ---- PNG output (WIC) -------------------------------------------------------------------------------

bool savePng(std::filesystem::path const& path, std::vector<uint8_t> const& bgra, UINT w, UINT h) {
    static GUID const kClsidFactory{0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
    static GUID const kPng{0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
    static GUID const kBgra{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    if (!ole) return false;
    using CoInit   = HRESULT(WINAPI*)(LPVOID, DWORD);
    using CoCreate = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    auto coInit    = reinterpret_cast<CoInit>(GetProcAddress(ole, "CoInitializeEx"));
    auto coCreate  = reinterpret_cast<CoCreate>(GetProcAddress(ole, "CoCreateInstance"));
    if (!coInit || !coCreate) return false;
    coInit(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory*    factory = nullptr;
    IWICStream*            stream  = nullptr;
    IWICBitmapEncoder*     encoder = nullptr;
    IWICBitmapFrameEncode* frame   = nullptr;
    bool                   ok      = false;
    std::error_code        ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (SUCCEEDED(coCreate(kClsidFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(kPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = kBgra;
        if (SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == kBgra
            && SUCCEEDED(frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data())))
            && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()))
            ok = true;
    }
    release(frame);
    release(encoder);
    release(stream);
    release(factory);
    return ok;
}

/// Converts one read-back row to BGRA8.
void toBgra(DXGI_FORMAT f, uint8_t const* src, uint8_t* dst, UINT w) {
    for (UINT x = 0; x < w; ++x) {
        uint8_t r, g, b;
        switch (f) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            b = src[x * 4], g = src[x * 4 + 1], r = src[x * 4 + 2];
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: {
            uint32_t v;
            memcpy(&v, src + x * 4, 4);
            r = static_cast<uint8_t>((v & 1023) >> 2), g = static_cast<uint8_t>(((v >> 10) & 1023) >> 2), b = static_cast<uint8_t>(((v >> 20) & 1023) >> 2);
            break;
        }
        default:
            r = src[x * 4], g = src[x * 4 + 1], b = src[x * 4 + 2];
            break;
        }
        dst[x * 4] = b, dst[x * 4 + 1] = g, dst[x * 4 + 2] = r, dst[x * 4 + 3] = 255;
    }
}

void process(IDXGISwapChain* raw) {
    std::optional<Params>  params;
    std::optional<Capture> capture;
    {
        std::lock_guard lock(gMutex);
        params = gParams;
        if (gCapture) capture = std::move(gCapture), gCapture.reset();
    }
    if (!params && !capture) return;
    auto fail = [&](char const* why) {
        if (capture && capture->done) {
            auto done = capture->done;
            std::thread([done] { done(false, {}); }).detach();
        }
        if (!gBroken.exchange(true)) log().warn("cosmetics: camera filter unavailable ({})", why);
    };
    ID3D12CommandQueue* queue = gQueue.load();
    if (!queue) return fail("no direct queue seen");
    IDXGISwapChain3* sc = nullptr;
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&sc)))) return fail("not a DXGI 1.4 swap chain");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    sc->GetDesc1(&desc);
    if (!setup(sc, desc.BufferCount, desc.Width, desc.Height, desc.Format)) {
        sc->Release();
        return fail("Direct3D 12 setup failed");
    }
    gAvailable = true;
    UINT            idx = sc->GetCurrentBackBufferIndex();
    ID3D12Resource* bb  = nullptr;
    if (idx >= gR.allocators.size() || FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&bb)))) {
        sc->Release();
        return fail("no back buffer");
    }
    gR.waitFor(gR.allocatorFence[idx]);
    auto* alloc = gR.allocators[idx];
    alloc->Reset();
    auto* cl = gR.list;
    cl->Reset(alloc, nullptr);

    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PRESENT;
    if (params) {
        D3D12_RESOURCE_BARRIER b1[] = {transition(bb, state, D3D12_RESOURCE_STATE_COPY_SOURCE)};
        cl->ResourceBarrier(1, b1);
        cl->CopyResource(gR.copy, bb);
        D3D12_RESOURCE_BARRIER b2[] = {
            transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            transition(gR.copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        };
        cl->ResourceBarrier(2, b2);
        state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = gR.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(idx) * gR.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format        = gR.psoFormat;
        rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        gR.device->CreateRenderTargetView(bb, &rd, rtv);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
        D3D12_RECT     sr{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sr);
        cl->SetGraphicsRootSignature(gR.root);
        cl->SetPipelineState(gR.pso);
        ID3D12DescriptorHeap* heaps[] = {gR.srvHeap};
        cl->SetDescriptorHeaps(1, heaps);
        float k[16];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) k[r * 4 + c] = params->rows[r][c];
        k[12] = static_cast<float>(params->mode);
        k[13] = params->mosaic;
        k[14] = params->levels;
        k[15] = params->hue;
        cl->SetGraphicsRoot32BitConstants(0, 16, k, 0);
        cl->SetGraphicsRootDescriptorTable(1, gR.srvHeap->GetGPUDescriptorHandleForHeapStart());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER b3[] = {transition(gR.copy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
        cl->ResourceBarrier(1, b3);
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64                             total = 0;
    if (capture) {
        D3D12_RESOURCE_DESC bd = bb->GetDesc();
        gR.device->GetCopyableFootprints(&bd, 0, 1, 0, &fp, nullptr, nullptr, &total);
        if (!gR.readback || gR.readbackSize < total) {
            release(gR.readback);
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC   rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = total;
            rd.Height           = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels        = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (SUCCEEDED(gR.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gR.readback))))
                gR.readbackSize = total;
        }
        if (gR.readback) {
            D3D12_RESOURCE_BARRIER b[] = {transition(bb, state, D3D12_RESOURCE_STATE_COPY_SOURCE)};
            cl->ResourceBarrier(1, b);
            state = D3D12_RESOURCE_STATE_COPY_SOURCE;
            D3D12_TEXTURE_COPY_LOCATION dst{gR.readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
            dst.PlacedFootprint = fp;
            D3D12_TEXTURE_COPY_LOCATION src{bb, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            src.SubresourceIndex = 0;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
    }
    if (state != D3D12_RESOURCE_STATE_PRESENT) {
        D3D12_RESOURCE_BARRIER b[] = {transition(bb, state, D3D12_RESOURCE_STATE_PRESENT)};
        cl->ResourceBarrier(1, b);
    }
    cl->Close();
    ID3D12CommandList* lists[] = {cl};
    gExecOrig(queue, 1, lists);
    queue->Signal(gR.fence, ++gR.fenceValue);
    gR.allocatorFence[idx] = gR.fenceValue;

    if (capture) {
        if (!gR.readback) {
            fail("no read-back buffer");
        } else {
            gR.waitFor(gR.fenceValue);
            std::vector<uint8_t> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
            void*                mapped = nullptr;
            D3D12_RANGE          range{0, static_cast<SIZE_T>(total)};
            if (SUCCEEDED(gR.readback->Map(0, &range, &mapped))) {
                for (UINT y = 0; y < desc.Height; ++y)
                    toBgra(desc.Format, static_cast<uint8_t const*>(mapped) + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                           pixels.data() + static_cast<size_t>(y) * desc.Width * 4, desc.Width);
                D3D12_RANGE none{0, 0};
                gR.readback->Unmap(0, &none);
                UINT w = desc.Width, h = desc.Height;
                std::thread([c = std::move(*capture), px = std::move(pixels), w, h] {
                    bool ok = savePng(c.path, px, w, h);
                    if (c.done) c.done(ok, c.path.string());
                }).detach();
            } else {
                fail("map failed");
            }
        }
    }
    bb->Release();
    sc->Release();
}

HRESULT STDMETHODCALLTYPE presentDetour(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) {
        try {
            process(sc);
        } catch (...) {}
    }
    return gPresentOrig(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE present1Detour(IDXGISwapChain1* sc, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* p) {
    if (!(flags & DXGI_PRESENT_TEST)) {
        try {
            process(sc);
        } catch (...) {}
    }
    return gPresent1Orig(sc, sync, flags, p);
}

void STDMETHODCALLTYPE execDetour(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
    if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) gQueue = q;
    gExecOrig(q, n, lists);
}

/// Reads the vtables of a throw-away device / queue / swap chain.
bool findTargets() {
    HMODULE d3d = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!d3d || !dxgi) return false;
    auto createDevice  = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d, "D3D12CreateDevice"));
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory = reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (!createDevice || !createFactory) return false;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"lepton-dx", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    if (!hwnd) return false;
    ID3D12Device*       dev     = nullptr;
    ID3D12CommandQueue* queue   = nullptr;
    IDXGIFactory4*      factory = nullptr;
    IDXGISwapChain1*    sc      = nullptr;
    bool                ok      = false;
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    DXGI_SWAP_CHAIN_DESC1    sd{};
    sd.Width            = 64;
    sd.Height           = 64;
    sd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (SUCCEEDED(createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))) && SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))
        && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc))) {
        void** scv      = *reinterpret_cast<void***>(sc);
        void** qv       = *reinterpret_cast<void***>(queue);
        gPresentTarget  = scv[8];
        gPresent1Target = scv[22];
        gExecTarget     = qv[10];
        ok              = true;
    }
    release(sc);
    release(factory);
    release(queue);
    release(dev);
    DestroyWindow(hwnd);
    return ok;
}

} // namespace

void set(std::optional<Params> params) {
    std::lock_guard lock(gMutex);
    gParams = params;
}

bool requestCapture(std::filesystem::path path, std::function<void(bool, std::string const&)> done) {
    if (!gInstalled || gBroken) return false;
    std::lock_guard lock(gMutex);
    gCapture = Capture{std::move(path), std::move(done)};
    return true;
}

bool available() { return gAvailable; }

void ensureInstalled() {
    if (gInstalled.exchange(true)) return;
    if (!findTargets()) {
        gBroken = true;
        log().warn("cosmetics: Direct3D 12 not available, camera filters disabled");
        return;
    }
    using ll::memory::FuncPtr;
    ll::memory::hook(gExecTarget, reinterpret_cast<FuncPtr>(&execDetour), reinterpret_cast<FuncPtr*>(&gExecOrig), ll::memory::HookPriority::Normal);
    ll::memory::hook(gPresentTarget, reinterpret_cast<FuncPtr>(&presentDetour), reinterpret_cast<FuncPtr*>(&gPresentOrig), ll::memory::HookPriority::Normal);
    ll::memory::hook(gPresent1Target, reinterpret_cast<FuncPtr>(&present1Detour), reinterpret_cast<FuncPtr*>(&gPresent1Orig), ll::memory::HookPriority::Normal);
    log().info("cosmetics: camera filter hooks installed");
}

void shutdown() {
    if (!gInstalled || gBroken) return;
    using ll::memory::FuncPtr;
    ll::memory::unhook(gPresent1Target, reinterpret_cast<FuncPtr>(&present1Detour));
    ll::memory::unhook(gPresentTarget, reinterpret_cast<FuncPtr>(&presentDetour));
    ll::memory::unhook(gExecTarget, reinterpret_cast<FuncPtr>(&execDetour));
    gR.reset();
    gInstalled = false;
}

} // namespace lp::client::cosmetics::filter

#endif // LP_CLIENT
