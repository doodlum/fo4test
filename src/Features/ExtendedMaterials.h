#pragma once

#include "Core/Feature.h"

#include <RE/FO4Runtime.h>

#include <cstdint>

struct FeatureExtendedMaterials : Feature
{
	[[nodiscard]] std::string GetName() override { return "Extended Materials"; }
	[[nodiscard]] std::string GetShortName() override { return "ExtendedMaterials"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
	[[nodiscard]] bool IsCore() const override { return true; }
	[[nodiscard]] bool SupportsVR() override { return true; }

	[[nodiscard]] std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Extended Materials adds advanced material effects including parallax occlusion mapping and complex material blending.",
			{
				"Parallax occlusion mapping for depth",
				"Complex material roughness and metallic output",
				"Parallax soft shadow helpers"
			}
		};
	}

	[[nodiscard]] bool HasShaderDefine(std::int32_t a_shaderType) override
	{
		return RE::FO4Runtime::IsLightingShaderType(a_shaderType);
	}

	struct alignas(16) Settings
	{
		std::uint32_t EnableComplexMaterial = 1;
		std::uint32_t EnableParallax = 1;
		std::uint32_t EnableShadows = 1;
		std::uint32_t ExtendShadows = 1;
		float DisplacementScale = 0.05f;
		float DisplacementOffset = -0.025f;
		float HeightScale = 1.0f;
		float pad = 0.0f;
	};
	static_assert(sizeof(Settings) == 32, "Settings must be 32 bytes");
	static_assert(alignof(Settings) == 16, "Settings must be 16-byte aligned");

	Settings settings;

	void DataLoaded() override;
	void DrawSettings() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
};
