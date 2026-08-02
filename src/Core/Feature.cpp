#include "Core/Feature.h"
#include "Core/Globals.h"
#include "Core/MenuRegistry.h"

Feature::Feature()
{
	MenuRegistry::Register(this);
}

void Feature::DrawUnloadedUI()
{
	if (!failedLoadedMessage.empty()) {
		logger::warn("[CommunityShaders] {} unloaded: {}", GetName(), failedLoadedMessage);
	}
}

bool Feature::ToggleAtBootSetting()
{
	return false;
}

bool Feature::ReapplyOverrideSettings()
{
	return false;
}

std::vector<Feature*>& Feature::GetFeatureList()
{
	return CommunityShaders::GetFeatureList();
}

void Feature::OpenSettingsIni(CSimpleIniA& a_ini, const std::filesystem::path& a_path) const
{
	a_ini.SetUnicode();

	std::error_code ec;
	if (std::filesystem::exists(a_path, ec)) {
		a_ini.LoadFile(a_path.string().c_str());
	}
}

bool Feature::SaveSettingsIni(const CSimpleIniA& a_ini, const std::filesystem::path& a_path) const
{
	std::error_code ec;
	std::filesystem::create_directories(a_path.parent_path(), ec);
	if (ec) {
		logger::warn("[Feature] Failed to create settings directory {}: {}", a_path.parent_path().string(), ec.message());
		return false;
	}
	return a_ini.SaveFile(a_path.string().c_str()) >= 0;
}
