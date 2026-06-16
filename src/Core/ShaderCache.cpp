#include "Core/ShaderCache.h"

#include "Core/CommunityShaders.h"
#include "Core/Feature.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif
#include <RE/FO4Runtime.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

namespace CommunityShaders
{
	namespace
	{
		constexpr const char* kDescriptorCompileEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE";
#if defined(FALLOUT_PRE_NG)
		namespace F4Runtime = RE::FO4Runtime;
		constexpr std::int32_t kPreNGBSLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE);
		constexpr std::int32_t kPreNGDFLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE);
		constexpr std::int32_t kPreNGDFCompositeShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_COMPOSITE_SHADER_TYPE);
		constexpr std::string_view kPreNGBSLightingFxpName = "lighting";
		constexpr std::string_view kPreNGDFLightingFxpName = "dflight";
		constexpr std::string_view kPreNGDFCompositeFxpName = "dfcomposite";
		constexpr const char* kPreNGBSLightingContractCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONTRACT_COMPILE";
		constexpr const char* kPreNGBSLightingConsumerCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONSUMER_COMPILE";
		constexpr const char* kPreNGBSLightingLLFBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND";
		constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
		constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
		constexpr const char* kPreNGDFLightFullContractVisibleLLFEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF";
		constexpr const char* kPreNGDFLightFullContractVisibleMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS";
		constexpr const char* kPreNGDFLightFullContractVisibleStrictMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS";
		constexpr const char* kPreNGDFLightFullContractVisibleClusterMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS";
		constexpr const char* kPreNGDFCompositeDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_COMPILE";
		constexpr const char* kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
		constexpr const char* kPreNGDFCompositeFogSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_FOG_SAFE_BIND";
		constexpr const char* kPreNGDFCompositeVisibleLLFEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF";
		constexpr const char* kPreNGDFCompositeVisibleLLFScale1024Env = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024";
		constexpr const char* kPreNGDFCompositeVisibleLLFMaxLightsEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS";
#else
		constexpr std::int32_t kPreNGBSLightingShaderType = 8;
#endif
		constexpr std::string_view kPreNGBSLightingContractProbeSource = "LightLimitFix/BSLightingContractProbePS.hlsl";
		constexpr std::string_view kPreNGBSLightingConsumerProbeSource = "LightLimitFix/BSLightingLLFConsumerProbePS.hlsl";
		constexpr std::string_view kPreNGBSLightingLLFConsumerVisibleSource = "LightLimitFix/BSLightingLLFConsumerPS.hlsl";
		constexpr std::string_view kPreNGDFLightFullContractDescriptorSource = "LightLimitFix/DFLightFullContractPS.hlsl";
		constexpr std::string_view kPreNGDFLightFullShadowedDescriptorSource = "LightLimitFix/DFLightFullShadowedPS.hlsl";
		constexpr std::string_view kPreNGDFCompositeDescriptorProbeSource = "LightLimitFix/DFCompositeContractProbePS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla40Source = "LightLimitFix/DFCompositeVanilla40PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla88Source = "LightLimitFix/DFCompositeVanilla88PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla10040Source = "LightLimitFix/DFCompositeVanilla10040PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla10088Source = "LightLimitFix/DFCompositeVanilla10088PS.hlsl";

#if defined(FALLOUT_POST_NG)
		REX::W32::ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11VertexShader*>(a_shader);
		}

		REX::W32::ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11PixelShader*>(a_shader);
		}
#else
		ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return a_shader;
		}

		ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return a_shader;
		}
#endif

		bool IsTruthyDescriptorEnvironmentValue(const char* a_value)
		{
			return std::strcmp(a_value, "1") == 0 ||
			       std::strcmp(a_value, "true") == 0 ||
			       std::strcmp(a_value, "TRUE") == 0 ||
			       std::strcmp(a_value, "on") == 0 ||
			       std::strcmp(a_value, "ON") == 0;
		}

		bool ReadDescriptorRegistryValue(const char* a_name, HKEY a_root, const char* a_subKey, char (&a_value)[16])
		{
			DWORD type = 0;
			DWORD size = static_cast<DWORD>(sizeof(a_value));
			const auto result = RegGetValueA(
				a_root,
				a_subKey,
				a_name,
				RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
				&type,
				a_value,
				&size);
			if (result != ERROR_SUCCESS || size == 0) {
				return false;
			}

			a_value[sizeof(a_value) - 1] = '\0';
			return true;
		}

		bool ReadDescriptorEnvironmentValue(const char* a_name, char (&a_value)[16])
		{
			if (ReadDescriptorRegistryValue(a_name, HKEY_CURRENT_USER, "Environment", a_value)) {
				return true;
			}

			if (ReadDescriptorRegistryValue(
					a_name,
					HKEY_LOCAL_MACHINE,
					"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
					a_value)) {
				return true;
			}

			SetLastError(ERROR_SUCCESS);
			const auto length = GetEnvironmentVariableA(
				a_name,
				a_value,
				static_cast<DWORD>(sizeof(a_value)));
			if (length > 0) {
				return length < sizeof(a_value);
			}
			if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
				return false;
			}

			return false;
		}

		bool ReadDescriptorEnvironmentSwitch(const char* a_name)
		{
			char value[16]{};
			return ReadDescriptorEnvironmentValue(a_name, value) && IsTruthyDescriptorEnvironmentValue(value);
		}

		std::uint32_t ReadDescriptorEnvironmentUInt(
			const char* a_name,
			std::uint32_t a_defaultValue,
			std::uint32_t a_minValue,
			std::uint32_t a_maxValue)
		{
			char value[16]{};
			if (!ReadDescriptorEnvironmentValue(a_name, value)) {
				return a_defaultValue;
			}

			char* end = nullptr;
			const auto parsed = std::strtoul(value, &end, 10);
			if (end == value || *end != '\0') {
				return a_defaultValue;
			}

			const auto clamped = std::clamp(parsed, static_cast<unsigned long>(a_minValue), static_cast<unsigned long>(a_maxValue));
			return static_cast<std::uint32_t>(clamped);
		}

		bool ReadDescriptorCompileSwitch()
		{
			return ReadDescriptorEnvironmentSwitch(kDescriptorCompileEnv);
		}

#if defined(FALLOUT_PRE_NG)
		bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
				const bool unsafeOverride = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				if (requested && !unsafeOverride) {
					logger::warn(
						"[ShaderCache] PreNG DFLight full-shadowed descriptor compile held; DFLightFullShadowedPS is not vanilla-equivalent and can black out sky-light-only views. Set {}=1 only for focused diagnostics.",
						kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				}
				return requested && unsafeOverride;
			}();
			return enabled;
		}

		bool ShouldEnablePreNGDFLightFullContractVisibleLLF()
		{
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullContractVisibleLLFEnv);
			return enabled;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleStrictMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleStrictMaxLightsEnv,
				15,
				0,
				15);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleClusterMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleClusterMaxLightsEnv,
				64,
				0,
				64);
			return maxLights;
		}

		bool IsPreNGDFCompositeVisibleLLFDescriptor(std::uint32_t a_descriptor)
		{
			return a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
		}

		bool ShouldEnablePreNGDFCompositeVisibleLLF()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
				if (requested) {
					logger::warn(
						"[ShaderCache] PreNG DFComposite visible LLF proof active; this screen-space contribution is diagnostic-only and uses the low-risk 0x88/0x10088 DFComposite consumers.");
				}
				return requested;
			}();
			return enabled;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFScale1024()
		{
			static const auto scale = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFScale1024Env,
				16,
				0,
				1024);
			return scale;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}
#endif

		std::string ToLowerAscii(std::string a_value)
		{
			std::ranges::transform(a_value, a_value.begin(), [](unsigned char a_ch) {
				return static_cast<char>(std::tolower(a_ch));
			});
			return a_value;
		}

		bool StartsWith(std::string_view a_value, std::string_view a_prefix) noexcept
		{
			return a_value.size() >= a_prefix.size() &&
			       a_value.substr(0, a_prefix.size()) == a_prefix;
		}

		bool IsPreNGDFLightFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == kPreNGDFLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGBSLightingFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == kPreNGBSLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFCompositeFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == kPreNGDFCompositeFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFLightFullShadowedDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor)
		{
			return a_descriptor == 0x00000001u ||
			       a_descriptor == 0x00000101u ||
			       a_descriptor == 0x00000111u ||
			       a_descriptor == 0x00000141u ||
			       a_descriptor == 0x00000201u;
		}

		bool IsPreNGBSLightingContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGBSLightingShaderType &&
			       IsPreNGBSLightingFxpName(a_normalizedFxpFilename) &&
			       IsPreNGBSLightingContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingContractShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFLightFullContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFCompositeContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGDFCompositeDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeDescriptorCompileEnv);
			return enabled &&
			       IsPreNGDFCompositeContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldEnablePreNGDFCompositeFogSafeBindShader()
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeFogSafeBindEnv);
			return enabled;
#else
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla40Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla88Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10040Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10088Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanillaSafeBindShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return ShouldUsePreNGDFCompositeVanilla40Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla88Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10040Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10088Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
		}

		bool IsPreNGDFLightLLFConsumerDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       (F4Runtime::PreNG::IsDFLightLLFConsumerPixelDescriptor(a_descriptor) ||
			        (ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
			         F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor)));
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool CanActivelyCompilePreNGLightingDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return a_shaderType == kPreNGBSLightingShaderType ||
			       IsPreNGDFLightLLFConsumerDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor) ||
			       IsPreNGDFCompositeContractDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor);
		}

		bool CanCompileDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return ReadDescriptorCompileSwitch() ||
			       ShouldCompilePreNGBSLightingContractShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGBSLightingConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanillaSafeBindShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGDFCompositeDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
		}

		std::string BuildFeatureDefineList(const std::vector<D3D_SHADER_MACRO>& a_defines)
		{
			std::string result;
			for (const auto& define : a_defines) {
				if (!define.Name) {
					break;
				}
				if (!result.empty()) {
					result += ',';
				}
				result += define.Name;
			}
			return result.empty() ? "<none>" : result;
		}

		struct DescriptorDefineSet
		{
			std::vector<std::pair<std::string, std::string>> storage;
			std::vector<D3D_SHADER_MACRO> macros;
		};

		DescriptorDefineSet BuildDescriptorDefineSet(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::uint32_t a_descriptor)
		{
			DescriptorDefineSet result;

			for (auto* feature : Feature::GetFeatureList()) {
				if (!feature || !feature->loaded) {
					continue;
				}

				const auto name = feature->GetShaderDefineName();
				if (!name.empty()) {
					result.storage.emplace_back(std::string{ name }, "1");
				}
			}

			result.storage.emplace_back("FO4CS_DESCRIPTOR_SHADER", "1");
			result.storage.emplace_back("FO4CS_SHADER_TYPE", std::to_string(a_shaderType));
			result.storage.emplace_back("FO4CS_SHADER_DESCRIPTOR", std::to_string(a_descriptor));
			result.storage.emplace_back(
				a_stage == ShaderStage::Vertex ? "FO4CS_DESCRIPTOR_VERTEX_SHADER" : "FO4CS_DESCRIPTOR_PIXEL_SHADER",
				"1");

#if defined(FALLOUT_PRE_NG)
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGBSLightingShaderType &&
				(a_descriptor & F4Runtime::ShaderDescriptorFlags::kDeferredLightingPixel) != 0) {
				result.storage.emplace_back("DEFERRED", "1");
				result.storage.emplace_back("FO4CS_DEFERRED_LIGHTING_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingContractShader(
					a_stage,
					a_shaderType,
					kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_CONTRACT_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingConsumerShader(
					a_stage,
					a_shaderType,
					kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
			}
			if (ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
					a_stage,
					a_shaderType,
					kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_VISIBLE_CONSUMER", "1");
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFLightingShaderType &&
				F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_DESCRIPTOR", "1");
				if (ShouldEnablePreNGDFLightFullContractVisibleLLF()) {
					result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleStrictMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleClusterMaxLights()));
				}
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFCompositeShaderType &&
				F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFCOMPOSITE_CONTRACT_DESCRIPTOR", "1");
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40 &&
					ShouldUsePreNGDFCompositeVanilla40Shader(
						a_stage,
						a_shaderType,
						kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_40", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 &&
					ShouldUsePreNGDFCompositeVanilla88Shader(
						a_stage,
						a_shaderType,
						kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_88", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040 &&
					ShouldUsePreNGDFCompositeVanilla10040Shader(
						a_stage,
						a_shaderType,
						kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10040", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088 &&
					ShouldUsePreNGDFCompositeVanilla10088Shader(
						a_stage,
						a_shaderType,
						kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10088", "1");
				}
				if (ShouldEnablePreNGDFCompositeVisibleLLF() &&
					IsPreNGDFCompositeVisibleLLFDescriptor(a_descriptor) &&
					ShouldUsePreNGDFCompositeVanillaSafeBindShader(
						a_stage,
						a_shaderType,
						kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024",
						std::to_string(GetPreNGDFCompositeVisibleLLFScale1024()));
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS",
						std::to_string(GetPreNGDFCompositeVisibleLLFMaxLights()));
				}
			}
#endif

			result.macros.reserve(result.storage.size() + 1);
			for (auto& [name, value] : result.storage) {
				result.macros.push_back({ name.c_str(), value.c_str() });
			}
			result.macros.push_back({});
			return result;
		}

		std::string_view GetDescriptorTarget(ShaderStage a_stage) noexcept
		{
			return a_stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
		}
	}

	ShaderCache* ShaderCache::GetSingleton()
	{
		static ShaderCache singleton;
		return std::addressof(singleton);
	}

	std::string ShaderCache::NormalizeFxpFilename(std::string_view a_fxpFilename)
	{
		if (a_fxpFilename.empty()) {
			return "<empty>";
		}

		std::string normalized{ a_fxpFilename };
		std::ranges::transform(normalized, normalized.begin(), [](unsigned char a_ch) {
			if (a_ch == '\\') {
				return '/';
			}
			return static_cast<char>(std::tolower(a_ch));
		});
		return normalized;
	}

	std::string ShaderCache::NormalizeFxpFilename(const char* a_fxpFilename)
	{
		return a_fxpFilename ? NormalizeFxpFilename(std::string_view{ a_fxpFilename }) : "<null>";
	}

	const char* ShaderCache::GetDescriptorCacheState(const std::optional<DescriptorShaderState>& a_state) noexcept
	{
		if (!a_state) {
			return "missing";
		}

		return a_state->found ? "vanilla-observed" : "miss-observed";
	}

	ShaderCache::DescriptorShaderKey ShaderCache::MakeDescriptorShaderKey(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor)
	{
		return {
			a_stage,
			a_shader.shaderType,
			a_descriptor,
			NormalizeFxpFilename(a_shader.fxpFilename)
		};
	}

	bool ShaderCache::ShouldCompileDescriptorShaders()
	{
#if defined(FALLOUT_PRE_NG)
		static const bool enabled = ReadDescriptorCompileSwitch();
		return enabled;
#else
		return false;
#endif
	}

	std::optional<std::string> ShaderCache::ResolveDescriptorShaderSource(const DescriptorShaderKey& a_key)
	{
		// Visible LLF consumer takes precedence over the compile-only probe when
		// FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND is enabled. The probe path stays as
		// the fallback for compile/observe profiles.
		if (ShouldBindPreNGBSLightingLLFVisibleConsumerShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingLLFConsumerVisibleSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingLLFConsumerVisibleSource };
			}
			return std::nullopt;
		}

		if (ShouldCompilePreNGBSLightingConsumerShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingConsumerProbeSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingConsumerProbeSource };
			}
			return std::nullopt;
		}

		if (ShouldCompilePreNGBSLightingContractShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingContractProbeSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingContractProbeSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFLightFullContractDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGDFLightFullContractDescriptorSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGDFLightFullContractDescriptorSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFLightFullShadowedDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGDFLightFullShadowedDescriptorSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGDFLightFullShadowedDescriptorSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFCompositeContractDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			std::string_view sourcePath = kPreNGDFCompositeDescriptorProbeSource;
			if (ShouldUsePreNGDFCompositeVanilla40Shader(
					a_key.stage,
					a_key.shaderType,
					a_key.fxpFilename,
					a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla40Source;
			} else if (ShouldUsePreNGDFCompositeVanilla88Shader(
					a_key.stage,
					a_key.shaderType,
					a_key.fxpFilename,
					a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla88Source;
			} else if (ShouldUsePreNGDFCompositeVanilla10040Shader(
						   a_key.stage,
						   a_key.shaderType,
						   a_key.fxpFilename,
						   a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla10040Source;
			} else if (ShouldUsePreNGDFCompositeVanilla10088Shader(
						   a_key.stage,
						   a_key.shaderType,
						   a_key.fxpFilename,
						   a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla10088Source;
			}
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(sourcePath);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ sourcePath };
			}
			return std::nullopt;
		}

		auto source = a_key.fxpFilename;
		if (source.empty() || source.front() == '<' || source.find('<') != std::string::npos) {
			return std::nullopt;
		}

		if (StartsWith(source, "data/shaders/")) {
			source.erase(0, std::string_view{ "data/shaders/" }.size());
		} else if (StartsWith(source, "shaders/")) {
			source.erase(0, std::string_view{ "shaders/" }.size());
		}

		std::filesystem::path sourcePath{ source };
		const auto extension = ToLowerAscii(sourcePath.extension().string());
		if (extension == ".fxp" || extension == ".fx") {
			sourcePath.replace_extension(".hlsl");
		} else if (extension.empty()) {
			sourcePath += ".hlsl";
		} else if (extension != ".hlsl") {
			return std::nullopt;
		}

		std::vector<std::filesystem::path> candidates;
		candidates.push_back(sourcePath);
		if (sourcePath.has_parent_path()) {
			candidates.push_back(sourcePath.filename());
		}

		for (const auto& candidate : candidates) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / candidate;
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return candidate.generic_string();
			}
		}

		return std::nullopt;
	}

	void ShaderCache::ObserveDescriptorShader(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_fxpFilename,
		bool a_found,
		std::uintptr_t a_shaderEntry,
		std::uintptr_t a_d3dObject)
	{
		const auto normalizedFxp = NormalizeFxpFilename(a_fxpFilename);
		const DescriptorShaderKey key{ a_stage, a_shader.shaderType, a_descriptor, normalizedFxp };

		std::scoped_lock lock(descriptorLock);
		auto [it, inserted] = descriptorShaders.try_emplace(key);
		auto& state = it->second;
		if (inserted) {
			state.stage = a_stage;
			state.shaderType = a_shader.shaderType;
			state.descriptor = a_descriptor;
			state.fxpFilename = normalizedFxp;
		}

		state.found = state.found || a_found;
		if (a_shaderEntry != 0) {
			state.shaderEntry = a_shaderEntry;
		}
		if (a_d3dObject != 0) {
			state.d3dObject = a_d3dObject;
		}
		++state.hits;
	}

	std::optional<ShaderCache::DescriptorShaderState> ShaderCache::GetDescriptorShaderState(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor) const
	{
		return GetDescriptorShaderState(a_stage, a_shader.shaderType, a_descriptor, NormalizeFxpFilename(a_shader.fxpFilename));
	}

	std::optional<ShaderCache::DescriptorShaderState> ShaderCache::GetDescriptorShaderState(
		ShaderStage a_stage,
		std::int32_t a_shaderType,
		std::uint32_t a_descriptor,
		std::string_view a_fxpFilename) const
	{
		const DescriptorShaderKey key{ a_stage, a_shaderType, a_descriptor, NormalizeFxpFilename(a_fxpFilename) };
		std::scoped_lock lock(descriptorLock);
		if (const auto it = descriptorShaders.find(key); it != descriptorShaders.end()) {
			return it->second;
		}

		return std::nullopt;
	}

	void ShaderCache::LogDescriptorBridgeHeld(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_operation)
	{
		const auto state = GetDescriptorShaderState(a_stage, a_shader, a_descriptor);
		const auto normalizedFxp = state ? state->fxpFilename : NormalizeFxpFilename(a_shader.fxpFilename);
		const auto heldKey = std::format(
			"{}:{}:{}:{:08X}:{}",
			GetStageName(a_stage),
			a_shader.shaderType,
			normalizedFxp,
			a_descriptor,
			a_operation);

		{
			std::scoped_lock lock(descriptorLock);
			if (!descriptorHeldLogs.insert(heldKey).second) {
				return;
			}
		}

		logger::info(
			"[ShaderCache] FO4 descriptor shader cache {} held shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile=held customBind=held",
			a_operation,
			a_shader.shaderType,
			normalizedFxp,
			GetStageName(a_stage),
			a_descriptor,
			GetDescriptorCacheState(state),
			state ? state->hits : 0);
	}

	void ShaderCache::LogDescriptorCompileEvent(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_operation,
		std::string_view a_compileState,
		std::string_view a_reason,
		std::string_view a_extra)
	{
		const auto state = GetDescriptorShaderState(a_stage, a_shader, a_descriptor);
		const auto normalizedFxp = state ? state->fxpFilename : NormalizeFxpFilename(a_shader.fxpFilename);
		const auto logKey = std::format(
			"{}:{}:{}:{:08X}:{}:{}:{}:{}",
			GetStageName(a_stage),
			a_shader.shaderType,
			normalizedFxp,
			a_descriptor,
			a_operation,
			a_compileState,
			a_reason,
			a_extra);

		{
			std::scoped_lock lock(descriptorLock);
			if (!descriptorCompileLogs.insert(logKey).second) {
				return;
			}
		}

		const auto descriptorState = GetDescriptorCacheState(state);
		const auto hits = state ? state->hits : 0;
		if (a_extra.empty()) {
			logger::info(
				"[ShaderCache] FO4 descriptor shader cache {} shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile={} customBind=held reason={}",
				a_operation,
				a_shader.shaderType,
				normalizedFxp,
				GetStageName(a_stage),
				a_descriptor,
				descriptorState,
				hits,
				a_compileState,
				a_reason);
			return;
		}

		logger::info(
			"[ShaderCache] FO4 descriptor shader cache {} shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile={} customBind=held reason={} {}",
			a_operation,
			a_shader.shaderType,
			normalizedFxp,
			GetStageName(a_stage),
			a_descriptor,
			descriptorState,
			hits,
			a_compileState,
			a_reason,
			a_extra);
	}

	RE::BSGraphics::VertexShader* ShaderCache::GetVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		const auto key = MakeDescriptorShaderKey(ShaderStage::Vertex, a_shader, a_descriptor);
		RE::BSGraphics::VertexShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorVertexShaders.find(key); it != descriptorVertexShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(ShaderStage::Vertex, a_shader, a_descriptor, "GetVertexShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		if (!CanCompileDescriptorShader(ShaderStage::Vertex, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(
				ShaderStage::Vertex,
				a_shader,
				a_descriptor,
				"GetVertexShader",
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		return MakeAndAddVertexShader(a_shader, a_descriptor);
	}

	RE::BSGraphics::PixelShader* ShaderCache::GetPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		const auto key = MakeDescriptorShaderKey(ShaderStage::Pixel, a_shader, a_descriptor);
		RE::BSGraphics::PixelShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorPixelShaders.find(key); it != descriptorPixelShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(ShaderStage::Pixel, a_shader, a_descriptor, "GetPixelShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		if (!CanCompileDescriptorShader(ShaderStage::Pixel, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(
				ShaderStage::Pixel,
				a_shader,
				a_descriptor,
				"GetPixelShader",
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		return MakeAndAddPixelShader(a_shader, a_descriptor);
	}

	RE::BSGraphics::VertexShader* ShaderCache::MakeAndAddVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		constexpr auto stage = ShaderStage::Vertex;
		const auto key = MakeDescriptorShaderKey(stage, a_shader, a_descriptor);

		if (!CanCompileDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "gated", "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		if (!CanActivelyCompilePreNGLightingDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "skipped", "unsupported-shaderType");
			return nullptr;
		}

		RE::BSGraphics::VertexShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorVertexShaders.find(key); it != descriptorVertexShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		auto* device = Runtime::GetSingleton()->GetDevice();
		if (!device) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "failed", "device-unavailable");
			return nullptr;
		}

		const auto source = ResolveDescriptorShaderSource(key);
		if (!source) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "failed", "source-unresolved");
			return nullptr;
		}

		auto defines = BuildDescriptorDefineSet(stage, a_shader.shaderType, a_descriptor);
		const auto defineList = BuildFeatureDefineList(defines.macros);
		auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(*source, GetDescriptorTarget(stage), defines.macros.data(), "main");
		if (!bytecode) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddVertexShader",
				"failed",
				"compile-failed",
				std::format("source={} target={} defines={}", *source, GetDescriptorTarget(stage), defineList));
			return nullptr;
		}

		ID3D11VertexShader* shader = nullptr;
		const auto hr = device->CreateVertexShader(bytecode->data(), bytecode->size(), nullptr, &shader);
		if (FAILED(hr)) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddVertexShader",
				"failed",
				"CreateVertexShader-failed",
				std::format("source={} target={} hr=0x{:08X}", *source, GetDescriptorTarget(stage), static_cast<std::uint32_t>(hr)));
			return nullptr;
		}

		auto owned = std::make_unique<OwnedDescriptorVertexShader>();
		owned->d3dShader.attach(shader);
		owned->bytecode = std::move(*bytecode);
		owned->entry.id = a_descriptor;
		owned->entry.shader = ToREVertexShader(owned->d3dShader.get());
		owned->entry.byteCodeSize = static_cast<std::uint32_t>(owned->bytecode.size());
		owned->entry.shaderDesc = a_descriptor;

		RE::BSGraphics::VertexShader* entry = nullptr;
		bool inserted = false;
		{
			std::scoped_lock lock(descriptorLock);
			auto [it, wasInserted] = descriptorVertexShaders.try_emplace(key, std::move(owned));
			inserted = wasInserted;
			entry = std::addressof(it->second->entry);
		}
		LogDescriptorCompileEvent(
			stage,
			a_shader,
			a_descriptor,
			"MakeAndAddVertexShader",
			inserted ? "created" : "cache-hit",
			inserted ? "owned-entry-created" : "owned-entry-created-by-peer",
			std::format("source={} target={} bytecode={} defines={}", *source, GetDescriptorTarget(stage), entry->byteCodeSize, defineList));

		return entry;
	}

	RE::BSGraphics::PixelShader* ShaderCache::MakeAndAddPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		constexpr auto stage = ShaderStage::Pixel;
		const auto key = MakeDescriptorShaderKey(stage, a_shader, a_descriptor);

		if (!CanCompileDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "gated", "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		if (!CanActivelyCompilePreNGLightingDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "skipped", "unsupported-shaderType");
			return nullptr;
		}

		RE::BSGraphics::PixelShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorPixelShaders.find(key); it != descriptorPixelShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		auto* device = Runtime::GetSingleton()->GetDevice();
		if (!device) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "failed", "device-unavailable");
			return nullptr;
		}

		const auto source = ResolveDescriptorShaderSource(key);
		if (!source) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "failed", "source-unresolved");
			return nullptr;
		}

		auto defines = BuildDescriptorDefineSet(stage, a_shader.shaderType, a_descriptor);
		const auto defineList = BuildFeatureDefineList(defines.macros);
		auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(*source, GetDescriptorTarget(stage), defines.macros.data(), "main");
		if (!bytecode) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddPixelShader",
				"failed",
				"compile-failed",
				std::format("source={} target={} defines={}", *source, GetDescriptorTarget(stage), defineList));
			return nullptr;
		}

		ID3D11PixelShader* shader = nullptr;
		const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
		if (FAILED(hr)) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddPixelShader",
				"failed",
				"CreatePixelShader-failed",
				std::format("source={} target={} hr=0x{:08X}", *source, GetDescriptorTarget(stage), static_cast<std::uint32_t>(hr)));
			return nullptr;
		}

		auto owned = std::make_unique<OwnedDescriptorPixelShader>();
		owned->d3dShader.attach(shader);
		owned->bytecode = std::move(*bytecode);
		owned->entry.id = a_descriptor;
		owned->entry.shader = ToREPixelShader(owned->d3dShader.get());
		if (const auto metadata = GetMetadataForBytecode(stage, owned->bytecode.data(), owned->bytecode.size())) {
			ObserveD3DShaderObject(stage, reinterpret_cast<std::uintptr_t>(owned->d3dShader.get()), *metadata);
		}

		RE::BSGraphics::PixelShader* entry = nullptr;
		std::size_t bytecodeSize = 0;
		bool inserted = false;
		{
			std::scoped_lock lock(descriptorLock);
			auto [it, wasInserted] = descriptorPixelShaders.try_emplace(key, std::move(owned));
			inserted = wasInserted;
			entry = std::addressof(it->second->entry);
			bytecodeSize = it->second->bytecode.size();
		}
		LogDescriptorCompileEvent(
			stage,
			a_shader,
			a_descriptor,
			"MakeAndAddPixelShader",
			inserted ? "created" : "cache-hit",
			inserted ? "owned-entry-created" : "owned-entry-created-by-peer",
			std::format("source={} target={} bytecode={} defines={}", *source, GetDescriptorTarget(stage), bytecodeSize, defineList));

		return entry;
	}

	void ShaderCache::ObserveShader(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (!a_bytecode || a_bytecodeLength == 0) {
			return;
		}

		m_hookVerifyCounter.fetch_add(1);

		const auto hash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		{
			std::scoped_lock lock(observedLock);
			if (!observedHashes.insert(hash).second) {
				return;
			}
		}

		logger::trace("[CommunityShaders] Observed {} shader {} ({} bytes)", GetStageName(a_stage), hash, a_bytecodeLength);

		if (tracePipeline)
			TraceShaderCreation(a_stage, a_bytecodeLength, hash);

		if (!dumpAllShaders)
			return;

		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		DumpShader(a_stage, bytes, hash);
	}

	std::optional<std::uint32_t> ShaderCache::GetAsmHashForBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		auto metadata = GetMetadataForBytecode(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
		if (!metadata) {
			return std::nullopt;
		}

		return metadata->asmHash;
	}

	std::optional<ShaderCache::ShaderMetadata> ShaderCache::GetMetadataForBytecode(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (!a_bytecode || a_bytecodeLength == 0) {
			return std::nullopt;
		}

		const auto bytecodeHash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		{
			std::scoped_lock lock(observedLock);
			if (auto it = bytecodeToMetadata.find(bytecodeHash); it != bytecodeToMetadata.end()) {
				return it->second;
			}
		}

		auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		winrt::com_ptr<ID3DBlob> disassembly;
		if (FAILED(D3DDisassemble(bytes.data(), bytes.size_bytes(), 0, nullptr, disassembly.put()))) {
			return std::nullopt;
		}

		auto disasmText = std::string_view{
			static_cast<const char*>(disassembly->GetBufferPointer()),
			disassembly->GetBufferSize()
		};

		auto metadata = BuildMetadata(a_stage, bytes, disasmText);
		{
			std::scoped_lock lock(observedLock);
			bytecodeToAsmHash[bytecodeHash] = metadata.asmHash;
			bytecodeToMetadata[bytecodeHash] = metadata;
		}
		return metadata;
	}

	void ShaderCache::ObserveD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata)
	{
		if (a_d3dObject == 0) {
			return;
		}

		std::scoped_lock lock(observedLock);
		d3dShaderObjectToMetadata.insert_or_assign(D3DShaderObjectKey{ a_stage, a_d3dObject }, a_metadata);
	}

	void ShaderCache::ObserveD3DShaderObjectBytecode(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (a_d3dObject == 0 || !a_bytecode || a_bytecodeLength == 0) {
			return;
		}

		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		D3DShaderObjectBytecode observed;
		observed.metadata = a_metadata;
		observed.bytecodeHash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		observed.bytecode.assign(bytes.begin(), bytes.end());

		std::scoped_lock lock(observedLock);
		const D3DShaderObjectKey key{ a_stage, a_d3dObject };
		d3dShaderObjectToMetadata.insert_or_assign(key, a_metadata);
		d3dShaderObjectToBytecode.insert_or_assign(key, std::move(observed));
	}

	std::optional<ShaderCache::ShaderMetadata> ShaderCache::GetMetadataForD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject)
	{
		if (a_d3dObject == 0) {
			return std::nullopt;
		}

		std::scoped_lock lock(observedLock);
		if (auto it = d3dShaderObjectToMetadata.find(D3DShaderObjectKey{ a_stage, a_d3dObject }); it != d3dShaderObjectToMetadata.end()) {
			return it->second;
		}

		return std::nullopt;
	}

	bool ShaderCache::DumpObservedD3DShaderObject(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		std::string_view a_label,
		std::string_view a_familySubdir)
	{
		if (a_d3dObject == 0) {
			return false;
		}

		D3DShaderObjectBytecode observed;
		const std::string label{ a_label };
		{
			std::scoped_lock lock(observedLock);
			const D3DShaderObjectKey objectKey{ a_stage, a_d3dObject };
			const auto it = d3dShaderObjectToBytecode.find(objectKey);
			if (it == d3dShaderObjectToBytecode.end()) {
				return false;
			}

			const auto dumpKey = std::format("{}:{:X}:{}", GetStageName(a_stage), a_d3dObject, label.empty() ? it->second.metadata.uid : label);
			if (!dumpedD3DShaderObjectKeys.insert(dumpKey).second) {
				return true;
			}

			observed = it->second;
		}

		const auto familySubdir = a_familySubdir.empty() ? std::string_view{ "Unknown" } : a_familySubdir;
		const auto dumpDirectory = GetDumpDirectory() / "LightLimitFix" / familySubdir / GetStageName(a_stage);
		std::error_code ec;
		std::filesystem::create_directories(dumpDirectory, ec);
		if (ec) {
			logger::warn("[CommunityShaders] Failed to create targeted shader dump directory {}: {}", dumpDirectory.string(), ec.message());
			return false;
		}

		const auto stem = label.empty() ?
			observed.metadata.uid :
			std::format("{}_{}", label, observed.metadata.uid);
		const auto binPath = dumpDirectory / std::format("{}.bin", stem);
		std::ofstream bin{ binPath, std::ios::binary };
		bin.write(reinterpret_cast<const char*>(observed.bytecode.data()), static_cast<std::streamsize>(observed.bytecode.size()));

		winrt::com_ptr<ID3DBlob> disassembly;
		if (SUCCEEDED(D3DDisassemble(observed.bytecode.data(), observed.bytecode.size(), 0, nullptr, disassembly.put()))) {
			const auto disassemblyText = std::string_view{
				static_cast<const char*>(disassembly->GetBufferPointer()),
				disassembly->GetBufferSize()
			};
			const auto asmPath = dumpDirectory / std::format("{}.asm", stem);
			std::ofstream asmFile{ asmPath, std::ios::binary };
			asmFile.write(disassemblyText.data(), static_cast<std::streamsize>(disassemblyText.size()));
		} else {
			logger::warn("[CommunityShaders] Failed to disassemble targeted {} shader {}", GetStageName(a_stage), observed.bytecodeHash);
		}

		WriteMetadataFiles(dumpDirectory, a_stage, observed.metadata);

		const auto capturePath = dumpDirectory / std::format("{}.capture.txt", stem);
		std::ofstream captureFile{ capturePath };
		captureFile << "[targetedShaderDump]\n";
		captureFile << "label=" << stem << "\n";
		captureFile << std::format("d3dObject=0x{:X}\n", a_d3dObject);
		captureFile << "stage=" << GetStageName(a_stage) << "\n";
		captureFile << "bytecodeHash=" << observed.bytecodeHash << "\n";
		captureFile << "shaderUID=" << observed.metadata.uid << "\n";
		captureFile << std::format("hash=0x{:08X}\n", observed.metadata.hash);
		captureFile << std::format("asmHash=0x{:08X}\n", observed.metadata.asmHash);
		captureFile << "bytecodeSize=" << observed.bytecode.size() << "\n";
		captureFile << "[/targetedShaderDump]\n";

		logger::info(
			"[CommunityShaders] Targeted observed {} shader dumped label={} d3dObject=0x{:X} uid={} hash={} asm=0x{:08X} size={} path={}",
			GetStageName(a_stage),
			stem,
			a_d3dObject,
			observed.metadata.uid,
			observed.bytecodeHash,
			observed.metadata.asmHash,
			observed.bytecode.size(),
			dumpDirectory.string());
		return true;
	}

	void ShaderCache::TraceShaderCreation(ShaderStage a_stage, SIZE_T a_len, std::string_view a_hash)
	{
		void* stack[64];
		USHORT frames = RtlCaptureStackBackTrace(0, 64, stack, nullptr);
		if (frames == 0)
			return;

		uint64_t stackHash = 14695981039346656037ull;
		for (USHORT i = 0; i < frames; i++) {
			stackHash ^= reinterpret_cast<uint64_t>(stack[i]);
			stackHash *= 1099511628211ull;
		}
		auto stackHashStr = std::format("{:016X}", stackHash);
		{
			std::scoped_lock lock(observedLock);
			if (!traceStackHashes.insert(stackHashStr).second)
				return;
		}

		auto moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("Fallout4.exe"));
		auto traceDir = GetDumpDirectory().parent_path() / "PipelineTrace" / State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);

		auto traceFile = traceDir / "pipeline_trace.txt";
		std::ofstream out(traceFile, std::ios::app);
		out << std::format("[{}] {} (hash={}, {} bytes)\n", GetStageName(a_stage), a_hash, a_hash, a_len);
		out << std::format("  Frames: {}\n", frames);
		for (USHORT i = 0; i < frames && i < 20; i++) {
			auto offset = reinterpret_cast<uintptr_t>(stack[i]) - moduleBase;
			out << std::format("    [{}] Fallout4.exe+0x{:X}\n", i, offset);
		}
		out << "\n";
	}


	std::string ShaderCache::HashShaderBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength) const
	{
		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		uint64_t hash = 14695981039346656037ull;
		for (const auto byte : bytes) {
			hash ^= static_cast<uint8_t>(byte);
			hash *= 1099511628211ull;
		}

		return std::format("{:016X}", hash);
	}

	void ShaderCache::DumpShader(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_hash)
	{
		const auto dumpDirectory = GetDumpDirectory() / GetStageName(a_stage);
		std::error_code ec;
		std::filesystem::create_directories(dumpDirectory, ec);
		if (ec) {
			logger::warn("[CommunityShaders] Failed to create shader dump directory {}: {}", dumpDirectory.string(), ec.message());
			return;
		}

		winrt::com_ptr<ID3DBlob> disassembly;
		if (SUCCEEDED(D3DDisassemble(a_bytecode.data(), a_bytecode.size_bytes(), 0, nullptr, disassembly.put()))) {
			const auto disassemblyText = std::string_view{
				static_cast<const char*>(disassembly->GetBufferPointer()),
				disassembly->GetBufferSize()
			};
			const auto metadata = BuildMetadata(a_stage, a_bytecode, disassemblyText);
			{
				std::scoped_lock lock(observedLock);
				bytecodeToAsmHash[std::string{ a_hash }] = metadata.asmHash;
				bytecodeToMetadata[std::string{ a_hash }] = metadata;
			}
			const auto binPath = dumpDirectory / std::format("{}.bin", metadata.uid);
			std::ofstream bin{ binPath, std::ios::binary };
			bin.write(reinterpret_cast<const char*>(a_bytecode.data()), static_cast<std::streamsize>(a_bytecode.size_bytes()));

			const auto asmPath = dumpDirectory / std::format("{}.asm", metadata.uid);
			std::ofstream asmFile{ asmPath, std::ios::binary };
			asmFile.write(disassemblyText.data(), static_cast<std::streamsize>(disassemblyText.size()));
			WriteMetadataFiles(dumpDirectory, a_stage, metadata);
		} else {
			const auto binPath = dumpDirectory / std::format("{}.bin", a_hash);
			std::ofstream bin{ binPath, std::ios::binary };
			bin.write(reinterpret_cast<const char*>(a_bytecode.data()), static_cast<std::streamsize>(a_bytecode.size_bytes()));
			logger::warn("[CommunityShaders] Failed to disassemble {} shader {}", GetStageName(a_stage), a_hash);
		}
	}

	ShaderCache::ShaderMetadata ShaderCache::BuildMetadata(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_disassembly) const
	{
		ShaderMetadata metadata;
		metadata.hash = HashText({ reinterpret_cast<const char*>(a_bytecode.data()), a_bytecode.size() });
		metadata.size = static_cast<std::uint32_t>(a_bytecode.size_bytes());

		static const auto regexFlags = std::regex_constants::optimize | std::regex_constants::icase;
		static const std::regex instructionRegex{ "^\\s*(add|sub|mul|div|mad|max|min|dp2|dp3|dp4|rsq|sqrt|and|or|xor|not|lt|gt|le|ge|eq|ne|mov(?:c|_sat)?|sample(?:_indexable)?|loop|endloop|if|else|endif|break(?:c)?|ret)\\b", regexFlags };
		static const std::regex sampleInstructionRegex{ "^\\s*(sample(?:_\\w+)?|ld(?:_\\w+)?)\\b", regexFlags };
		static const std::regex textureSampleSlotRegex{ "\\bt(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex textureRegex{ "dcl_resource_(\\w+)\\s*(?:\\([^)]*\\))?\\s*(?:\\([^)]+\\))?\\s+t(\\d+)", regexFlags };
		static const std::regex inputRegex{ "^\\s*dcl_input[^\\r\\n]*\\bv(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex constantBufferRegex{ "dcl_constantbuffer\\s+cb(\\d+)\\[(\\d+)\\]", regexFlags };
		static const std::regex outputRegex{ "^\\s*dcl_output[^\\r\\n]*\\bo(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex discardRegex{ "^\\s*discard(?:_\\w+)?\\b", regexFlags };
		static const std::regex immediateConstantBufferRegex{ "^\\s*dcl_immediateConstantBuffer\\b", regexFlags };

		std::string opcodeStream;
		std::istringstream stream{ std::string{ a_disassembly } };
		std::string line;
		bool inImmediateConstantBuffer = false;
		int immediateConstantBufferDepth = 0;
		while (std::getline(stream, line)) {
			std::smatch match;
			if (std::regex_search(line, match, instructionRegex)) {
				++metadata.instructionCount;
				opcodeStream += match[1].str();
				opcodeStream += ';';
			}

			if (std::regex_search(line, match, sampleInstructionRegex)) {
				++metadata.sampleInstructionCount;
				for (auto it = std::sregex_iterator(line.begin(), line.end(), textureSampleSlotRegex); it != std::sregex_iterator(); ++it) {
					const auto slot = static_cast<std::uint32_t>(std::stoul((*it)[1].str()));
					if (slot < metadata.textureSampleCounts.size()) {
						++metadata.textureSampleCounts[slot];
					}
				}
			}

			const bool startsImmediateConstantBuffer = std::regex_search(line, immediateConstantBufferRegex);
			if (startsImmediateConstantBuffer) {
				metadata.hasImmediateConstantBuffer = true;
				inImmediateConstantBuffer = true;
				immediateConstantBufferDepth = 0;
			}

			if (inImmediateConstantBuffer) {
				const auto openBraceCount = static_cast<int>(std::count(line.begin(), line.end(), '{'));
				const auto closeBraceCount = static_cast<int>(std::count(line.begin(), line.end(), '}'));
				const auto declarationBraceCount = startsImmediateConstantBuffer ? 1 : 0;
				if (openBraceCount > declarationBraceCount) {
					metadata.immediateConstantBufferRows += static_cast<std::uint32_t>(openBraceCount - declarationBraceCount);
				}
				immediateConstantBufferDepth += openBraceCount - closeBraceCount;
				if (immediateConstantBufferDepth <= 0) {
					inImmediateConstantBuffer = false;
					immediateConstantBufferDepth = 0;
				}
			}

			if (std::regex_search(line, discardRegex)) {
				metadata.hasDiscard = true;
			}

			if (std::regex_search(line, match, textureRegex)) {
				const auto slot = static_cast<std::uint32_t>(std::stoul(match[2].str()));
				const auto dimension = GetResourceDimension(match[1].str());
				metadata.textureSlots.push_back(slot);
				metadata.textureDimensions.emplace_back(dimension, slot);
				if (slot < 32) {
					metadata.textureSlotMask |= (1u << slot);
				}
				if (dimension < 32) {
					metadata.textureDimensionMask |= (1u << dimension);
				}
				++metadata.inputTextureCount;
				continue;
			}

			if (std::regex_search(line, match, inputRegex)) {
				const auto inputIndex = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				if (inputIndex < 32) {
					metadata.inputMask |= (1u << inputIndex);
				}
				++metadata.inputCount;
				continue;
			}

			if (std::regex_search(line, match, constantBufferRegex)) {
				const auto slot = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				const auto sizeInDwords = static_cast<std::uint32_t>(std::stoul(match[2].str()));
				if (slot < metadata.constantBufferSizes.size()) {
					metadata.constantBufferSizes[slot] = sizeInDwords * 16;
				}
				continue;
			}

			if (std::regex_search(line, match, outputRegex)) {
				const auto outputIndex = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				if (outputIndex < 32) {
					metadata.outputMask |= (1u << outputIndex);
				}
				++metadata.outputCount;
			}
		}

		metadata.asmHash = HashText(opcodeStream);
		metadata.uid = std::format("{}{:08X}I{}O{}", GetStageName(a_stage), metadata.asmHash, metadata.inputCount, metadata.outputCount);
		return metadata;
	}

	void ShaderCache::WriteMetadataFiles(const std::filesystem::path& a_dumpDirectory, ShaderStage a_stage, const ShaderMetadata& a_metadata) const
	{
		const auto logPath = a_dumpDirectory / std::format("{}.txt", a_metadata.uid);
		std::ofstream logFile{ logPath };
		logFile << "[shaderDump]\n";
		logFile << "active=true\n";
		logFile << "priority=0\n";
		logFile << "type=" << GetStageTypeName(a_stage) << "\n";
		logFile << "shaderUID=" << a_metadata.uid << "\n";
		logFile << std::format("hash=0x{:08X}\n", a_metadata.hash);
		logFile << std::format("asmHash=0x{:08X}\n", a_metadata.asmHash);
		logFile << "size=(" << a_metadata.size << ")\n";

		logFile << "buffersize=";
		bool first = true;
		for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
			if (a_metadata.constantBufferSizes[slot] == 0) {
				continue;
			}
			if (!first) {
				logFile << ",";
			}
			logFile << a_metadata.constantBufferSizes[slot] << "@" << slot;
			first = false;
		}
		logFile << "\n";

		logFile << "textures=";
		for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index) {
			if (index > 0) {
				logFile << ",";
			}
			logFile << a_metadata.textureSlots[index];
		}
		logFile << "\n";

		logFile << "textureDimensions=";
		for (std::size_t index = 0; index < a_metadata.textureDimensions.size(); ++index) {
			if (index > 0) {
				logFile << ",";
			}
			const auto [dimension, slot] = a_metadata.textureDimensions[index];
			logFile << dimension << "@" << slot;
		}
		logFile << "\n";

		logFile << "textureSampleCounts=";
		first = true;
		for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot) {
			const auto count = a_metadata.textureSampleCounts[slot];
			if (count == 0) {
				continue;
			}
			if (!first) {
				logFile << ",";
			}
			logFile << slot << ":" << count;
			first = false;
		}
		logFile << "\n";

		logFile << std::format("textureSlotMask=0x{:X}\n", a_metadata.textureSlotMask);
		logFile << std::format("textureDimensionMask=0x{:X}\n", a_metadata.textureDimensionMask);
		logFile << "inputTextureCount=(" << a_metadata.inputTextureCount << ")\n";
		logFile << "inputcount=(" << a_metadata.inputCount << ")\n";
		logFile << std::format("inputMask=0x{:X}\n", a_metadata.inputMask);
		logFile << "outputcount=(" << a_metadata.outputCount << ")\n";
		logFile << std::format("outputMask=0x{:X}\n", a_metadata.outputMask);
		logFile << "instructionCount=(" << a_metadata.instructionCount << ")\n";
		logFile << "sampleInstructionCount=(" << a_metadata.sampleInstructionCount << ")\n";
		logFile << "immediateConstantBufferRows=(" << a_metadata.immediateConstantBufferRows << ")\n";
		logFile << "hasDiscard=" << (a_metadata.hasDiscard ? "true" : "false") << "\n";
		logFile << "hasImmediateConstantBuffer=" << (a_metadata.hasImmediateConstantBuffer ? "true" : "false") << "\n";
		logFile << "shader=;" << a_metadata.uid << "_replacement.hlsl\n";
		logFile << "log=true\n";
		logFile << "dump=true\n";
		logFile << "[/shaderDump]\n";
	}

	std::filesystem::path ShaderCache::GetDumpDirectory() const
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "ShaderDump" / State::GetSingleton()->GetRuntimeName();
	}

	std::string_view ShaderCache::GetStageName(ShaderStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderStage::Vertex:
			return "VS";
		case ShaderStage::Pixel:
			return "PS";
		case ShaderStage::Compute:
			return "CS";
		case ShaderStage::Geometry:
			return "GS";
		case ShaderStage::Hull:
			return "HS";
		case ShaderStage::Domain:
			return "DS";
		default:
			return "Unknown";
		}
	}

	std::string_view ShaderCache::GetStageTypeName(ShaderStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderStage::Vertex:
			return "vs";
		case ShaderStage::Pixel:
			return "ps";
		case ShaderStage::Compute:
			return "cs";
		case ShaderStage::Geometry:
			return "gs";
		case ShaderStage::Hull:
			return "hs";
		case ShaderStage::Domain:
			return "ds";
		default:
			return "unknown";
		}
	}

	std::uint32_t ShaderCache::HashText(std::string_view a_text) noexcept
	{
		std::uint32_t hash = 2166136261u;
		for (const auto value : a_text) {
			hash ^= static_cast<std::uint8_t>(value);
			hash *= 16777619u;
		}
		return hash;
	}

	std::uint32_t ShaderCache::GetResourceDimension(std::string_view a_resourceType) noexcept
	{
		std::string resourceType{ a_resourceType };
		std::ranges::transform(resourceType, resourceType.begin(), [](unsigned char a_ch) {
			return static_cast<char>(std::tolower(a_ch));
		});

		if (resourceType == "texture2d") return 4;
		if (resourceType == "texture2dms") return 6;
		if (resourceType == "texture2darray") return 5;
		if (resourceType == "texturecube") return 8;
		if (resourceType == "texturecubearray") return 11;
		if (resourceType == "texture3d") return 7;
		if (resourceType == "texture1d") return 3;
		if (resourceType == "buffer") return 1;
		return 0;
	}
}
