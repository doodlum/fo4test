#include "Features/Reflex.h"

#include "Streamline.h"
#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>
void FeatureReflex::Load()
{
	upscaling = Upscaling::GetSingleton();

	if (upscaling->pluginMode != Upscaling::PluginMode::kUpscaler) {
		upscaling->pluginMode = Upscaling::PluginMode::kReflex;
	}

	LoadSettings();

	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::Reflex] Loaded");
}

void FeatureReflex::PostPostLoad()
{
	if (!loaded || !upscaling) return;

	if (upscaling->UsesReflex() && Streamline::GetSingleton()->featureReflex) {
		logger::info("[Feature::Reflex] Streamline Reflex active");
	}
}

void FeatureReflex::Prepass()
{
	if (!loaded || !upscaling) return;

	if (upscaling->UsesReflex() && Streamline::GetSingleton()->featureReflex) {
		Streamline::GetSingleton()->SleepReflexFrame("[Reflex] Prepass sleep");
	}
}

void FeatureReflex::Reset() {}

void FeatureReflex::LoadSettings()
{
	upscaling = Upscaling::GetSingleton();

	CSimpleIniA ini;
	OpenSettingsIni(ini, GetSettingsPath());

	settings.reflexMode = static_cast<int>(ini.GetLongValue("Settings", "iReflexMode", settings.reflexMode));
	settings.reflexSleepMode = ini.GetBoolValue("Settings", "bReflexSleepMode", settings.reflexSleepMode);

	upscaling->settings.reflexMode = settings.reflexMode;
	upscaling->settings.reflexSleepMode = settings.reflexSleepMode;

	logger::info("[Feature::Reflex] Settings (mode={}, sleep={})",
		settings.reflexMode, settings.reflexSleepMode);
}

void FeatureReflex::SaveSettings()
{
	if (upscaling) {
		upscaling->settings.reflexMode = settings.reflexMode;
		upscaling->settings.reflexSleepMode = settings.reflexSleepMode;
		upscaling->ApplyRuntimeFallbacks();
		settings.reflexMode = upscaling->settings.reflexMode;
	}

	const auto path = GetSettingsPath();
	CSimpleIniA ini;
	OpenSettingsIni(ini, path);

	ini.SetLongValue("Settings", "iReflexMode", settings.reflexMode);
	ini.SetBoolValue("Settings", "bReflexSleepMode", settings.reflexSleepMode);

	SaveSettingsIni(ini, path);

	if (upscaling) {
		upscaling->settings.reflexMode = settings.reflexMode;
		upscaling->settings.reflexSleepMode = settings.reflexSleepMode;
	}
}

void FeatureReflex::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}

void FeatureReflex::DrawSettings()
{
	if (ImGui::CollapsingHeader("Reflex")) {
		int changed = 0;

		if (upscaling) {
			upscaling->settings.reflexMode = settings.reflexMode;
			upscaling->ApplyRuntimeFallbacks();
			settings.reflexMode = upscaling->settings.reflexMode;
			if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
				ImGui::TextWrapped("%s", reason);
			}
		}

		const char* modes[] = { "Off", "Low Latency", "Low Latency + Boost" };
		changed |= ImGui::Combo("Mode", &settings.reflexMode, modes, IM_ARRAYSIZE(modes)) ? 1 : 0;
		changed |= ImGui::Checkbox("Reflex Sleep Mode", &settings.reflexSleepMode) ? 1 : 0;

		if (changed && upscaling) {
			upscaling->settings.reflexMode = settings.reflexMode;
			upscaling->settings.reflexSleepMode = settings.reflexSleepMode;
			upscaling->ApplyRuntimeFallbacks();
			settings.reflexMode = upscaling->settings.reflexMode;
		}
	}
}
