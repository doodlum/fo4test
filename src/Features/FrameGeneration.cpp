#include "Features/FrameGeneration.h"
#include "Core/CommunityShaders.h"

#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>
void FeatureFrameGeneration::Load()
{
	upscaling = Upscaling::GetSingleton();
	LoadSettings();

	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::FrameGeneration] Loaded");
}

void FeatureFrameGeneration::PostPostLoad()
{
	if (!loaded || !upscaling) return;

	// Frame generation resources are created lazily by CheckResources()
	// during frame processing when render targets are available.
	// Calling CreateFrameGenerationResources here crashes on null main.texture.

	logger::info("[Feature::FrameGeneration] PostPostLoad complete");
}

void FeatureFrameGeneration::SetupResources()
{
	if (!loaded || !upscaling) return;
	if (!upscaling->UsesFSRFrameGeneration()) return;

#if defined(FALLOUT_PRE_NG)
	// PreNG render targets are not valid during device creation.
	// Shared resources are created lazily from Prepass/Present once targets exist.
	return;
#else
	if (auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice()) {
		upscaling->CreateFrameGenerationResources();
	}
#endif
}

void FeatureFrameGeneration::Prepass()
{
	if (!loaded || !upscaling) return;
	if (!upscaling->UsesFSRFrameGeneration()) return;

	upscaling->CopyBuffersToSharedResources();
}

void FeatureFrameGeneration::Reset()
{
	// FrameGeneration resources are managed by Upscaling singleton
}

void FeatureFrameGeneration::LoadSettings()
{
	upscaling = Upscaling::GetSingleton();

	CSimpleIniA ini;
	OpenSettingsIni(ini, GetSettingsPath());

	settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", settings.frameGenerationMode);
	settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", settings.frameLimitMode);
	settings.realFrameRateLimit = static_cast<float>(ini.GetDoubleValue("Settings", "fRealFrameRateLimit", settings.realFrameRateLimit));
	settings.frameGenerationBackend = static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", Upscaling::kFrameGenerationBackendDLSS));

	// Sync to shared Upscaling singleton
	upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
	upscaling->settings.frameLimitMode = settings.frameLimitMode;
	upscaling->settings.realFrameRateLimit = settings.realFrameRateLimit;
	upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
	upscaling->ApplyRuntimeFallbacks();
	settings.frameGenerationMode = upscaling->settings.frameGenerationMode;
	settings.frameLimitMode = upscaling->settings.frameLimitMode;
	settings.realFrameRateLimit = upscaling->settings.realFrameRateLimit;
	settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;

	logger::info("[Feature::FrameGeneration] Settings (enabled={}, limiter={}, realFPS={}, backend={})",
		settings.frameGenerationMode, settings.frameLimitMode, settings.realFrameRateLimit, settings.frameGenerationBackend);
}

void FeatureFrameGeneration::SaveSettings()
{
	if (upscaling) {
		upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
		upscaling->settings.frameLimitMode = settings.frameLimitMode;
		upscaling->settings.realFrameRateLimit = settings.realFrameRateLimit;
		upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
		upscaling->ApplyRuntimeFallbacks();
		settings.frameGenerationMode = upscaling->settings.frameGenerationMode;
		settings.frameLimitMode = upscaling->settings.frameLimitMode;
		settings.realFrameRateLimit = upscaling->settings.realFrameRateLimit;
		settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
	}

	const auto path = GetSettingsPath();
	CSimpleIniA ini;
	OpenSettingsIni(ini, path);

	ini.SetBoolValue("Settings", "bFrameGenerationMode", settings.frameGenerationMode);
	ini.SetBoolValue("Settings", "bFrameLimitMode", settings.frameLimitMode);
	ini.SetDoubleValue("Settings", "fRealFrameRateLimit", settings.realFrameRateLimit);
	ini.SetLongValue("Settings", "iFrameGenerationBackend", settings.frameGenerationBackend);

	SaveSettingsIni(ini, path);

	if (upscaling) {
		upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
		upscaling->settings.frameLimitMode = settings.frameLimitMode;
		upscaling->settings.realFrameRateLimit = settings.realFrameRateLimit;
		upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
	}
}

void FeatureFrameGeneration::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}

void FeatureFrameGeneration::DrawSettings()
{
	if (ImGui::CollapsingHeader("Frame Generation")) {
		int changed = 0;

		changed |= ImGui::Checkbox("Enabled", &settings.frameGenerationMode) ? 1 : 0;
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Frame Limit", &settings.frameLimitMode) ? 1 : 0;
		changed |= ImGui::InputFloat("Real Rendered FPS", &settings.realFrameRateLimit, 1.0f, 10.0f, "%.1f") ? 1 : 0;
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("0 = automatic policy. Verified untied physics is unlimited; otherwise the safety cap is 60 FPS. Positive values are clamped to 30-1000 FPS.");
		}

		if (upscaling) {
			upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
			upscaling->ApplyRuntimeFallbacks();
			settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
			if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
				ImGui::TextWrapped("%s", reason);
			}
		}

		const char* backends[] = { "NVIDIA DLSS-G", "AMD FSR FG" };
		int backendIndex = settings.frameGenerationBackend == Upscaling::kFrameGenerationBackendFSR ? 1 : 0;
		if (ImGui::Combo("Backend", &backendIndex, backends, IM_ARRAYSIZE(backends))) {
			settings.frameGenerationBackend = backendIndex == 0 ? Upscaling::kFrameGenerationBackendDLSS : Upscaling::kFrameGenerationBackendFSR;
			changed = 1;
		}

		if (changed && upscaling) {
			upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
			upscaling->settings.frameLimitMode = settings.frameLimitMode;
			upscaling->settings.realFrameRateLimit = settings.realFrameRateLimit;
			upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
			upscaling->ApplyRuntimeFallbacks();
			settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
			settings.realFrameRateLimit = upscaling->settings.realFrameRateLimit;
		}
	}
}
