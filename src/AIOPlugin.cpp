#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "DX12SwapChain.h"
#include "Upscaler.h"
#include "Overlay/Overlay.h"

#include <OverlayAPI.h>
#include <SimpleIni.h>
#include <imgui.h>

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			Upscaling::GetSingleton()->PostPostLoad();
		}
	}
}

// Panel callbacks — all feature panels registered when running in AIO mode
namespace AIOOverlay
{
	void DrawDLSSRuntimeNotice()
	{
		auto* upscaling = Upscaling::GetSingleton();
		upscaling->ApplyRuntimeFallbacks();
		if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
			ImGui::TextWrapped("%s", reason);
		}
	}

	int DrawFrameGenerationBackendCombo(int& backend)
	{
		const char* fgBackends[] = { "NVIDIA DLSS-G", "AMD FSR FG" };
		int backendIndex = backend == Upscaling::kFrameGenerationBackendFSR ? 1 : 0;
		if (ImGui::Combo("Backend", &backendIndex, fgBackends, IM_ARRAYSIZE(fgBackends))) {
			backend = backendIndex == 0 ? Upscaling::kFrameGenerationBackendDLSS : Upscaling::kFrameGenerationBackendFSR;
			return 1;
		}
		return 0;
	}

	template <int PanelId>
	int RenderPanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		int changed = 0;

		if constexpr (PanelId == 0) { // Frame Generation
			if (ImGui::CollapsingHeader("Frame Generation")) {
				changed |= ImGui::Checkbox("Enabled", &s.frameGenerationMode) ? 1 : 0;
				ImGui::SameLine();
				changed |= ImGui::Checkbox("Frame Limit", &s.frameLimitMode) ? 1 : 0;
				changed |= ImGui::InputFloat("Real Rendered FPS", &s.realFrameRateLimit, 1.0f, 10.0f, "%.1f") ? 1 : 0;
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("0 = automatic policy; positive values limit real rendered FPS (30-1000).");
				}
				DrawDLSSRuntimeNotice();
				changed |= DrawFrameGenerationBackendCombo(s.frameGenerationBackend);
			}
		} else if constexpr (PanelId == 1) { // Reflex
			if (ImGui::CollapsingHeader("Reflex")) {
				DrawDLSSRuntimeNotice();
				const char* reflexModes[] = { "Off", "Low Latency", "Low Latency + Boost" };
				changed |= ImGui::Combo("Mode", &s.reflexMode, reflexModes, IM_ARRAYSIZE(reflexModes)) ? 1 : 0;
				changed |= ImGui::Checkbox("Reflex Sleep Mode", &s.reflexSleepMode) ? 1 : 0;
			}
		} else if constexpr (PanelId == 2) { // Upscaler
			if (ImGui::CollapsingHeader("Upscaler")) {
				DrawDLSSRuntimeNotice();
				const char* upscaleMethods[] = { "Disabled", "FSR", "DLSS" };
				changed |= ImGui::Combo("Method", &s.upscaleMethodPreference, upscaleMethods, IM_ARRAYSIZE(upscaleMethods)) ? 1 : 0;
				const char* qualityModes[] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };
				changed |= ImGui::Combo("Quality", &s.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes)) ? 1 : 0;
			}
		}
		if (changed) {
			Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
		}
		return changed;
	}

	template <int PanelId>
	void SavePanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
		CSimpleIniA ini;
		ini.SetUnicode();

		if constexpr (PanelId == 0) {
			ini.SetValue("Settings", "bFrameGenerationMode", s.frameGenerationMode ? "true" : "false");
			ini.SetValue("Settings", "bFrameLimitMode", s.frameLimitMode ? "true" : "false");
			ini.SetDoubleValue("Settings", "fRealFrameRateLimit", s.realFrameRateLimit);
			ini.SetValue("Settings", "iFrameGenerationBackend", std::to_string(s.frameGenerationBackend).c_str());
			ini.SaveFile("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.ini");
		} else if constexpr (PanelId == 1) {
			ini.SetValue("Settings", "iReflexMode", std::to_string(s.reflexMode).c_str());
			ini.SetValue("Settings", "bReflexSleepMode", s.reflexSleepMode ? "true" : "false");
			ini.SaveFile("Data\\F4SE\\Plugins\\Reflex\\Reflex.ini");
		} else if constexpr (PanelId == 2) {
			ini.SetValue("Settings", "iUpscaleMethodPreference", std::to_string(s.upscaleMethodPreference).c_str());
			ini.SetValue("Settings", "iQualityMode", std::to_string(s.qualityMode).c_str());
			ini.SetValue("Settings", "iDLSSPreset", std::to_string(s.dlssPreset).c_str());
			ini.SaveFile("Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini");
		}
	}

	template <int PanelId>
	void TryRegister(const char* name, int category)
	{
		static OverlayPanelCallbacks cbs;
		cbs.render = RenderPanel<PanelId>;
		cbs.save = SavePanel<PanelId>;
		cbs.userData = &Upscaling::GetSingleton()->settings;

		Overlay::GetSingleton()->RegisterPanel(name, category, &cbs);
	}

	void RegisterAll()
	{
		TryRegister<0>("Frame Generation", kOverlayCategory_Rendering);
		TryRegister<1>("Reflex", kOverlayCategory_Latency);
		TryRegister<2>("Upscaler", kOverlayCategory_Rendering);
	}

	void RegisterHostCallbacks()
	{
		auto* dx12 = DX12SwapChain::GetSingleton();
		dx12->RegisterOverlayInitCallback(Overlay::OnSwapChainCreated);
		dx12->RegisterOverlayPresentCallback(Overlay::OnPresent);
		dx12->RegisterOverlayPollCallback(Overlay::OnPollHotkey);
		logger::info("[AIO] Overlay callbacks registered directly");
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

	auto upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kUpscaler;
	upscaling->LoadSettings();

	logger::info("[AIO] Upscaler DLSS={} FrameGen={} Reflex={}",
		static_cast<int>(upscaling->settings.upscaleMethodPreference),
		static_cast<int>(upscaling->settings.frameGenerationMode),
		upscaling->settings.reflexMode);

	AIOOverlay::RegisterHostCallbacks();
	DX11Hooks::Install();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	AIOOverlay::RegisterAll();
	return true;
}
