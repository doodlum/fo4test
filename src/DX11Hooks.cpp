#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "Upscaling.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"

#include "ENB/ENBSeriesAPI.h"

bool enbLoaded = false;

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;
decltype(&IDXGIFactory::CreateSwapChain) ptrCreateSwapChain;

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
	IDXGIDevice* dxgiDevice = nullptr;
	DX::ThrowIfFailed(a_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

	IDXGIAdapter* adapter = nullptr;
	DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));

	auto proxy = DX12SwapChain::GetSingleton();

	proxy->SetD3D11Device(a_device);

	ID3D11DeviceContext* context;
	a_device->GetImmediateContext(&context);
	proxy->SetD3D11DeviceContext(context);

	proxy->CreateD3D12Device(adapter);
	proxy->CreateSwapChain((IDXGIFactory4*)This, *pDesc);
	proxy->CreateInterop();

	*ppSwapChain = proxy->GetSwapChainProxy();

	return S_OK;
}

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	auto upscaling = Upscaling::GetSingleton();

	if (pSwapChainDesc->Windowed) {
		logger::info("[Frame Generation] Frame Generation enabled, using D3D12 proxy");
		
		auto fidelityFX = FidelityFX::GetSingleton();

		if (fidelityFX->module) {
			upscaling->d3d12Interop = true;

			IDXGIFactory4* dxgiFactory;
			pAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

			pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
			pFeatureLevels = &featureLevel;
			FeatureLevels = 1;

			if (enbLoaded) {
				*(uintptr_t*)&ptrCreateSwapChain = Detours::X64::DetourClassVTable(*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory_CreateSwapChain, 10);
			}
			else {
				DX::ThrowIfFailed(D3D11CreateDevice(
					pAdapter,
					DriverType,
					Software,
					Flags,
					pFeatureLevels,
					FeatureLevels,
					SDKVersion,
					ppDevice,
					pFeatureLevel,
					ppImmediateContext));

				IDXGIDevice* dxgiDevice = nullptr;
				DX::ThrowIfFailed((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

				IDXGIAdapter* adapter = nullptr;
				DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));

				auto proxy = DX12SwapChain::GetSingleton();

				proxy->SetD3D11Device(*ppDevice);

				ID3D11DeviceContext* context;
				(*ppDevice)->GetImmediateContext(&context);
				proxy->SetD3D11DeviceContext(context);

				proxy->CreateD3D12Device(adapter);
				proxy->CreateSwapChain((IDXGIFactory4*)dxgiFactory, *pSwapChainDesc);
				proxy->CreateInterop();

				*ppSwapChain = proxy->GetSwapChainProxy();
				
				return S_OK;
			}

		} else {
			logger::warn("[Frame Generation] amd_fidelityfx_dx12.dll is not loaded, skipping proxy");
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChain(
		pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	return ret;
}

void DX11Hooks::Install()
{
	if (ENB_API::RequestENBAPI()) {
		logger::info("ENB detected, using alternative swap chain hook");
		enbLoaded = true;
	} else {
		logger::info("ENB not detected, using standard swap chain hook");
	}

	auto fidelityFX = FidelityFX::GetSingleton();
	fidelityFX->LoadFFX();

	uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);

	(uintptr_t&)ptrD3D11CreateDeviceAndSwapChain = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hk_D3D11CreateDeviceAndSwapChain);
}
