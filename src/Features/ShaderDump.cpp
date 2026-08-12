#include "Features/ShaderDump.h"

#include "Core/DebugSwitches.h"
#include "Core/ShaderCache.h"

#include "SimpleIni.h"

#include <filesystem>
#include <imgui.h>

namespace
{
	constexpr auto kSection = "Settings";
	constexpr auto kDumpAllShadersKey = "bDumpAllShaders";

	const std::filesystem::path kDefaultSettingsPath{ "Data\\MCM\\Config\\CommunityShaders\\settings.ini" };
	const std::filesystem::path kUserSettingsPath{ "Data\\MCM\\Settings\\CommunityShaders.ini" };

	bool LoadIniIfExists(CSimpleIniA& a_ini, const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_path, ec)) {
			return false;
		}

		const auto result = a_ini.LoadFile(a_path.string().c_str());
		if (result < 0) {
			logger::warn("[CommunityShaders] Failed to load {} settings from {}", a_label, a_path.string());
			return false;
		}

		logger::info("[CommunityShaders] Loaded {} settings from {}", a_label, a_path.string());
		return true;
	}
}

std::pair<std::string, std::vector<std::string>> ShaderDump::GetFeatureSummary()
{
	return {
		"Captures original D3D11 shader bytecode for building runtime-specific ShaderDB data.",
		{
			"Hooks D3D11 shader creation through the Phase 0 foundation",
			"Writes PreNG dumps under Data/F4SE/Plugins/CommunityShaders/ShaderDump/PreNG",
			"Can be enabled with MCM ini or FO4CS_DUMP_SHADERS"
		}
	};
}

void ShaderDump::LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	LoadIniIfExists(ini, kDefaultSettingsPath, "default CommunityShaders");
	LoadIniIfExists(ini, kUserSettingsPath, "user CommunityShaders");

	dumpAllShaders = ini.GetBoolValue(kSection, kDumpAllShadersKey, dumpAllShaders);
	if (CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_DUMP_SHADERS")) {
		dumpAllShaders = true;
	}
}

void ShaderDump::SaveSettings()
{
	CSimpleIniA ini;
	OpenSettingsIni(ini, kUserSettingsPath);
	ini.SetBoolValue(kSection, kDumpAllShadersKey, dumpAllShaders);

	if (!SaveSettingsIni(ini, kUserSettingsPath)) {
		logger::warn("[CommunityShaders] Failed to save settings to {}", kUserSettingsPath.string());
	}
}

void ShaderDump::Load()
{
	CommunityShaders::ShaderCache::GetSingleton()->SetDumpAllShaders(dumpAllShaders);
	logger::info("[CommunityShaders] ShaderDB dump mode {}", dumpAllShaders ? "enabled" : "disabled");

	if (CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_TRACE_PIPELINE")) {
		CommunityShaders::ShaderCache::GetSingleton()->SetTracePipeline(true);
		logger::info("[CommunityShaders] Pipeline tracer enabled (Debug.ini FO4CS_TRACE_PIPELINE=1)");
		logger::info("[CommunityShaders] Traces written to Data/F4SE/Plugins/CommunityShaders/PipelineTrace/");
	}
}

void ShaderDump::DrawSettings()
{
	if (ImGui::CollapsingHeader("ShaderDB Dump")) {
		if (ImGui::Checkbox("Dump All Shaders", &dumpAllShaders)) {
			CommunityShaders::ShaderCache::GetSingleton()->SetDumpAllShaders(dumpAllShaders);
			SaveSettings();
		}

		ImGui::TextWrapped("Writes shader bytecode dumps under Data/F4SE/Plugins/CommunityShaders/ShaderDump for building ShaderDB data.");
	}
}
