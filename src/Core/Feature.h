#pragma once

#include "Core/FeatureCategories.h"
#include "Core/FeatureConstraints.h"
#include "Core/FeatureVersions.h"
#include "Core/IMenuItem.h"

#include "SimpleIni.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Feature : IMenuItem
{
	struct SettingSearchEntry
	{
		std::string label;
		std::string description;
		std::function<void()> focusCallback;
		std::string featureName;
	};

	Feature();

	bool loaded = false;
	std::string version;
	std::string failedLoadedMessage;

	// --- IMenuItem overrides ---
	[[nodiscard]] std::string GetName() override = 0;
	[[nodiscard]] std::string GetShortName() override = 0;

	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kOther; }
	[[nodiscard]] bool IsLoaded() override { return loaded; }
	[[nodiscard]] std::string GetVersion() override { return version; }
	void DrawSettings() override {}
	void DrawOverlay() override {}
	[[nodiscard]] std::vector<SearchEntry> GetSearchEntries() override { return {}; }

	// --- Feature-specific ---
	[[nodiscard]] virtual std::string GetFeatureModLink() { return {}; }
	[[nodiscard]] virtual std::string_view GetShaderDefineName() { return {}; }
	[[nodiscard]] virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() { return {}; }
	[[nodiscard]] virtual std::vector<SettingSearchEntry> GetSettingsSearchEntries() { return {}; }

	static std::vector<Feature*>& GetFeatureList();
	[[nodiscard]] virtual std::vector<FeatureConstraints::Constraint> GetActiveConstraints() { return {}; }
	[[nodiscard]] virtual bool HasShaderDefine(std::int32_t) { return false; }
	[[nodiscard]] virtual bool SupportsVR() { return false; }
	[[nodiscard]] virtual bool IsCore() const { return false; }
	[[nodiscard]] virtual bool IsInMenu() const { return true; }
	[[nodiscard]] virtual bool DrawFailLoadMessage() const { return true; }
	[[nodiscard]] virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() { return {}; }

	// Settings persistence: Data/F4SE/Plugins/CommunityShaders/{ShortName}.ini
	[[nodiscard]] virtual std::filesystem::path GetSettingsPath()
	{
		return std::filesystem::path("Data\\F4SE\\Plugins\\CommunityShaders") / (GetShortName() + ".ini");
	}

	virtual void SetupResources() {}
	virtual void Reset() {}
	virtual void DrawUnloadedUI();

	virtual void ReflectionsPrepass() {}
	virtual void Prepass() {}
	virtual void EarlyPrepass() {}

	virtual void Load() {}
	virtual void DataLoaded() {}
	virtual void PostPostLoad() {}

	virtual void SaveSettings() {}
	virtual void LoadSettings() {}
	virtual void RestoreDefaultSettings() {}
	virtual bool ToggleAtBootSetting();
	virtual bool ReapplyOverrideSettings();

protected:
	// Settings INI plumbing shared by the Feature LoadSettings/SaveSettings
	// implementations. OpenSettingsIni preserves any existing keys on disk;
	// SaveSettingsIni creates the parent directory and writes the file.
	void OpenSettingsIni(CSimpleIniA& a_ini, const std::filesystem::path& a_path) const;
	bool SaveSettingsIni(const CSimpleIniA& a_ini, const std::filesystem::path& a_path) const;

public:
	template <typename Func>
	static inline void ForEachLoadedFeature(std::string_view /*a_methodName*/, Func&& a_callback)
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				a_callback(feature);
			}
		}
	}
};
