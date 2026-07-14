#pragma once

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include "Buffer.h"

// --- Overlay callbacks (registered by Overlay.dll at load time) ---
using OverlayInitCallback = void (*)(ID3D12Device *device, ID3D12CommandQueue *queue, IDXGISwapChain4 *swapChain,
                                     DXGI_FORMAT format, HWND hwnd);
using OverlayPresentCallback = void (*)(ID3D12GraphicsCommandList4 *cmdList, ID3D12Resource *backBuffer,
                                        DXGI_FORMAT format);
using OverlayPollCallback = void (*)();
using FrameCallback = void (*)();

class WrappedResource
{
  public:
    WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5 *a_d3d11Device, ID3D12Device *a_d3d12Device);

    ID3D11Texture2D *resource11;
    ID3D11ShaderResourceView *srv;
    ID3D11UnorderedAccessView *uav;
    ID3D11RenderTargetView *rtv;
    winrt::com_ptr<ID3D12Resource> resource;
};

struct DXGISwapChainProxy : IDXGISwapChain
{
  public:
    DXGISwapChainProxy(IDXGISwapChain4 *a_swapChain);

    IDXGISwapChain4 *swapChain;

    /****IUnknown****/
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;

    /****IDXGIObject****/
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, UINT DataSize,
                                                     _In_reads_bytes_(DataSize) const void *pData) override;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name,
                                                              _In_opt_ const IUnknown *pUnknown) override;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT *pDataSize,
                                                     _Out_writes_bytes_(*pDataSize) void *pData) override;
    virtual HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void **ppParent) override;

    /****IDXGIDeviceSubObject****/
    virtual HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void **ppDevice) override;

    /****IDXGISwapChain****/
    virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags);
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void **ppSurface);
    virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput *pTarget);
    virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL *pFullscreen,
                                                         _COM_Outptr_opt_result_maybenull_ IDXGIOutput **ppTarget);
    virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC *pDesc);
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                                    UINT SwapChainFlags);
    virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC *pNewTargetParameters);
    virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput **ppOutput);
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS *pStats);
    virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT *pLastPresentCount);
};

class DX12SwapChain
{
  public:
    static DX12SwapChain *GetSingleton()
    {
        static DX12SwapChain singleton;
        return &singleton;
    }

    winrt::com_ptr<ID3D12Device> d3d12Device;
    winrt::com_ptr<ID3D12CommandQueue> commandQueue;
    winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[2];
    winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[2];

    IDXGISwapChain4 *swapChain;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    OverlayInitCallback overlayInitCallback = nullptr;
    OverlayPresentCallback overlayPresentCallback = nullptr;
    OverlayPollCallback overlayPollCallback = nullptr;
    FrameCallback frameCallback = nullptr;

    void RegisterOverlayInitCallback(OverlayInitCallback a_cb)
    {
        overlayInitCallback = a_cb;
    }
    void RegisterOverlayPresentCallback(OverlayPresentCallback a_cb)
    {
        overlayPresentCallback = a_cb;
    }
    void RegisterOverlayPollCallback(OverlayPollCallback a_cb)
    {
        overlayPollCallback = a_cb;
    }
    void RegisterFrameCallback(FrameCallback a_cb)
    {
        frameCallback = a_cb;
    }

    bool HasOverlayPresentCallback() const
    {
        return overlayPresentCallback != nullptr;
    }
    bool HasOverlayPollCallback() const
    {
        return overlayPollCallback != nullptr;
    }

    Texture2D *swapChainBufferProxy;
    WrappedResource *swapChainBufferProxyENB;

    WrappedResource *swapChainBufferWrapped[2];

    winrt::com_ptr<ID3D11Device5> d3d11Device;
    winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

    winrt::com_ptr<ID3D11Fence> d3d11Fence;
    winrt::com_ptr<ID3D12Fence> d3d12Fence;

    winrt::com_ptr<ID3D12Resource> swapChainBuffers[2];

    UINT frameIndex = 0;
    UINT64 fenceValue = 1;
    UINT64 commandAllocatorFenceValues[2]{};
    HANDLE d3d12FenceEvent = nullptr;

    LARGE_INTEGER qpf;

    DXGISwapChainProxy *swapChainProxy = nullptr;

    // HDR color space conversion
    winrt::com_ptr<ID3D12RootSignature> colorSpaceRootSig;
    winrt::com_ptr<ID3D12PipelineState> colorSpacePSO;
    winrt::com_ptr<ID3D12DescriptorHeap> colorSpaceSRVHeap;
    winrt::com_ptr<ID3D12DescriptorHeap> colorSpaceRTVHeap;
    winrt::com_ptr<ID3D12Resource> colorSpaceCB;
    UINT colorSpaceSRVHandleSize = 0;
    UINT colorSpaceRTVHandleSize = 0;
    bool isHDR = false;

    void CreateD3D12Device(IDXGIAdapter *a_adapter);
    void CreateSwapChain(IDXGIFactory4 *a_dxgiFactory, DXGI_SWAP_CHAIN_DESC swapChainDesc);

    void CreateInterop();

    DXGISwapChainProxy *GetSwapChainProxy();
    void SetD3D11Device(ID3D11Device *a_d3d11Device);
    void SetD3D11DeviceContext(ID3D11DeviceContext *a_d3d11Context);

    HRESULT GetBuffer(void **ppSurface);
    HRESULT Present(UINT SyncInterval, UINT Flags);
    HRESULT GetDevice(_In_ REFIID riid, _COM_Outptr_ void **ppDevice);

    void WaitForCommandAllocator(UINT a_index);

    ID3D12GraphicsCommandList4 *BeginInteropCommandList();
    void ExecuteInteropCommandListAndWait();

    void EnsureColorSpaceResources();
    void DestroyColorSpaceResources();
};
