#pragma once

#include "Core/Feature.h"

class Upscaling;

struct FeatureFrameGeneration : Feature
{
	[[nodiscard]] std::string GetName() override { return "Frame Generation"; }
	[[nodiscard]] std::string GetShortName() override { return "FrameGeneration"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kRendering; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "FRAME_GENERATION"; }
	[[nodiscard]] bool IsCore() const override { return true; }

	struct Settings
	{
		bool frameGenerationMode = true;
		bool frameLimitMode = true;
		float realFrameRateLimit = 0.0f;
		int frameGenerationBackend = 1;
	} settings;

	Upscaling* upscaling = nullptr;

	void Load() override;
	void PostPostLoad() override;
	void SetupResources() override;
	void Prepass() override;
	void Reset() override;

	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;

	[[nodiscard]] std::filesystem::path GetSettingsPath() override
	{
		return std::filesystem::path("Data\\F4SE\\Plugins\\FrameGen") / "FrameGen.ini";
	}
};
