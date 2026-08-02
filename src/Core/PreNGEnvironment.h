#pragma once

#include <string_view>

// PreNG runtime environment variable names and shader base names shared
// across Hooks.cpp / BSShaderHooks.cpp / ShaderCache.cpp. Single source of
// truth for the literals; the reading helpers stay local to each file
// (Hooks reads registry + process env, BSShaderHooks reads Debug.ini via
// DebugSwitches — different configuration sources).
namespace CommunityShaders::PreNGEnvironment
{
	inline constexpr std::string_view kPreNGBSLightingFxpName = "lighting";
	inline constexpr std::string_view kPreNGDFLightingFxpName = "dflight";
	inline constexpr std::string_view kPreNGDFCompositeFxpName = "dfcomposite";

	inline constexpr const char* kPreNGShaderLookupDiagEnv = "FO4CS_LLF_PRENG_SHADER_LOOKUP_DIAG";
	inline constexpr const char* kPreNGBSLightingVanillaDumpEnv = "FO4CS_LLF_PRENG_BSLIGHTING_VANILLA_DUMP";
	inline constexpr const char* kPreNGDFLightVanillaDumpEnv = "FO4CS_LLF_PRENG_DFLIGHT_VANILLA_DUMP";
	inline constexpr const char* kPreNGDFCompositeVanillaDumpEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VANILLA_DUMP";
	inline constexpr const char* kPreNGDescriptorCompileEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE";
}
