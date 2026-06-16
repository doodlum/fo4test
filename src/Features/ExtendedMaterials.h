#pragma once

#include "Core/Feature.h"

#include <RE/FO4Runtime.h>

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
			"Enables the shader define and descriptor handoff used by extended material shader variants.",
			{
				"Lighting shader descriptor integration",
				"Deferred-material define injection",
				"Foundation for parallax and complex material support"
			}
		};
	}

	[[nodiscard]] bool HasShaderDefine(std::int32_t a_shaderType) override
	{
		return a_shaderType == RE::FO4Runtime::ShaderType::kLighting;
	}
};
