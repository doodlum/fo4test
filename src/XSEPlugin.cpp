
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#ifndef NDEBUG
#pragma comment(lib, "dxguid.lib")
#endif

#include "DX11Hooks.h"
#include "Upscaling.h"

void InitializeLog()
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = spdlog::level::info;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%v"s);
}

#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.PluginVersion(Plugin::VERSION);
	data.PluginName(Plugin::NAME.data());
	data.AuthorName("");
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	data.CompatibleVersions({ F4SE::RUNTIME_LATEST });

	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	a_info->name = Plugin::NAME.data();
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->version = 0;
	return true;
}
#endif

void AddDebugInformation()
{
	auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	for (uint32_t i = 0; i < 101; i++) {
		if (auto texture = rendererData->renderTargets[i].texture) {
			auto name = std::format("RT {}", i);
			texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto texture = rendererData->renderTargets[i].copyTexture) {
			auto name = std::format("COPY RT {}", i);
			texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto srView = rendererData->renderTargets[i].srView) {
			auto name = std::format("SRV {}", i);
			srView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto copySRView = rendererData->renderTargets[i].copySRView) {
			auto name = std::format("COPY SRV {}", i);
			copySRView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto rtView = rendererData->renderTargets[i].rtView) {
			auto name = std::format("RTV {}", i);
			rtView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto uaView = rendererData->renderTargets[i].uaView) {
			auto name = std::format("UAV {}", i);
			uaView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
	}

	for (uint32_t i = 0; i < 13; i++) {
		if (auto texture = rendererData->depthStencilTargets[i].texture) {
			auto name = std::format("DEPTH RT {}", i);
			texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
		if (auto srViewDepth = rendererData->depthStencilTargets[i].srViewDepth) {
			auto name = std::format("DS VIEW {}", i);
			srViewDepth->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
		}
	}
}

void OnInit(F4SE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case F4SE::MessagingInterface::kGameDataReady:
	{
		logger::info("Data loaded");
		Upscaling::GetSingleton()->OnDataLoaded();
#ifndef NDEBUG
		AddDebugInformation();
#endif
	}
	break;
	default:
		break;
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

#ifndef NDEBUG
#	if defined(FALLOUT_POST_NG)
	while (!REX::W32::IsDebuggerPresent()) {};
#	else
	while (!IsDebuggerPresent()) {};
#	endif
#endif

	InitializeLog();

	DX11Hooks::Install();
	Upscaling::InstallHooks();

	const auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(OnInit);

	return true;
}
