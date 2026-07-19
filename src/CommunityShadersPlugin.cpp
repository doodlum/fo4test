#include "PluginCommon.h"

#include "Core/CommunityShaders.h"

#include "Core/Globals.h"
#include "Core/Menu.h"
#include "Diagnostics/HangTrace.h"
#include "DX11Hooks.h"
#include "DX12SwapChain.h"
#include "Overlay/Overlay.h"

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace
{
	std::atomic<ID3D11Device*> g_device{ nullptr };
	std::atomic_bool g_deviceReady{ false };
#if defined(FALLOUT_POST_AE)
	std::atomic_bool g_existingRendererRecoveryComplete{ false };
#endif

	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:enter");
		if (!a_device) {
			fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:skip");
			return;
		}

		ID3D11Device* expected = nullptr;
		if (!g_device.compare_exchange_strong(expected, a_device, std::memory_order_acq_rel)) {
			fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:skip");
			return;
		}

		fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:begin");
		CommunityShaders::Runtime::GetSingleton()->OnD3D11DeviceCreated(a_device);
		g_deviceReady.store(true, std::memory_order_release);
		fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:end");
	}

#if defined(FALLOUT_POST_AE)
	void RecoverExistingPostAERenderer()
	{
		if (g_existingRendererRecoveryComplete.load(std::memory_order_acquire)) {
			return;
		}

		if (DX12SwapChain::GetSingleton()->swapChain) {
			logger::debug("[CommunityShaders] PostAE existing-renderer recovery skipped; D3D12 proxy is active");
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			logger::warn(
				"[CommunityShaders] PostAE existing-renderer recovery unavailable (rendererData=0x0)");
			return;
		}

		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		auto* currentWindow = RE::BSGraphics::GetCurrentRendererWindow();
		if (currentWindow && currentWindow->hwnd && currentWindow->swapChain &&
			DX11Hooks::RecoverExistingRenderer(device, reinterpret_cast<IDXGISwapChain*>(currentWindow->swapChain))) {
			g_existingRendererRecoveryComplete.store(true, std::memory_order_release);
			return;
		}

		// RenderDoc can leave the engine's current-window pointer unset while the
		// renderer table already contains the live game window. Try those entries
		// synchronously on the F4SE message thread; never poll or patch a vtable
		// from a detached worker thread.
		for (auto& renderWindow : rendererData->renderWindow) {
			if (!renderWindow.hwnd || !renderWindow.swapChain || !IsWindow(reinterpret_cast<HWND>(renderWindow.hwnd))) {
				continue;
			}
			if (DX11Hooks::RecoverExistingRenderer(device, reinterpret_cast<IDXGISwapChain*>(renderWindow.swapChain))) {
				g_existingRendererRecoveryComplete.store(true, std::memory_order_release);
				return;
			}
		}

		logger::warn(
			"[CommunityShaders] PostAE existing-renderer recovery unavailable (rendererData=0x{:X}, currentWindow=0x{:X}, no valid renderer-table window)",
			reinterpret_cast<uintptr_t>(rendererData),
			reinterpret_cast<uintptr_t>(currentWindow));
	}
#endif

	void OnPresent(IDXGISwapChain* a_swapChain)
	{
#if !defined(FALLOUT_PRE_NG) && !defined(FALLOUT_POST_NG)
		(void)a_swapChain;
#endif
		fo4cs::Diagnostics::WriteHangTraceLine("Present:enter");
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (runtime->IsLoaded()) {
			fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnFrame:begin");
			runtime->OnFrame();
			fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnFrame:end");
		}

#if defined(FALLOUT_PRE_NG) || defined(FALLOUT_POST_NG)
		auto* device = g_deviceReady.load(std::memory_order_acquire) ?
			g_device.load(std::memory_order_acquire) : nullptr;
		if (!a_swapChain || !device) {
			fo4cs::Diagnostics::WriteHangTraceLine("Present:exit:no-swapchain-or-device");
			return;
		}

		if (DX12SwapChain::GetSingleton()->swapChain) {
			fo4cs::Diagnostics::WriteHangTraceLine("Present:skip-d3d11-menu-dx12-proxy");
			return;
		}

		ID3D11DeviceContext* context = nullptr;
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:begin");
		device->GetImmediateContext(&context);
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:end");
		if (context) {
			fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:begin");
			CommunityShaders::Menu::Render(device, context, a_swapChain);
			fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:end");
			context->Release();
		}
#endif
		fo4cs::Diagnostics::WriteHangTraceLine("Present:exit");
	}

	void OnDX12ProxyFrame()
	{
		fo4cs::Diagnostics::WriteHangTraceLine("DX12ProxyFrame:enter");
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (runtime->IsLoaded()) {
			fo4cs::Diagnostics::WriteHangTraceLine("DX12ProxyFrame:Runtime:OnFrame:begin");
			runtime->OnFrame();
			fo4cs::Diagnostics::WriteHangTraceLine("DX12ProxyFrame:Runtime:OnFrame:end");
		}
	}

	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (!message) {
			return;
		}

		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			CommunityShaders::Runtime::GetSingleton()->PostPostLoad();
		}
#if defined(FALLOUT_POST_AE)
		if (message->type == F4SE::MessagingInterface::kPostLoad ||
			message->type == F4SE::MessagingInterface::kPostPostLoad ||
			message->type == F4SE::MessagingInterface::kPostLoadGame) {
			RecoverExistingPostAERenderer();
		}
#endif
	}
}

#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = []() consteval {
	F4SE::PluginVersionData data{};
	fo4cs::PopulateVersionData(data);
	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	fo4cs::PopulatePluginInfo(a_info);
	return true;
}
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
#if defined(FALLOUT_POST_AE)
	F4SE::Init(a_f4se, { .trampoline = true, .trampolineSize = 16 * stl::kThunkCallTrampolineSize });
#else
	F4SE::Init(a_f4se);
	F4SE::AllocTrampoline(16 * stl::kThunkCallTrampolineSize);
#endif
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();
	fo4cs::Diagnostics::ResetHangTrace();

	logger::info("[CommunityShaders] Initializing unified Feature framework...");

	DX11Hooks::SetDeviceCreatedCallback(OnD3D11DeviceCreated);
	DX11Hooks::SetPresentCallback(OnPresent);
	DX12SwapChain::GetSingleton()->RegisterFrameCallback(OnDX12ProxyFrame);

	CommunityShaders::Runtime::GetSingleton()->Load();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	logger::info("[CommunityShaders] Plugin loaded with unified Features");
	return true;
}
