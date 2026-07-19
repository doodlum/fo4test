#pragma once

struct ID3D11Device;
struct IDXGISwapChain;

namespace DX11Hooks
{
	void Install();
	void NotifyD3D11DeviceCreated(ID3D11Device* a_device);
	bool RecoverExistingRenderer(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);
	using DeviceCreatedCallback = void (*)(ID3D11Device* a_device);
	void SetDeviceCreatedCallback(DeviceCreatedCallback a_callback);
	using PresentCallback = void (*)(IDXGISwapChain* a_swapChain);
	void SetPresentCallback(PresentCallback a_callback);
}
