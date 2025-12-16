#include "DX11Hooks.h"

#include <d3d11.h>

#include "Streamline.h"

extern bool enbLoaded;

struct BSGraphics_CreateD3DAndSwapChain_D3D11CreateDeviceAndSwapChain
{
	static HRESULT WINAPI thunk(
		IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext)
	{
		const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
		pFeatureLevels = &featureLevel;
		FeatureLevels = 1;

		auto ret = func(pAdapter,
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
			
		auto streamline = Streamline::GetSingleton();
		streamline->LoadInterposer();

		if (streamline->interposer){
			streamline->Initialize();
			if (!enbLoaded){
				streamline->slUpgradeInterface((void**)&(*ppDevice));
				streamline->slUpgradeInterface((void**)&(*ppSwapChain));
			}
			streamline->slSetD3DDevice(*ppDevice);
			streamline->CheckFeatures(pAdapter);
			streamline->PostDevice();
		}

		return ret;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

namespace DX11Hooks
{
	void Install()
	{
#if defined(FALLOUT_POST_NG)
		uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);
		(uintptr_t&)BSGraphics_CreateD3DAndSwapChain_D3D11CreateDeviceAndSwapChain::func = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)BSGraphics_CreateD3DAndSwapChain_D3D11CreateDeviceAndSwapChain::thunk);
#else
		// Hook BSGraphics::CreateD3DAndSwapChain::D3D11CreateDeviceAndSwapChain to use D3D_FEATURE_LEVEL_11_1
		stl::write_thunk_call<BSGraphics_CreateD3DAndSwapChain_D3D11CreateDeviceAndSwapChain>(REL::ID(224250).address() + 0x419);
#endif
	}
}