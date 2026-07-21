#include "PluginCommon.h"

#include "Core/CommunityShaders.h"

#include "Core/Globals.h"
#include "Diagnostics/HangTrace.h"
#include "DX11Hooks.h"
#include "DX12SwapChain.h"
#include "Presentation/PresentationBackend.h"

#include <d3d11.h>
#include <dxgi.h>

namespace
{
	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		CommunityShaders::Presentation::Coordinator::Get().OnD3D11DeviceCreated(a_device);
	}

	void OnPresent(IDXGISwapChain* a_swapChain)
	{
		CommunityShaders::Presentation::Coordinator::Get().OnD3D11Present(a_swapChain);
	}

	void OnDX12ProxyFrame()
	{
		CommunityShaders::Presentation::Coordinator::Get().OnD3D12ProxyFrame();
	}

	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (!message) {
			return;
		}

		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			CommunityShaders::Runtime::GetSingleton()->PostPostLoad();
		}
		if (message->type == F4SE::MessagingInterface::kPostLoad ||
			message->type == F4SE::MessagingInterface::kPostPostLoad ||
			message->type == F4SE::MessagingInterface::kPostLoadGame) {
			CommunityShaders::Presentation::Coordinator::Get().TryRecoverExistingRenderer();
		}
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
