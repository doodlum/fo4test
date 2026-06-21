#include "Features/ExtendedMaterials.h"

#include "SimpleIni.h"

#include <algorithm>
#include <filesystem>
#include <imgui.h>

namespace
{
	constexpr auto kSection = "Settings";
	constexpr auto kEnableComplexMaterial = "bEnableComplexMaterial";
	constexpr auto kEnableParallax = "bEnableParallax";
	constexpr auto kEnableShadows = "bEnableShadows";
	constexpr auto kExtendShadows = "bExtendShadows";
	constexpr auto kDisplacementScale = "fDisplacementScale";
	constexpr auto kDisplacementOffset = "fDisplacementOffset";
	constexpr auto kHeightScale = "fHeightScale";

	std::uint32_t ToFlag(bool a_value)
	{
		return a_value ? 1u : 0u;
	}

	bool DrawFlagCheckbox(const char* a_label, std::uint32_t& a_value)
	{
		bool enabled = a_value != 0;
		if (!ImGui::Checkbox(a_label, &enabled)) {
			return false;
		}

		a_value = ToFlag(enabled);
		return true;
	}
}

void FeatureExtendedMaterials::DataLoaded()
{
	// FO4 terrain parallax support is not wired yet; no runtime INI toggle is forced here.
}

void FeatureExtendedMaterials::DrawSettings()
{
	bool changed = false;

	if (ImGui::CollapsingHeader("Extended Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNodeEx("Complex Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawFlagCheckbox("Enable Complex Material", settings.EnableComplexMaterial);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Uses verified material texture channels to derive roughness and metallic output for deferred surfaces.");
			}

			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Parallax", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawFlagCheckbox("Enable Parallax", settings.EnableParallax);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Enables parallax occlusion mapping helpers for surfaces with verified height data.");
			}

			changed |= ImGui::SliderFloat("Displacement Scale", &settings.DisplacementScale, 0.0f, 0.2f, "%.4f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Overall strength of the parallax displacement effect.");
			}

			changed |= ImGui::SliderFloat("Displacement Offset", &settings.DisplacementOffset, -0.1f, 0.0f, "%.4f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Shifts the height range. Negative values push surfaces inward.");
			}

			changed |= ImGui::SliderFloat("Height Scale", &settings.HeightScale, 0.1f, 3.0f, "%.2f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Multiplier for height map intensity.");
			}

			ImGui::Spacing();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Approximate Soft Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= DrawFlagCheckbox("Enable Shadows", settings.EnableShadows);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Enables cheap soft shadows when using parallax.\nApplies to directional and point-light paths once wired.");
			}

			changed |= DrawFlagCheckbox("Extend Shadows", settings.ExtendShadows);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Extends parallax shadows beyond the parallax range. Small performance impact.");
			}

			ImGui::Spacing();
			ImGui::TreePop();
		}
	}

	if (changed) {
		settings.DisplacementScale = std::clamp(settings.DisplacementScale, 0.0f, 0.2f);
		settings.DisplacementOffset = std::clamp(settings.DisplacementOffset, -0.1f, 0.0f);
		settings.HeightScale = std::clamp(settings.HeightScale, 0.1f, 3.0f);
		SaveSettings();
	}
}

void FeatureExtendedMaterials::LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const auto path = GetSettingsPath();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		ini.LoadFile(path.string().c_str());
	}

	settings.EnableComplexMaterial = ToFlag(ini.GetBoolValue(kSection, kEnableComplexMaterial, settings.EnableComplexMaterial != 0));
	settings.EnableParallax = ToFlag(ini.GetBoolValue(kSection, kEnableParallax, settings.EnableParallax != 0));
	settings.EnableShadows = ToFlag(ini.GetBoolValue(kSection, kEnableShadows, settings.EnableShadows != 0));
	settings.ExtendShadows = ToFlag(ini.GetBoolValue(kSection, kExtendShadows, settings.ExtendShadows != 0));
	settings.DisplacementScale = static_cast<float>(ini.GetDoubleValue(kSection, kDisplacementScale, settings.DisplacementScale));
	settings.DisplacementOffset = static_cast<float>(ini.GetDoubleValue(kSection, kDisplacementOffset, settings.DisplacementOffset));
	settings.HeightScale = static_cast<float>(ini.GetDoubleValue(kSection, kHeightScale, settings.HeightScale));

	settings.DisplacementScale = std::clamp(settings.DisplacementScale, 0.0f, 0.2f);
	settings.DisplacementOffset = std::clamp(settings.DisplacementOffset, -0.1f, 0.0f);
	settings.HeightScale = std::clamp(settings.HeightScale, 0.1f, 3.0f);
}

void FeatureExtendedMaterials::SaveSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const auto path = GetSettingsPath();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		ini.LoadFile(path.string().c_str());
	}

	ini.SetBoolValue(kSection, kEnableComplexMaterial, settings.EnableComplexMaterial != 0);
	ini.SetBoolValue(kSection, kEnableParallax, settings.EnableParallax != 0);
	ini.SetBoolValue(kSection, kEnableShadows, settings.EnableShadows != 0);
	ini.SetBoolValue(kSection, kExtendShadows, settings.ExtendShadows != 0);
	ini.SetDoubleValue(kSection, kDisplacementScale, settings.DisplacementScale);
	ini.SetDoubleValue(kSection, kDisplacementOffset, settings.DisplacementOffset);
	ini.SetDoubleValue(kSection, kHeightScale, settings.HeightScale);

	std::filesystem::create_directories(path.parent_path(), ec);
	ini.SaveFile(path.string().c_str());
}

void FeatureExtendedMaterials::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}
