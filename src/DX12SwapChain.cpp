#include "DX12SwapChain.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <dx12/ffx_api_dx12.hpp>
#include <dx12/ffx_api_framegeneration_dx12.hpp>
#include <dxgi1_6.h>
#include <optional>
#include <string_view>

#include <directx/d3dx12.h>

#include "FidelityFX.h"
#include "Streamline.h"
#include "Upscaler.h"

extern bool enbLoaded;

// --- HDR color space conversion shader (compiled at runtime) ---
namespace
{
constexpr const char *kColorSpaceShader = R"_HDR_(
struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};
VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput o;
    o.pos.x = (float)(id == 1 ? 3 : -1);
    o.pos.y = (float)(id == 2 ? -3 : 1);
    o.pos.z = 0;
    o.pos.w = 1;
    o.uv.x = o.pos.x * 0.5 + 0.5;
    o.uv.y = -o.pos.y * 0.5 + 0.5;
    return o;
}
cbuffer ColorSpaceCB : register(b0) {
    float cb_peakNits;
    uint cb_isHDR10;
};
float3 sRGBToLinear(float3 c) {
    float3 sel = step(c, 0.04045);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), sel);
}
static const float3x3 Rec709ToRec2020 = {
    0.6274, 0.3293, 0.0433,
    0.0691, 0.9195, 0.0114,
    0.0164, 0.0880, 0.8956
};
static const float PQ_m1 = 0.1593017578125;
static const float PQ_m2 = 78.84375;
static const float PQ_c1 = 0.8359375;
static const float PQ_c2 = 18.8515625;
static const float PQ_c3 = 18.6875;
float3 LinearToPQ(float3 c, float peakNits) {
    c = max(c / peakNits, 0);
    float3 c1 = pow(c, PQ_m1);
    return pow((PQ_c1 + PQ_c2 * c1) / (1.0 + PQ_c3 * c1), PQ_m2);
}
Texture2D<float4> sourceTex : register(t0);
SamplerState pointSampler : register(s0);
float4 PSMain(VSOutput i) : SV_TARGET {
    float4 color = sourceTex.Sample(pointSampler, i.uv);
    float3 linear = sRGBToLinear(color.rgb);
    [branch] if (cb_isHDR10) {
        float3 rec2020 = mul(Rec709ToRec2020, linear);
        float3 pq = LinearToPQ(rec2020, cb_peakNits);
        return float4(pq, color.a);
    } else {
        return float4(linear, color.a);
    }
}
)_HDR_";
}

// Overlay callbacks resolved from Overlay.dll at init time.
// Using file-scope statics avoids the OBJECT-library multiple-singleton problem:
// whichever DLL creates the DX12SwapChain resolves these once.
namespace
{
OverlayInitCallback s_overlayInitCb = nullptr;
OverlayPresentCallback s_overlayPresentCb = nullptr;
OverlayPollCallback s_overlayPollCb = nullptr;
bool s_overlayCallbacksResolved = false;
bool s_overlayCallbacksMissingLogged = false;

bool ResolveOverlayCallbacks()
{
    if (s_overlayCallbacksResolved)
    {
        return true;
    }

    auto tryResolve = [](HMODULE a_module, const char *a_source) -> bool {
        if (!a_module)
        {
            return false;
        }

        auto initCb = reinterpret_cast<OverlayInitCallback>(GetProcAddress(a_module, "Overlay_OnSwapChainCreated"));
        auto presentCb = reinterpret_cast<OverlayPresentCallback>(GetProcAddress(a_module, "Overlay_OnPresent"));
        auto pollCb = reinterpret_cast<OverlayPollCallback>(GetProcAddress(a_module, "Overlay_OnPollHotkey"));
        if (!initCb || !presentCb || !pollCb)
        {
            return false;
        }

        s_overlayInitCb = initCb;
        s_overlayPresentCb = presentCb;
        s_overlayPollCb = pollCb;
        s_overlayCallbacksResolved = true;
        logger::info("[DX12SwapChain] Overlay callbacks resolved from {}", a_source);
        return true;
    };

    HMODULE currentModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&s_overlayCallbacksResolved), &currentModule) &&
        tryResolve(currentModule, "current module"))
    {
        return true;
    }

    if (tryResolve(GetModuleHandleW(L"NuclearGFX.dll"), "NuclearGFX.dll"))
    {
        return true;
    }

    if (tryResolve(GetModuleHandleW(L"Overlay.dll"), "Overlay.dll"))
    {
        return true;
    }

    if (!s_overlayCallbacksMissingLogged)
    {
        s_overlayCallbacksMissingLogged = true;
        logger::info("[DX12SwapChain] Overlay callbacks not found; retrying until Overlay is loaded");
    }
    return false;
}
} // namespace

namespace
{
std::string FormatHRESULT(HRESULT hr)
{
    return std::format("0x{:08X}", static_cast<std::uint32_t>(hr));
}

enum class PresentTracePhase
{
    kNone,
    kLoading,
    kGameplay
};

const char *GetPresentTracePhaseName(PresentTracePhase phase)
{
    switch (phase)
    {
    case PresentTracePhase::kLoading:
        return "loading";
    case PresentTracePhase::kGameplay:
        return "gameplay";
    default:
        return "none";
    }
}

bool ShouldTracePresentFrame(uint64_t presentID, PresentTracePhase tracePhase)
{
    const auto settings = Upscaling::GetSingleton()->settings;
    if (!settings.debugLogging || settings.debugFrameLogCount <= 0)
    {
        return false;
    }

    const auto bootstrapFrames = static_cast<uint64_t>(std::min(settings.debugFrameLogCount, 12));
    if (presentID < bootstrapFrames)
    {
        return true;
    }

    static PresentTracePhase previousTracePhase = PresentTracePhase::kNone;
    static uint64_t traceWindowStart = UINT64_MAX;

    if (tracePhase != PresentTracePhase::kNone && tracePhase != previousTracePhase)
    {
        traceWindowStart = presentID;
        logger::info("[DX12SwapChain] Present trace window started at present={} phase={} for {} frames", presentID,
                     GetPresentTracePhaseName(tracePhase), settings.debugFrameLogCount);
    }
    previousTracePhase = tracePhase;

    return traceWindowStart != UINT64_MAX && presentID >= traceWindowStart &&
           presentID - traceWindowStart < static_cast<uint64_t>(settings.debugFrameLogCount);
}

struct ScopedPresentTraceFlag
{
    explicit ScopedPresentTraceFlag(Upscaling *a_upscaling, bool a_enabled) : upscaling(a_upscaling)
    {
        if (upscaling)
        {
            upscaling->debugTraceCurrentPresent = a_enabled;
        }
    }

    ~ScopedPresentTraceFlag()
    {
        if (upscaling)
        {
            upscaling->debugTraceCurrentPresent = false;
        }
    }

    Upscaling *upscaling;
};

const char *GetFrameGenerationBackendName(bool dlss, bool fsr)
{
    if (dlss)
    {
        return "DLSS-G";
    }
    if (fsr)
    {
        return "FSR-FG";
    }
    return "none";
}

enum class FrameGenerationBlockReason
{
    kUIUnavailable,
    kBlockingMenuOpen,
    kPostLoadingSettle,
    kPostMenuSettle
};

struct FrameGenerationBlock
{
    FrameGenerationBlockReason reason;
    std::string_view detail;

    constexpr bool operator==(const FrameGenerationBlock &) const = default;
};

// VATS, HUD, dialogue, and workshop remain FrameGen-enabled as gameplay surfaces.
constexpr std::array kFrameGenerationBlockingMenus{
    std::string_view{"MainMenu"},           std::string_view{"LoadingMenu"},
    std::string_view{"FaderMenu"},          std::string_view{"PauseMenu"},
    std::string_view{"PipboyMenu"},         std::string_view{"TerminalMenu"},
    std::string_view{"ExamineMenu"},        std::string_view{"ExamineConfirmMenu"},
    std::string_view{"ContainerMenu"},      std::string_view{"BarterMenu"},
    std::string_view{"LockpickingMenu"},    std::string_view{"MessageBoxMenu"},
    std::string_view{"SitWaitMenu"},        std::string_view{"HolotapeMenu"},
    std::string_view{"PipboyHolotapeMenu"}, std::string_view{"TerminalHolotapeMenu"},
    std::string_view{"PowerArmorModMenu"}};
constexpr std::uint32_t kFrameGenerationPostMenuSettlePresents = 120;

const char *GetFrameGenerationBlockReasonName(FrameGenerationBlockReason reason)
{
    switch (reason)
    {
    case FrameGenerationBlockReason::kUIUnavailable:
        return "ui-unavailable";
    case FrameGenerationBlockReason::kBlockingMenuOpen:
        return "menu-open";
    case FrameGenerationBlockReason::kPostLoadingSettle:
        return "post-loading-settle";
    case FrameGenerationBlockReason::kPostMenuSettle:
        return "post-menu-settle";
    default:
        return "unknown";
    }
}

std::optional<FrameGenerationBlock> GetFrameGenerationUIBlock()
{
    auto *ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return FrameGenerationBlock{FrameGenerationBlockReason::kUIUnavailable, "UI"};
    }

    const auto openMenu = std::ranges::find_if(kFrameGenerationBlockingMenus,
                                               [ui](std::string_view menu) { return ui->GetMenuOpen(menu.data()); });
    if (openMenu != kFrameGenerationBlockingMenus.end())
    {
        return FrameGenerationBlock{FrameGenerationBlockReason::kBlockingMenuOpen, *openMenu};
    }

    return std::nullopt;
}

std::string_view GetFrameGenerationBlockReasonName(const std::optional<FrameGenerationBlock> &block)
{
    return block ? GetFrameGenerationBlockReasonName(block->reason) : "none";
}

std::string_view GetFrameGenerationBlockDetail(const std::optional<FrameGenerationBlock> &block)
{
    return block ? block->detail : "none";
}

} // namespace

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter *a_adapter)
{
    DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));
    if (ID3D12Device *upgradedDevice = d3d12Device.get();
        Streamline::GetSingleton()->UpgradeD3D12DeviceForDLSSG(&upgradedDevice) && upgradedDevice != d3d12Device.get())
    {
        d3d12Device.attach(upgradedDevice);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.NodeMask = 0;

    DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
    Streamline::GetSingleton()->LogD3D12CommandQueueProxyState(commandQueue.get());

    for (int i = 0; i < 2; i++)
    {
        DX::ThrowIfFailed(
            d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
        DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(),
                                                         nullptr, IID_PPV_ARGS(&commandLists[i])));
        commandLists[i]->Close();
    }
}

void DX12SwapChain::CreateSwapChain(IDXGIFactory4 *a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
    swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
    swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
    swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
    isHDR = (swapChainDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) ||
            (swapChainDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (isHDR)
    {
        logger::info("[DX12SwapChain] HDR detected: fmt={} {}", static_cast<uint32_t>(swapChainDesc.Format),
                     swapChainDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM ? "HDR10" : "scRGB");
    }
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    BOOL allowTearing = FALSE;
    if (winrt::com_ptr<IDXGIFactory5> dxgiFactory5;
        SUCCEEDED(a_dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory5.put()))))
    {
        DX::ThrowIfFailed(
            dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)));
    }

    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (allowTearing)
    {
        swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    auto upscaling = Upscaling::GetSingleton();
    auto fidelityFX = FidelityFX::GetSingleton();
    const bool useFidelityFXSwapChain = upscaling->UsesFSRFrameGeneration() && fidelityFX->module;
    IDXGIFactory4 *dxgiFactory = a_dxgiFactory;
    logger::info("[DX12SwapChain] Creating D3D12 proxy swap chain {}x{} fmt={} flags=0x{:X} backend={}",
                 swapChainDesc.Width, swapChainDesc.Height, static_cast<uint32_t>(swapChainDesc.Format),
                 swapChainDesc.Flags, useFidelityFXSwapChain ? "FidelityFX" : "native");

    const auto createNativeSwapChain = [&]() {
        winrt::com_ptr<IDXGISwapChain1> nativeSwapChain;
        DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(commandQueue.get(), a_swapChainDesc.OutputWindow,
                                                              &swapChainDesc, nullptr, nullptr, nativeSwapChain.put()));
        DX::ThrowIfFailed(nativeSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain)));
    };

    if (useFidelityFXSwapChain)
    {
        ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};

        ffxSwapChainDesc.desc = &swapChainDesc;
        ffxSwapChainDesc.dxgiFactory = a_dxgiFactory;
        ffxSwapChainDesc.fullscreenDesc = nullptr;
        ffxSwapChainDesc.gameQueue = commandQueue.get();
        ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
        ffxSwapChainDesc.swapchain = &swapChain;

        if (ffx::CreateContext(fidelityFX->swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok ||
            !swapChain)
        {
            logger::error("[FidelityFX] Failed to create swap chain context, using native D3D12 swap chain");
            swapChain = nullptr;
            fidelityFX->swapChainContext = nullptr;
            createNativeSwapChain();
        }
    }
    else
    {
        createNativeSwapChain();
    }

    DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
    DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

    frameIndex = swapChain->GetCurrentBackBufferIndex();
    logger::info("[DX12SwapChain] Swap chain ready (frameIndex={}, buffers={})", frameIndex, swapChainDesc.BufferCount);

    if (useFidelityFXSwapChain && fidelityFX->swapChainContext != nullptr)
        fidelityFX->SetupFrameGeneration();

    swapChainProxy = new DXGISwapChainProxy(swapChain);

    ResolveOverlayCallbacks();
    if (auto initCb = s_overlayInitCb ? s_overlayInitCb : overlayInitCallback)
    {
        initCb(d3d12Device.get(), commandQueue.get(), swapChain, swapChainDesc.Format, a_swapChainDesc.OutputWindow);
    }
}

void DX12SwapChain::CreateInterop()
{
    HANDLE sharedFenceHandle;
    DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
    DX::ThrowIfFailed(
        d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
    DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
    CloseHandle(sharedFenceHandle);
    d3d12FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d3d12FenceEvent)
    {
        DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    D3D11_TEXTURE2D_DESC texDesc11{};
    texDesc11.Width = swapChainDesc.Width;
    texDesc11.Height = swapChainDesc.Height;
    texDesc11.MipLevels = 1;
    texDesc11.ArraySize = 1;
    texDesc11.Format = swapChainDesc.Format;
    texDesc11.SampleDesc.Count = 1;
    texDesc11.SampleDesc.Quality = 0;
    texDesc11.Usage = D3D11_USAGE_DEFAULT;
    texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texDesc11.CPUAccessFlags = 0;
    texDesc11.MiscFlags = 0;

    if (enbLoaded)
        swapChainBufferProxyENB = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
    else
        swapChainBufferProxy = new Texture2D(texDesc11);

    swapChainBufferWrapped[0] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
    swapChainBufferWrapped[1] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy *DX12SwapChain::GetSwapChainProxy()
{
    return swapChainProxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device *a_d3d11Device)
{
    DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext *a_d3d11Context)
{
    DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(void **ppSurface)
{
    if (enbLoaded)
        *ppSurface = swapChainBufferProxyENB->resource11;
    else
        *ppSurface = swapChainBufferProxy->resource.get();
    return S_OK;
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
    static std::atomic_uint64_t presentCounter{0};
    static bool lastFrameGenerationActive = false;
    static const char *lastFrameGenerationBackend = "none";
    static bool frameCallbackFailureLogged = false;
    static std::optional<FrameGenerationBlock> lastFrameGenerationBlock;

    const auto presentID = presentCounter.fetch_add(1, std::memory_order_relaxed);
    auto upscaling = Upscaling::GetSingleton();
    auto streamline = Streamline::GetSingleton();
    const auto frameGenerationUIBlock = GetFrameGenerationUIBlock();
    const bool loadingMenuOpen = frameGenerationUIBlock &&
                                 frameGenerationUIBlock->reason == FrameGenerationBlockReason::kBlockingMenuOpen &&
                                 frameGenerationUIBlock->detail == "LoadingMenu";

    const auto tracePhase = loadingMenuOpen
                                ? PresentTracePhase::kLoading
                                : (!frameGenerationUIBlock ? PresentTracePhase::kGameplay : PresentTracePhase::kNone);
    const bool traceFrame = ShouldTracePresentFrame(presentID, tracePhase);
    ScopedPresentTraceFlag scopedTraceFlag(upscaling, traceFrame);
    const char *stage = "begin";
    const auto trace = [&](const char *nextStage) { stage = nextStage; };

    try
    {
        if (traceFrame)
        {
            logger::debug("[DX12SwapChain] Present#{} begin frameIndex={}", presentID, frameIndex);
        }
        if (frameCallback)
        {
            trace("frame-callback");
            try
            {
                frameCallback();
            }
            catch (const std::exception &e)
            {
                if (!frameCallbackFailureLogged)
                {
                    frameCallbackFailureLogged = true;
                    logger::error("[DX12SwapChain] Frame callback failed: {}", e.what());
                }
            }
            catch (...)
            {
                if (!frameCallbackFailureLogged)
                {
                    frameCallbackFailureLogged = true;
                    logger::error("[DX12SwapChain] Frame callback failed with unknown exception");
                }
            }
        }
        trace("reflex-sleep");
        streamline->SleepReflexFrame("present");

        ID3D11Texture2D *finalFrame =
            enbLoaded ? swapChainBufferProxyENB->resource11 : swapChainBufferProxy->resource.get();

        trace("copy-d3d11-proxy-to-shared");
        if (enbLoaded)
            d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);
        else
            d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);

        const bool uiColorAndAlphaReady =
            upscaling->UsesDLSSFrameGeneration() &&
            upscaling->BuildUIColorAndAlphaResource(swapChainBufferWrapped[frameIndex]->resource11);

        trace("wait-d3d11-to-d3d12");
        DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
        DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
        fenceValue++;

        WaitForCommandAllocator(frameIndex);
        trace("reset-command-list");
        DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
        DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

        auto fakeSwapChain = swapChainBufferWrapped[frameIndex]->resource.get();
        auto realSwapChain = swapChainBuffers[frameIndex].get();

        trace("copy-shared-to-backbuffer");
        {
            CD3DX12_RESOURCE_BARRIER barriers[] = {
                CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON,
                                                     D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT,
                                                     D3D12_RESOURCE_STATE_COPY_DEST)};
            commandLists[frameIndex]->ResourceBarrier(2, barriers);
        }

        if (isHDR)
        {
            trace("color-space-conversion");
            EnsureColorSpaceResources();
            {
                CD3DX12_RESOURCE_BARRIER barriers[] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON,
                                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                    CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT,
                                                         D3D12_RESOURCE_STATE_RENDER_TARGET)};
                commandLists[frameIndex]->ResourceBarrier(2, barriers);
            }

            ID3D12DescriptorHeap *heaps[] = {colorSpaceSRVHeap.get()};
            commandLists[frameIndex]->SetDescriptorHeaps(1, heaps);

            commandLists[frameIndex]->SetGraphicsRootSignature(colorSpaceRootSig.get());
            commandLists[frameIndex]->SetPipelineState(colorSpacePSO.get());

            const float peakNits = 1000.0f;
            const UINT rootConstants[2] = {*reinterpret_cast<const UINT *>(&peakNits),
                                           (swapChainDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) ? 1U : 0U};
            commandLists[frameIndex]->SetGraphicsRoot32BitConstants(0, 2, rootConstants, 0);

            const CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(colorSpaceSRVHeap->GetGPUDescriptorHandleForHeapStart(),
                                                          frameIndex, colorSpaceSRVHandleSize);
            commandLists[frameIndex]->SetGraphicsRootDescriptorTable(1, srvHandle);

            const CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(colorSpaceRTVHeap->GetCPUDescriptorHandleForHeapStart(),
                                                          frameIndex, colorSpaceRTVHandleSize);
            commandLists[frameIndex]->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

            commandLists[frameIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandLists[frameIndex]->DrawInstanced(3, 1, 0, 0);

            {
                CD3DX12_RESOURCE_BARRIER barriers[] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                         D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                         D3D12_RESOURCE_STATE_PRESENT)};
                commandLists[frameIndex]->ResourceBarrier(2, barriers);
            }
        }
        else
        {
            commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

            {
                CD3DX12_RESOURCE_BARRIER barriers[] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                         D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_RESOURCE_STATE_PRESENT)};
                commandLists[frameIndex]->ResourceBarrier(2, barriers);
            }
        }

        bool useFrameGenerationThisFrame = false;
        auto fidelityFX = FidelityFX::GetSingleton();
        const bool useDLSSFrameGeneration = upscaling->UsesDLSSFrameGeneration() && streamline->featureDLSSG;
        const bool useFSRFrameGeneration = upscaling->UsesFSRFrameGeneration() && fidelityFX->featureFrameGen;
        const bool frameGenerationBackendAvailable = useDLSSFrameGeneration || useFSRFrameGeneration;
        const char *frameGenerationBackend =
            GetFrameGenerationBackendName(useDLSSFrameGeneration, useFSRFrameGeneration);

        bool skipFgAfterLoading = false;
        {
            static bool wasLoading = false;
            if (!loadingMenuOpen && wasLoading)
            {
                skipFgAfterLoading = true;
                upscaling->postLoadingSkipUpscale = true;
            }
            wasLoading = loadingMenuOpen;
        }

        auto frameGenerationBlock = frameGenerationUIBlock;
        {
            static std::uint32_t postMenuSettlePresents = 0;
            static std::string_view postMenuSettleDetail{"post-menu"};

            if (frameGenerationUIBlock &&
                frameGenerationUIBlock->reason == FrameGenerationBlockReason::kBlockingMenuOpen)
            {
                postMenuSettlePresents = kFrameGenerationPostMenuSettlePresents;
                postMenuSettleDetail = frameGenerationUIBlock->detail;
            }
            else if (postMenuSettlePresents > 0)
            {
                --postMenuSettlePresents;
            }

            if (!frameGenerationBlock && postMenuSettlePresents > 0 && !skipFgAfterLoading)
            {
                frameGenerationBlock =
                    FrameGenerationBlock{FrameGenerationBlockReason::kPostMenuSettle, postMenuSettleDetail};
            }
        }
        if (!frameGenerationBlock && skipFgAfterLoading)
        {
            frameGenerationBlock = FrameGenerationBlock{FrameGenerationBlockReason::kPostLoadingSettle, "post-loading"};
        }

        useFrameGenerationThisFrame = frameGenerationBackendAvailable && !frameGenerationBlock;

        if (upscaling->pluginMode != Upscaling::PluginMode::kReflex &&
            (presentID == 0 || lastFrameGenerationActive != useFrameGenerationThisFrame ||
             std::string_view(lastFrameGenerationBackend) != frameGenerationBackend ||
             lastFrameGenerationBlock != frameGenerationBlock))
        {
            logger::info("[FrameGen] present={} backend={} active={} available={} phase={} block={} detail={}",
                         presentID, frameGenerationBackend, useFrameGenerationThisFrame,
                         frameGenerationBackendAvailable, GetPresentTracePhaseName(tracePhase),
                         GetFrameGenerationBlockReasonName(frameGenerationBlock),
                         GetFrameGenerationBlockDetail(frameGenerationBlock));
            lastFrameGenerationActive = useFrameGenerationThisFrame;
            lastFrameGenerationBackend = frameGenerationBackend;
            lastFrameGenerationBlock = frameGenerationBlock;
        }

        trace("frame-generation");
        if (useDLSSFrameGeneration)
        {
            const bool dlssgTagged = streamline->TagResourcesAndConfigure(
                upscaling->HUDLessBufferShared12[frameIndex].get(),
                uiColorAndAlphaReady ? upscaling->uiColorAndAlphaBufferShared12[frameIndex].get() : nullptr,
                upscaling->depthBufferShared12[frameIndex].get(),
                upscaling->motionVectorBufferShared12[frameIndex].get(), useFrameGenerationThisFrame);
            if (!dlssgTagged && useFrameGenerationThisFrame)
            {
                logger::warn("[FrameGen] DLSS-G skipped this frame after Streamline tagging/configuration failure");
                useFrameGenerationThisFrame = false;
            }
        }

        if (useFSRFrameGeneration)
        {
            fidelityFX->Present(useFrameGenerationThisFrame);
        }

        // Fallback hotkey polling. Works even if WndProc hook is displaced
        ResolveOverlayCallbacks();
        if (auto pollCb = s_overlayPollCb ? s_overlayPollCb : overlayPollCallback)
        {
            pollCb();
        }

        trace("overlay");
        if (auto presentCb = s_overlayPresentCb ? s_overlayPresentCb : overlayPresentCallback)
        {
            auto *backBuffer = swapChainBuffers[frameIndex].get();
            CD3DX12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(
                backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandLists[frameIndex]->ResourceBarrier(1, &toRT);
            presentCb(commandLists[frameIndex].get(), backBuffer, swapChainDesc.Format);
            CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
                backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandLists[frameIndex]->ResourceBarrier(1, &toPresent);
        }

        trace("close-command-list");
        DX::ThrowIfFailed(commandLists[frameIndex]->Close());

        trace("execute-command-list");
        ID3D12CommandList *commandListsToExecute[] = {commandLists[frameIndex].get()};
        commandQueue->ExecuteCommandLists(1, commandListsToExecute);

        // Fix FPS cap being e.g. 55 instead of 60
        if (!upscaling->highFPSPhysicsFixLoaded && SyncInterval > 0)
            SyncInterval = 1;

        streamline->SetPCLMarker(sl::PCLMarker::ePresentStart, "present-start");
        trace("present");
        const auto presentResult = swapChain->Present(SyncInterval, Flags);
        if (FAILED(presentResult))
        {
            logger::error("[DX12SwapChain] IDXGISwapChain::Present failed: {}", FormatHRESULT(presentResult));
            streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");
            streamline->AdvanceFrame();
            return presentResult;
        }

        streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");

        if (useDLSSFrameGeneration)
        {
            trace("dlssg-present-state");
            streamline->LogDLSSGPresentState(useFrameGenerationThisFrame, presentID);
        }

        trace("wait-d3d12-to-d3d11");
        DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
        commandAllocatorFenceValues[frameIndex] = fenceValue;
        DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
        fenceValue++;

        streamline->AdvanceFrame();

        trace("skip-frame-latency-wait");

        trace("update-frame-index");
        frameIndex = swapChain->GetCurrentBackBufferIndex();

        trace("reset-shared-resources");
        if (frameGenerationBackendAvailable)
        {
            upscaling->Reset();
        }

        trace("game-frame-limiter");
        if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && upscaling->settings.frameLimitMode &&
            !upscaling->highFPSPhysicsFixLoaded)
        {
            upscaling->GameFrameLimiter();
        }

        trace("frame-limiter");
        if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && SyncInterval == 0)
            upscaling->FrameLimiter(useFrameGenerationThisFrame);

        if (traceFrame)
        {
            logger::debug("[DX12SwapChain] Present#{} completed (nextFrameIndex={})", presentID, frameIndex);
        }

        return S_OK;
    }
    catch (const winrt::hresult_error &e)
    {
        const auto hr = static_cast<HRESULT>(e.code());
        logger::error("[DX12SwapChain] Present failed at stage '{}' with HRESULT {}", stage, FormatHRESULT(hr));
        commandLists[frameIndex]->Close();
        commandAllocators[frameIndex]->Reset();
        commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
        streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");
        streamline->AdvanceFrame();
        return hr;
    }
    catch (const std::exception &e)
    {
        logger::error("[DX12SwapChain] Present failed at stage '{}': {}", stage, e.what());
        commandLists[frameIndex]->Close();
        commandAllocators[frameIndex]->Reset();
        commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
        streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");
        streamline->AdvanceFrame();
        return DXGI_ERROR_DEVICE_REMOVED;
    }
    catch (...)
    {
        logger::error("[DX12SwapChain] Present failed at stage '{}' with unknown exception", stage);
        commandLists[frameIndex]->Close();
        commandAllocators[frameIndex]->Reset();
        commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
        streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");
        streamline->AdvanceFrame();
        return DXGI_ERROR_DEVICE_REMOVED;
    }
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void **ppDevice)
{
    if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) ||
        uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5))
    {
        *ppDevice = d3d11Device.get();
        return S_OK;
    }

    return swapChain->GetDevice(uuid, ppDevice);
}

void DX12SwapChain::WaitForCommandAllocator(UINT a_index)
{
    const auto waitFenceValue = commandAllocatorFenceValues[a_index];
    if (waitFenceValue == 0 || d3d12Fence->GetCompletedValue() >= waitFenceValue)
    {
        return;
    }

    DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(waitFenceValue, d3d12FenceEvent));
    const auto waitResult = WaitForSingleObject(d3d12FenceEvent, 1000);
    if (waitResult == WAIT_OBJECT_0)
    {
        return;
    }

    logger::warn("[DX12SwapChain] Timed out waiting for command allocator {} fence={} completed={}", a_index,
                 waitFenceValue, d3d12Fence->GetCompletedValue());
    DX::ThrowIfFailed(HRESULT_FROM_WIN32(waitResult == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError()));
}

ID3D12GraphicsCommandList4 *DX12SwapChain::BeginInteropCommandList()
{
    DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
    DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
    fenceValue++;

    WaitForCommandAllocator(frameIndex);
    DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
    DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));
    return commandLists[frameIndex].get();
}

void DX12SwapChain::ExecuteInteropCommandListAndWait()
{
    DX::ThrowIfFailed(commandLists[frameIndex]->Close());

    ID3D12CommandList *lists[] = {commandLists[frameIndex].get()};
    commandQueue->ExecuteCommandLists(1, lists);

    DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
    commandAllocatorFenceValues[frameIndex] = fenceValue;
    DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
    fenceValue++;
}

void DX12SwapChain::EnsureColorSpaceResources()
{
    if (colorSpacePSO)
    {
        return; // already created
    }

    // --- Root signature: root constants (b0) + descriptor table (t0) + static sampler ---
    CD3DX12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[2] = {};
    params[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                 D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Init(static_cast<UINT>(std::size(params)), params, 1, &sampler,
                     D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    winrt::com_ptr<ID3DBlob> sigBlob, errorBlob;
    DX::ThrowIfFailed(
        D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.put(), errorBlob.put()));
    DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                                       IID_PPV_ARGS(&colorSpaceRootSig)));

    // --- Compile shader and create PSO ---
    winrt::com_ptr<ID3DBlob> vsBlob, psBlob;
    {
        winrt::com_ptr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(kColorSpaceShader, strlen(kColorSpaceShader), nullptr, nullptr, nullptr, "VSMain",
                                "vs_5_0", 0, 0, vsBlob.put(), errors.put());
        if (FAILED(hr))
        {
            const char *msg = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "unknown";
            logger::error("[DX12SwapChain] VS compile failed: {}", msg);
            DX::ThrowIfFailed(hr);
        }
    }
    {
        winrt::com_ptr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(kColorSpaceShader, strlen(kColorSpaceShader), nullptr, nullptr, nullptr, "PSMain",
                                "ps_5_0", 0, 0, psBlob.put(), errors.put());
        if (FAILED(hr))
        {
            const char *msg = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "unknown";
            logger::error("[DX12SwapChain] PS compile failed: {}", msg);
            DX::ThrowIfFailed(hr);
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = colorSpaceRootSig.get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = swapChainDesc.Format;
    psoDesc.SampleDesc.Count = 1;
    DX::ThrowIfFailed(d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&colorSpacePSO)));

    // --- SRV descriptor heap (2 entries, one per backbuffer) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&colorSpaceSRVHeap)));
        colorSpaceSRVHandleSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = swapChainDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        for (int i = 0; i < 2; i++)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(colorSpaceSRVHeap->GetCPUDescriptorHandleForHeapStart(), i,
                                                 colorSpaceSRVHandleSize);
            d3d12Device->CreateShaderResourceView(swapChainBufferWrapped[i]->resource.get(), &srvDesc, handle);
        }
    }

    // --- RTV descriptor heap (2 entries, one per swapchain backbuffer) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&colorSpaceRTVHeap)));
        colorSpaceRTVHandleSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (int i = 0; i < 2; i++)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(colorSpaceRTVHeap->GetCPUDescriptorHandleForHeapStart(), i,
                                                 colorSpaceRTVHandleSize);
            d3d12Device->CreateRenderTargetView(swapChainBuffers[i].get(), nullptr, handle);
        }
    }

    logger::info("[DX12SwapChain] Color space conversion resources created (fmt={})",
                 static_cast<uint32_t>(swapChainDesc.Format));
}

void DX12SwapChain::DestroyColorSpaceResources()
{
    colorSpaceRootSig = nullptr;
    colorSpacePSO = nullptr;
    colorSpaceSRVHeap = nullptr;
    colorSpaceRTVHeap = nullptr;
    colorSpaceCB = nullptr;
    colorSpaceSRVHandleSize = 0;
    colorSpaceRTVHandleSize = 0;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5 *a_d3d11Device,
                                 ID3D12Device *a_d3d12Device)
{
    // Create D3D11 shared texture directly instead of wrapping D3D12 resource
    a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11));

    // Get shared handle from D3D11 texture to enable D3D12 access
    winrt::com_ptr<IDXGIResource1> dxgiResource;
    DX::ThrowIfFailed(resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
    HANDLE sharedHandle = nullptr;
    DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                       nullptr, &sharedHandle));

    // Open the shared D3D11 texture as D3D12 resource
    DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
    CloseHandle(sharedHandle);

    if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = a_texDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
    }

    if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
    {
        if (a_texDesc.ArraySize > 1)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = a_texDesc.Format;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

            DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
        }
        else
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = a_texDesc.Format;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;

            DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
        }
    }

    if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
    {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = a_texDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
    }
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4 *a_swapChain)
{
    swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void **ppvObj)
{
    auto ret = swapChain->QueryInterface(riid, ppvObj);
    if (*ppvObj)
        *ppvObj = this;
    return ret;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
    return swapChain->AddRef();
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
    return swapChain->Release();
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize,
                                                             _In_reads_bytes_(DataSize) const void *pData)
{
    return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name,
                                                                      _In_opt_ const IUnknown *pUnknown)
{
    return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT *pDataSize,
                                                             _Out_writes_bytes_(*pDataSize) void *pData)
{
    return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void **ppParent)
{
    return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void **ppDevice)
{
    return DX12SwapChain::GetSingleton()->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
    return DX12SwapChain::GetSingleton()->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID, _COM_Outptr_ void **ppSurface)
{
    return DX12SwapChain::GetSingleton()->GetBuffer(ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL, _In_opt_ IDXGIOutput *)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(
    _Out_opt_ BOOL *pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput **ppTarget)
{
    return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC *pDesc)
{
    return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                            DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    return swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC *)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput **ppOutput)
{
    return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS *pStats)
{
    return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT *pLastPresentCount)
{
    return swapChain->GetLastPresentCount(pLastPresentCount);
}
