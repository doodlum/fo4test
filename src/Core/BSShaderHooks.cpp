#include "Core/BSShaderHooks.h"
#include "Core/CommunityShaders.h"
#include "Core/Feature.h"
#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderCache.h"
#include "Features/LightLimitFix.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <winrt/base.h>
#include <RE/FO4Runtime.h>

namespace CommunityShaders
{
	// ── BSShader::ReloadShaders hook ──────────────────────────────
	//
	// Port of Skyrim CS BSShader::LoadShaders hook.
	// FO4 equivalent: BSShader::ReloadShaders(bool) at vfunc 0x0B.
	//
	// Deferred execution pattern:
	//   ReloadShaders may fire before the D3D device is stable (loading
	//   screens, device creation).  Instead of silently dropping these
	//   calls, we enqueue the BSShader* and drain the queue once per
	//   frame when the device is ready (≥ kStableFrame frames).
	//
	// PreNG shader-path runtime API lives in RE::FO4Runtime::PreNG.
#if defined(FALLOUT_PRE_NG)
	namespace F4Runtime = RE::FO4Runtime;
	static constexpr std::int32_t kPreNGBSLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE);
	static constexpr std::int32_t kPreNGDFLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE);
	static constexpr std::int32_t kPreNGDFCompositeShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_COMPOSITE_SHADER_TYPE);
	static constexpr std::string_view kPreNGDFLightingFxpName = "dflight";
	static constexpr std::string_view kPreNGDFCompositeFxpName = "dfcomposite";
	static constexpr std::size_t kPreNGMaxShaderLookupDiagnostics = 48;
	static constexpr std::uint32_t kPreNGMaxShaderLookupHeavyDiagnostics = 16;
	static constexpr std::size_t kPreNGMaxDescriptorMutationDiagnostics = 64;
	static constexpr std::size_t kPreNGMaxDescriptorBindDiagnostics = 64;
	static constexpr const char* kPreNGShaderLookupDiagEnv = "FO4CS_LLF_PRENG_SHADER_LOOKUP_DIAG";
	static constexpr const char* kPreNGDescriptorMutateEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_MUTATE";
	static constexpr const char* kPreNGDescriptorCompileEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDescriptorBindEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_BIND";
	static constexpr const char* kPreNGDFLightFullContractDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDFLightFullContractDescriptorBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_DESCRIPTOR_BIND";
	static constexpr const char* kPreNGDFLightDescriptorObserveEnv = "FO4CS_LLF_PRENG_DFLIGHT_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPreNGDFLightFullShadowedBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND";
	static constexpr const char* kPreNGDFLightFullShadowedBindBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND_BUDGET";
	static constexpr const char* kPreNGDFLightFullShadowedBindBudgetAliasEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BUDGET";
	static constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
	static constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
	static constexpr const char* kPreNGDFLightVanillaDumpEnv = "FO4CS_LLF_PRENG_DFLIGHT_VANILLA_DUMP";
	static constexpr const char* kPreNGDFCompositeDescriptorObserveEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPreNGDFCompositeDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDFCompositeResourceBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_RESOURCE_BIND";
	static constexpr const char* kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
	static constexpr const char* kPreNGDFCompositeFogSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_FOG_SAFE_BIND";
	static constexpr const char* kPreNGDFCompositeVanillaDumpEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VANILLA_DUMP";
	static constexpr std::size_t kPreNGMaxFxpFilenameLength = 96;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc920 = F4Runtime::PreNG::DF_LIGHT_FULL_SHADOWED_PIXEL_DESCRIPTOR_920;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc922 = F4Runtime::PreNG::DF_LIGHT_FULL_SHADOWED_PIXEL_DESCRIPTOR_922;
	static constexpr std::uint32_t kPreNGDefaultDFLightFullShadowedCandidateBindBudget = 1;
	static constexpr std::uint32_t kPreNGMinDFLightFullShadowedCandidateBindBudget = 1;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindBudget = 2048;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindLogs = 16;
	static constexpr std::size_t kPreNGMaxDFLightVanillaDumpDiagnostics = 8;
	static constexpr std::size_t kPreNGMaxDFCompositeVanillaDumpDiagnostics = 8;
	static constexpr std::size_t kPreNGMaxShaderLookupFirstSeenDiagnostics = 96;
	static constexpr std::string_view kPreNGDFLightFullShadowedCandidateSource = "LightLimitFix\\DFLightFullShadowedPS.hlsl";
#endif
	static constexpr std::uint64_t kStableFrame = 5;

	// ── Forward declarations ────────────────────────────────────

	void ReplacePixelShaders(RE::BSShader* shader);
	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device*, const RE::BSShader&, std::uint32_t);
#if defined(FALLOUT_PRE_NG)
	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		return F4Runtime::IsReadableAddress(a_address, a_size);
	}

	bool IsWritableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		return F4Runtime::IsWritableAddress(a_address, a_size);
	}

	template <class T>
	bool WritePreNGValue(std::uintptr_t a_address, const T& a_value)
	{
		return F4Runtime::WriteValue(a_address, a_value);
	}

	bool IsTruthyPreNGEnvironmentValue(const char* a_value)
	{
		return std::strcmp(a_value, "1") == 0 ||
		       std::strcmp(a_value, "true") == 0 ||
		       std::strcmp(a_value, "TRUE") == 0 ||
		       std::strcmp(a_value, "on") == 0 ||
		       std::strcmp(a_value, "ON") == 0;
	}

	bool ReadPreNGRegistryEnvironmentValue(const char* a_name, HKEY a_root, const char* a_subKey, char (&a_value)[16])
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

	bool ReadPreNGEnvironmentSwitch(const char* a_name)
	{
		char value[16]{};
		if (ReadPreNGRegistryEnvironmentValue(a_name, HKEY_CURRENT_USER, "Environment", value)) {
			return IsTruthyPreNGEnvironmentValue(value);
		}

		if (ReadPreNGRegistryEnvironmentValue(
				a_name,
				HKEY_LOCAL_MACHINE,
				"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				value)) {
			return IsTruthyPreNGEnvironmentValue(value);
		}

		SetLastError(ERROR_SUCCESS);
		const auto length = GetEnvironmentVariableA(a_name, value, static_cast<DWORD>(sizeof(value)));
		if (length > 0) {
			return length < sizeof(value) && IsTruthyPreNGEnvironmentValue(value);
		}
		if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			return false;
		}

		return false;
	}

	enum class PreNGEnvironmentValueSource
	{
		kNone,
		kProcess,
		kUserRegistry,
		kMachineRegistry
	};

	struct PreNGEnvironmentUIntState
	{
		std::uint32_t value = 0;
		PreNGEnvironmentValueSource source = PreNGEnvironmentValueSource::kNone;
		bool present = false;
		bool valid = false;
	};

	const char* PreNGEnvironmentValueSourceName(PreNGEnvironmentValueSource a_source)
	{
		switch (a_source) {
		case PreNGEnvironmentValueSource::kProcess:
			return "process";
		case PreNGEnvironmentValueSource::kUserRegistry:
			return "user-reg";
		case PreNGEnvironmentValueSource::kMachineRegistry:
			return "machine-reg";
		default:
			return "none";
		}
	}

	bool ParsePreNGEnvironmentUIntValue(const char* a_value, std::uint32_t& a_result)
	{
		if (!a_value || *a_value == '\0') {
			return false;
		}

		const char* end = a_value;
		while (*end != '\0') {
			++end;
		}

		const auto parsed = std::from_chars(a_value, end, a_result, 10);
		return parsed.ec == std::errc{} && parsed.ptr == end;
	}

	PreNGEnvironmentUIntState MakePreNGEnvironmentUIntState(
		const char* a_value,
		PreNGEnvironmentValueSource a_source,
		bool a_available)
	{
		PreNGEnvironmentUIntState state{};
		state.source = a_source;
		state.present = true;
		if (!a_available) {
			return state;
		}

		state.valid = ParsePreNGEnvironmentUIntValue(a_value, state.value);
		return state;
	}

	PreNGEnvironmentUIntState ReadPreNGEnvironmentUInt(const char* a_name)
	{
		char value[16]{};
		if (ReadPreNGRegistryEnvironmentValue(a_name, HKEY_CURRENT_USER, "Environment", value)) {
			return MakePreNGEnvironmentUIntState(value, PreNGEnvironmentValueSource::kUserRegistry, true);
		}

		if (ReadPreNGRegistryEnvironmentValue(
				a_name,
				HKEY_LOCAL_MACHINE,
				"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				value)) {
			return MakePreNGEnvironmentUIntState(value, PreNGEnvironmentValueSource::kMachineRegistry, true);
		}

		SetLastError(ERROR_SUCCESS);
		const auto length = GetEnvironmentVariableA(a_name, value, static_cast<DWORD>(sizeof(value)));
		if (length > 0) {
			return MakePreNGEnvironmentUIntState(
				value,
				PreNGEnvironmentValueSource::kProcess,
				length < sizeof(value));
		}
		if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			return MakePreNGEnvironmentUIntState("", PreNGEnvironmentValueSource::kProcess, false);
		}

		return {};
	}

	std::uint32_t GetPreNGDFLightFullShadowedCandidateBindBudget()
	{
		static const std::uint32_t budget = [] {
			const auto primaryState = ReadPreNGEnvironmentUInt(kPreNGDFLightFullShadowedBindBudgetEnv);
			const auto aliasState = ReadPreNGEnvironmentUInt(kPreNGDFLightFullShadowedBindBudgetAliasEnv);
			const auto& state = primaryState.present ? primaryState : aliasState;
			const char* selectedEnv = primaryState.present ? kPreNGDFLightFullShadowedBindBudgetEnv :
				(aliasState.present ? kPreNGDFLightFullShadowedBindBudgetAliasEnv : kPreNGDFLightFullShadowedBindBudgetEnv);
			auto resolved = kPreNGDefaultDFLightFullShadowedCandidateBindBudget;
			auto clamped = false;
			if (state.present && state.valid) {
				const auto requested = state.value;
				resolved = std::clamp(
					requested,
					kPreNGMinDFLightFullShadowedCandidateBindBudget,
					kPreNGMaxDFLightFullShadowedCandidateBindBudget);
				clamped = resolved != requested;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate bind budget resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{} aliasEnv={} aliasPresent={} aliasValid={}",
				selectedEnv,
				resolved,
				PreNGEnvironmentValueSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultDFLightFullShadowedCandidateBindBudget,
				kPreNGMinDFLightFullShadowedCandidateBindBudget,
				kPreNGMaxDFLightFullShadowedCandidateBindBudget,
				kPreNGDFLightFullShadowedBindBudgetAliasEnv,
				aliasState.present,
				aliasState.valid);
			return resolved;
		}();
		return budget;
	}

	bool ShouldBindPreNGDescriptorShaders()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightFullContractDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractDescriptorBindEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDFLightFullContractDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldObservePreNGDFLightDescriptors()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDescriptorObserveEnv);
		return enabled;
	}

	bool ShouldObservePreNGDFCompositeDescriptors()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeDescriptorObserveEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDFCompositeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeDescriptorResources()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeSafeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeFogSafeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeFogSafeBindEnv);
		return enabled;
	}

	bool ShouldMutatePreNGDescriptorShaders()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorMutateEnv);
		return enabled;
	}

	bool ShouldEnablePreNGShaderLookupDiagnostic()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGShaderLookupDiagEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDescriptorShadersForDiagnostic()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightFullShadowedCandidate()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedBindEnv);
		return enabled;
	}

	bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer()
	{
		static const bool enabled = [] {
			const bool requested = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
			const bool unsafeOverride = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
			if (requested && !unsafeOverride) {
				logger::warn(
					"[BSShaderHooks] PreNG DFLight full-shadowed descriptor consumer held; DFLightFullShadowedPS is not vanilla-equivalent and can black out sky-light-only views. Set {}=1 only for focused diagnostics.",
					kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
			}
			return requested && unsafeOverride;
		}();
		return enabled;
	}

	bool ShouldDumpPreNGDFLightVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightVanillaDumpEnv);
		return enabled;
	}

	bool ShouldDumpPreNGDFCompositeVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeVanillaDumpEnv);
		return enabled;
	}

	std::uint32_t NormalizePreNGLightingVertexDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::NormalizeLightingVertexDescriptor(a_descriptor);
	}

	std::uint32_t NormalizePreNGLightingPixelDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::NormalizeLightingPixelDescriptor(a_descriptor);
	}

	template <class T>
	bool ReadPreNGValue(std::uintptr_t a_address, T& a_value)
	{
		return F4Runtime::ReadValue(a_address, a_value);
	}

	std::uintptr_t ReadPreNGPointer(std::uintptr_t a_address)
	{
		return F4Runtime::ReadPointer(a_address);
	}

	std::uintptr_t ReadPreNGShaderEntryD3DObject(std::uintptr_t a_entry)
	{
		return F4Runtime::ReadPreNGShaderEntryD3DObject(a_entry);
	}

	std::string ReadPreNGCString(const char* a_value, std::size_t a_maxLength)
	{
		if (!a_value) {
			return "<null>";
		}

		const auto base = reinterpret_cast<std::uintptr_t>(a_value);
		std::string result;
		result.reserve(a_maxLength);

		for (std::size_t i = 0; i < a_maxLength; ++i) {
			char ch = 0;
			if (!ReadPreNGValue(base + i, ch)) {
				return result.empty() ? "<unreadable>" : result + "<unreadable-tail>";
			}
			if (ch == '\0') {
				return result.empty() ? "<empty>" : result;
			}

			const auto byte = static_cast<unsigned char>(ch);
			result.push_back(byte >= 0x20 && byte <= 0x7E ? ch : '?');
		}

		return result + "<truncated>";
	}

	std::string NormalizePreNGFxpFilename(std::string_view a_fxpFilename)
	{
		std::string normalized{ a_fxpFilename };
		for (auto& ch : normalized) {
			const auto byte = static_cast<unsigned char>(ch);
			ch = byte == '\\' ? '/' : static_cast<char>(std::tolower(byte));
		}
		return normalized;
	}

	bool IsPreNGFxpBaseName(std::string_view a_fxpFilename, std::string_view a_expectedBaseName)
	{
		auto normalized = NormalizePreNGFxpFilename(a_fxpFilename);
		if (normalized.empty() || normalized.front() == '<' || normalized.find('<') != std::string::npos) {
			return false;
		}

		if (const auto slash = normalized.find_last_of('/'); slash != std::string::npos) {
			normalized.erase(0, slash + 1);
		}
		for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
			if (normalized.ends_with(extension)) {
				normalized.resize(normalized.size() - extension.size());
				break;
			}
		}

		return normalized == a_expectedBaseName;
	}

	bool IsPreNGDFLightFxpName(std::string_view a_fxpFilename)
	{
		return IsPreNGFxpBaseName(a_fxpFilename, kPreNGDFLightingFxpName);
	}

	bool IsPreNGDFCompositeFxpName(std::string_view a_fxpFilename)
	{
		return IsPreNGFxpBaseName(a_fxpFilename, kPreNGDFCompositeFxpName);
	}

	bool IsPreNGLightingDescriptorShader(std::int32_t a_shaderType, std::string_view a_fxpFilename)
	{
		return a_shaderType == kPreNGBSLightingShaderType ||
		       (a_shaderType == kPreNGDFLightingShaderType && IsPreNGDFLightFxpName(a_fxpFilename));
	}

	bool IsPreNGLightingDescriptorShader(const RE::BSShader* a_shader)
	{
		if (!a_shader) {
			return false;
		}

		const auto shaderType = static_cast<std::int32_t>(a_shader->shaderType);
		if (shaderType == kPreNGBSLightingShaderType) {
			return true;
		}
		if (shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename);
	}

	bool IsPreNGDFCompositeDescriptorShader(const RE::BSShader* a_shader)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFCompositeShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFCompositeFxpName(fxpFilename);
	}

	bool IsPreNGDFCompositeContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader) &&
		       F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFCompositeSafeBindDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader) &&
		       (a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
		        a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088 ||
		        (ShouldBindPreNGDFCompositeFogSafeDescriptorShader() &&
		         (a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40 ||
		          a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040)));
	}


	bool IsPreNGDFLightFullShadowedPixelDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor);
	}

	bool IsPreNGDFLightFullShadowedDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       IsPreNGDFLightFullShadowedPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFLightFullContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFLightLLFConsumerDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       (F4Runtime::PreNG::IsDFLightLLFConsumerPixelDescriptor(a_pixelDescriptor) ||
		        (ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
		         F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_pixelDescriptor)));
	}

	void LogPreNGDFLightFullContractDescriptorBindHeld(
		const RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		static std::atomic_uint32_t heldCount = 0;
		const auto heldIndex = ++heldCount;
		if (heldIndex > 8 && heldIndex % 8192 != 0) {
			return;
		}

		const auto shaderType = a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1;
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string{ "<null>" };
		logger::info(
			"[BSShaderHooks] PreNG DFLight full-contract descriptor bind held hits={} shaderType={} fxp={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} env={} reason=owned-full-contract-shader-not-vanilla-equivalent",
			heldIndex,
			shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			static_cast<std::uint32_t>(a_pixelDescriptor),
			a_found,
			kPreNGDFLightFullContractDescriptorBindEnv);
	}

	bool IsPreNGDFLightFullShadowedDescriptorConsumerShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
		       IsPreNGDFLightFullShadowedDescriptorShader(a_shader, a_pixelDescriptor);
	}

	bool CanActivelyReplacePreNGLightingDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader) {
			return false;
		}
		if (a_shader->shaderType == kPreNGBSLightingShaderType) {
			return true;
		}
		return IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, a_pixelDescriptor);
	}

	bool IsPreNGDFLightFullShadowedCandidateLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       IsPreNGDFLightFullShadowedPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor));
	}

	bool IsPreNGDFLightVanillaDumpLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       (F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(pixelDescriptor) ||
		        IsPreNGDFLightFullShadowedPixelDescriptor(pixelDescriptor));
	}

	bool IsPreNGDFCompositeVanillaDumpLookup(RE::BSShader* a_shader)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader);
	}

	std::string_view GetPreNGDFLightVanillaDumpFamily(std::uint32_t a_pixelDescriptor)
	{
		if (F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_pixelDescriptor)) {
			return "DFLightFullContract";
		}
		if (IsPreNGDFLightFullShadowedPixelDescriptor(a_pixelDescriptor)) {
			return "DFLightFullShadowed";
		}
		return "DFLightUnknown";
	}

	struct PreNGDFLightFullShadowedCandidateState
	{
		bool attempted = false;
		RE::BSGraphics::PixelShader entry{};
		winrt::com_ptr<ID3D11PixelShader> shader;
		std::vector<std::byte> bytecode;
	};

	struct PreNGDFLightVanillaDumpKey
	{
		std::uint32_t pixelDescriptor = 0;
		std::uintptr_t pixelD3D = 0;
	};

	PreNGDFLightFullShadowedCandidateState s_preNGDFLightFullShadowedCandidate920;
	PreNGDFLightFullShadowedCandidateState s_preNGDFLightFullShadowedCandidate922;
	std::mutex s_preNGDFLightFullShadowedCandidateLock;
	std::atomic_uint32_t s_preNGDFLightFullShadowedBindAttempts = 0;
	std::atomic_uint32_t s_preNGDFLightFullShadowedBoundCount = 0;
	std::atomic_bool s_preNGDFLightFullShadowedBindLimitLogged = false;
	std::mutex s_preNGDFLightVanillaDumpLock;
	std::vector<PreNGDFLightVanillaDumpKey> s_preNGDFLightVanillaDumpKeys;
	std::atomic_uint32_t s_preNGDFLightVanillaDumpAttempts = 0;
	std::atomic_bool s_preNGDFLightVanillaDumpLimitLogged = false;
	std::mutex s_preNGDFCompositeVanillaDumpLock;
	std::vector<PreNGDFLightVanillaDumpKey> s_preNGDFCompositeVanillaDumpKeys;
	std::atomic_uint32_t s_preNGDFCompositeVanillaDumpAttempts = 0;
	std::atomic_bool s_preNGDFCompositeVanillaDumpLimitLogged = false;
	std::atomic_uint32_t s_preNGDFCompositeDescriptorObservations = 0;

	PreNGDFLightFullShadowedCandidateState& GetPreNGDFLightFullShadowedCandidateState(std::uint32_t a_descriptor)
	{
		return a_descriptor == kPreNGDFLightFullShadowedPixelDesc920 ?
			s_preNGDFLightFullShadowedCandidate920 :
			s_preNGDFLightFullShadowedCandidate922;
	}

	bool ShouldDumpPreNGDFLightVanillaObject(std::uint32_t a_pixelDescriptor, std::uintptr_t a_pixelD3D)
	{
		std::scoped_lock lock(s_preNGDFLightVanillaDumpLock);
		for (const auto& key : s_preNGDFLightVanillaDumpKeys) {
			if (key.pixelDescriptor == a_pixelDescriptor && key.pixelD3D == a_pixelD3D) {
				return false;
			}
		}

		if (s_preNGDFLightVanillaDumpKeys.size() >= kPreNGMaxDFLightVanillaDumpDiagnostics) {
			return false;
		}

		s_preNGDFLightVanillaDumpKeys.push_back({ a_pixelDescriptor, a_pixelD3D });
		return true;
	}

	bool ShouldDumpPreNGDFCompositeVanillaObject(std::uint32_t a_pixelDescriptor, std::uintptr_t a_pixelD3D)
	{
		std::scoped_lock lock(s_preNGDFCompositeVanillaDumpLock);
		for (const auto& key : s_preNGDFCompositeVanillaDumpKeys) {
			if (key.pixelDescriptor == a_pixelDescriptor && key.pixelD3D == a_pixelD3D) {
				return false;
			}
		}

		if (s_preNGDFCompositeVanillaDumpKeys.size() >= kPreNGMaxDFCompositeVanillaDumpDiagnostics) {
			return false;
		}

		s_preNGDFCompositeVanillaDumpKeys.push_back({ a_pixelDescriptor, a_pixelD3D });
		return true;
	}

	struct PreNGShaderLookupDiagnosticKey
	{
		std::int32_t shaderType = 0;
		std::int32_t vertexDescriptor = 0;
		std::int32_t hullDescriptor = 0;
		std::int32_t domainDescriptor = 0;
		std::int32_t pixelDescriptor = 0;
		bool found = false;
	};

	bool SamePreNGShaderLookupKey(const PreNGShaderLookupDiagnosticKey& a_lhs, const PreNGShaderLookupDiagnosticKey& a_rhs)
	{
		return a_lhs.shaderType == a_rhs.shaderType &&
		       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
		       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
		       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
		       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
		       a_lhs.found == a_rhs.found;
	}

	struct PreNGShaderLookupFirstSeenKey
	{
		std::int32_t shaderType = 0;
		std::int32_t vertexDescriptor = 0;
		std::int32_t hullDescriptor = 0;
		std::int32_t domainDescriptor = 0;
		std::int32_t pixelDescriptor = 0;
		bool found = false;
		std::string fxpFilename;
	};

	bool SamePreNGShaderLookupFirstSeenKey(
		const PreNGShaderLookupFirstSeenKey& a_lhs,
		const PreNGShaderLookupFirstSeenKey& a_rhs)
	{
		return a_lhs.shaderType == a_rhs.shaderType &&
		       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
		       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
		       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
		       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
		       a_lhs.found == a_rhs.found &&
		       a_lhs.fxpFilename == a_rhs.fxpFilename;
	}

	std::mutex s_preNGShaderLookupDiagnosticLock;
	std::vector<PreNGShaderLookupDiagnosticKey> s_preNGShaderLookupDiagnosticKeys;
	std::mutex s_preNGShaderLookupFirstSeenLock;
	std::vector<PreNGShaderLookupFirstSeenKey> s_preNGShaderLookupFirstSeenKeys;
	std::atomic_uint32_t s_preNGBSShaderLookupEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSShaderLookupLightingEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSShaderLookupBSLightingEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSShaderLookupNullEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSLightingShaderLookupCalls = 0;
	std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsComplete = false;
	std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsLogged = false;
	std::atomic_bool s_preNGShaderLookupFirstSeenLimitLogged = false;

	bool ShouldBypassPreNGShaderLookupHeavyDiagnostics()
	{
		return s_preNGShaderLookupHeavyDiagnosticsComplete.load(std::memory_order_relaxed);
	}

	void MaybeCompletePreNGShaderLookupHeavyDiagnostics()
	{
		const auto lightingCalls = s_preNGBSLightingShaderLookupCalls.load(std::memory_order_relaxed);
		const auto bsLightingCalls = s_preNGBSShaderLookupBSLightingEntryCalls.load(std::memory_order_relaxed);
		const auto totalEntryCalls = s_preNGBSShaderLookupEntryCalls.load(std::memory_order_relaxed);
		constexpr std::uint32_t kPreNGShaderLookupNoBSLightingTotalCallBudget = 250000;
		if (bsLightingCalls < kPreNGMaxShaderLookupHeavyDiagnostics &&
			totalEntryCalls < kPreNGShaderLookupNoBSLightingTotalCallBudget) {
			return;
		}

		s_preNGShaderLookupHeavyDiagnosticsComplete.store(true, std::memory_order_relaxed);
		if (!s_preNGShaderLookupHeavyDiagnosticsLogged.exchange(true, std::memory_order_relaxed)) {
			std::size_t uniqueLookups = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupDiagnosticLock);
				uniqueLookups = s_preNGShaderLookupDiagnosticKeys.size();
			}
			std::size_t firstSeenLookups = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupFirstSeenLock);
				firstSeenLookups = s_preNGShaderLookupFirstSeenKeys.size();
			}
			logger::info(
				"[BSShaderHooks] PreNG shader lookup heavy diagnostic complete; bypassing hot-path metadata/audit work after lightingCalls={} bsLightingCalls={} totalEntryCalls={} uniqueLookups={} firstSeenLookups={} max={} noBSLightingTotalBudget={}; shader replacement remains held",
				lightingCalls,
				bsLightingCalls,
				totalEntryCalls,
				uniqueLookups,
				firstSeenLookups,
				kPreNGMaxShaderLookupHeavyDiagnostics,
				kPreNGShaderLookupNoBSLightingTotalCallBudget);
		}
	}

	bool IsPreNGPowerOfTwo(std::uint32_t a_value)
	{
		return a_value != 0 && (a_value & (a_value - 1)) == 0;
	}

	bool ShouldLogPreNGShaderLookupEntry(
		std::uint32_t a_totalCalls,
		std::uint32_t a_lightingCalls,
		std::uint32_t a_bsLightingCalls,
		std::uint32_t a_nullCalls,
		bool a_isLighting,
		bool a_isBSLighting,
		bool a_isNull)
	{
		return a_totalCalls <= 16 ||
		       IsPreNGPowerOfTwo(a_totalCalls) ||
		       (a_isLighting && (a_lightingCalls <= 16 || IsPreNGPowerOfTwo(a_lightingCalls))) ||
		       (a_isBSLighting && (a_bsLightingCalls <= 16 || IsPreNGPowerOfTwo(a_bsLightingCalls))) ||
		       (a_isNull && a_nullCalls <= 8);
	}

	void TracePreNGShaderLookupFirstSeen(
		std::int32_t a_shaderType,
		std::string_view a_fxpFilename,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalHullDescriptor,
		std::int32_t a_originalDomainDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_lookupVertexDescriptor,
		std::int32_t a_lookupPixelDescriptor,
		std::uint32_t a_totalCalls,
		std::uint32_t a_lightingCalls,
		std::uint32_t a_bsLightingCalls,
		bool a_found,
		bool a_mutated,
		bool a_isLighting,
		bool a_isBSLighting,
		bool a_isNull)
	{
		const PreNGShaderLookupFirstSeenKey key{
			a_shaderType,
			a_originalVertexDescriptor,
			a_originalHullDescriptor,
			a_originalDomainDescriptor,
			a_originalPixelDescriptor,
			a_found,
			std::string{ a_fxpFilename }
		};

		std::size_t seenCount = 0;
		{
			std::scoped_lock lock(s_preNGShaderLookupFirstSeenLock);
			for (const auto& loggedKey : s_preNGShaderLookupFirstSeenKeys) {
				if (SamePreNGShaderLookupFirstSeenKey(loggedKey, key)) {
					return;
				}
			}
			if (s_preNGShaderLookupFirstSeenKeys.size() >= kPreNGMaxShaderLookupFirstSeenDiagnostics) {
				if (!s_preNGShaderLookupFirstSeenLimitLogged.exchange(true, std::memory_order_relaxed)) {
					logger::info(
						"[BSShaderHooks] PreNG shader lookup first-seen diagnostic limit reached max={} total={} lighting={} bsLighting={}; further first-seen entries are held",
						kPreNGMaxShaderLookupFirstSeenDiagnostics,
						a_totalCalls,
						a_lightingCalls,
						a_bsLightingCalls);
				}
				return;
			}
			s_preNGShaderLookupFirstSeenKeys.push_back(key);
			seenCount = s_preNGShaderLookupFirstSeenKeys.size();
		}

		const char* classification = a_isBSLighting ? "bs-lighting" :
			(a_isLighting ? "lighting" :
				(a_shaderType == kPreNGDFCompositeShaderType ? "df-composite" : (a_isNull ? "null" : "non-lighting")));
		logger::info(
			"[BSShaderHooks] PreNG shader lookup first-seen diagnostic seen={} max={} total={} lighting={} bsLighting={} class={} shaderType={} fxp={} originalVS=0x{:X} originalHS=0x{:X} originalDS=0x{:X} originalPS=0x{:X} lookupVS=0x{:X} lookupPS=0x{:X} found={} mutated={} mutateGate={} bindGate={}",
			seenCount,
			kPreNGMaxShaderLookupFirstSeenDiagnostics,
			a_totalCalls,
			a_lightingCalls,
			a_bsLightingCalls,
			classification,
			a_shaderType,
			a_fxpFilename,
			static_cast<std::uint32_t>(a_originalVertexDescriptor),
			static_cast<std::uint32_t>(a_originalHullDescriptor),
			static_cast<std::uint32_t>(a_originalDomainDescriptor),
			static_cast<std::uint32_t>(a_originalPixelDescriptor),
			static_cast<std::uint32_t>(a_lookupVertexDescriptor),
			static_cast<std::uint32_t>(a_lookupPixelDescriptor),
			a_found,
			a_mutated,
			ShouldMutatePreNGDescriptorShaders() ? "on" : "off",
			ShouldBindPreNGDescriptorShaders() ? "on" : "off");
	}

	void TracePreNGShaderLookupEntry(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalHullDescriptor,
		std::int32_t a_originalDomainDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_lookupVertexDescriptor,
		std::int32_t a_lookupPixelDescriptor,
		bool a_found)
	{
		const auto totalCalls = ++s_preNGBSShaderLookupEntryCalls;
		const bool isNull = a_shader == nullptr;
		const auto shaderType = isNull ? -1 : static_cast<std::int32_t>(a_shader->shaderType);
		const auto fxpFilename = isNull ? std::string("<null>") : ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const bool isLighting = !isNull && IsPreNGLightingDescriptorShader(shaderType, fxpFilename);
		const bool isBSLighting = !isNull && shaderType == kPreNGBSLightingShaderType;
		const auto lightingCalls = isLighting ? ++s_preNGBSShaderLookupLightingEntryCalls : s_preNGBSShaderLookupLightingEntryCalls.load();
		const auto bsLightingCalls = isBSLighting ? ++s_preNGBSShaderLookupBSLightingEntryCalls : s_preNGBSShaderLookupBSLightingEntryCalls.load();
		const auto nullCalls = isNull ? ++s_preNGBSShaderLookupNullEntryCalls : s_preNGBSShaderLookupNullEntryCalls.load();
		const bool mutated = a_originalVertexDescriptor != a_lookupVertexDescriptor ||
		                     a_originalPixelDescriptor != a_lookupPixelDescriptor;

		TracePreNGShaderLookupFirstSeen(
			shaderType,
			fxpFilename,
			a_originalVertexDescriptor,
			a_originalHullDescriptor,
			a_originalDomainDescriptor,
			a_originalPixelDescriptor,
			a_lookupVertexDescriptor,
			a_lookupPixelDescriptor,
			totalCalls,
			lightingCalls,
			bsLightingCalls,
			a_found,
			mutated,
			isLighting,
			isBSLighting,
			isNull);
		if (!ShouldLogPreNGShaderLookupEntry(totalCalls, lightingCalls, bsLightingCalls, nullCalls, isLighting, isBSLighting, isNull)) {
			return;
		}

		logger::info(
			"[BSShaderHooks] PreNG shader lookup entry diagnostic total={} lighting={} bsLighting={} null={} shaderType={} fxp={} originalVS=0x{:X} originalHS=0x{:X} originalDS=0x{:X} originalPS=0x{:X} lookupVS=0x{:X} lookupPS=0x{:X} found={} mutated={} mutateGate={} bindGate={}",
			totalCalls,
			lightingCalls,
			bsLightingCalls,
			nullCalls,
			shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_originalVertexDescriptor),
			static_cast<std::uint32_t>(a_originalHullDescriptor),
			static_cast<std::uint32_t>(a_originalDomainDescriptor),
			static_cast<std::uint32_t>(a_originalPixelDescriptor),
			static_cast<std::uint32_t>(a_lookupVertexDescriptor),
			static_cast<std::uint32_t>(a_lookupPixelDescriptor),
			a_found,
			mutated,
			ShouldMutatePreNGDescriptorShaders() ? "on" : "off",
			ShouldBindPreNGDescriptorShaders() ? "on" : "off");
	}

	bool ShouldLogPreNGShaderLookup(const PreNGShaderLookupDiagnosticKey& a_key)
	{
		std::scoped_lock lock(s_preNGShaderLookupDiagnosticLock);
		for (const auto& loggedKey : s_preNGShaderLookupDiagnosticKeys) {
			if (SamePreNGShaderLookupKey(loggedKey, a_key)) {
				return false;
			}
		}

		if (s_preNGShaderLookupDiagnosticKeys.size() >= kPreNGMaxShaderLookupDiagnostics) {
			return false;
		}

		s_preNGShaderLookupDiagnosticKeys.push_back(a_key);
		return true;
	}

	struct PreNGDescriptorMutationDiagnosticKey
	{
		std::int32_t shaderType = 0;
		std::int32_t originalVertexDescriptor = 0;
		std::int32_t originalPixelDescriptor = 0;
		std::int32_t modifiedVertexDescriptor = 0;
		std::int32_t modifiedPixelDescriptor = 0;
		std::string mutateState;
		std::string reason;
	};

	bool SamePreNGDescriptorMutationKey(const PreNGDescriptorMutationDiagnosticKey& a_lhs, const PreNGDescriptorMutationDiagnosticKey& a_rhs)
	{
		return a_lhs.shaderType == a_rhs.shaderType &&
		       a_lhs.originalVertexDescriptor == a_rhs.originalVertexDescriptor &&
		       a_lhs.originalPixelDescriptor == a_rhs.originalPixelDescriptor &&
		       a_lhs.modifiedVertexDescriptor == a_rhs.modifiedVertexDescriptor &&
		       a_lhs.modifiedPixelDescriptor == a_rhs.modifiedPixelDescriptor &&
		       a_lhs.mutateState == a_rhs.mutateState &&
		       a_lhs.reason == a_rhs.reason;
	}

	std::mutex s_preNGDescriptorMutationDiagnosticLock;
	std::vector<PreNGDescriptorMutationDiagnosticKey> s_preNGDescriptorMutationDiagnosticKeys;

	bool ShouldLogPreNGDescriptorMutation(const PreNGDescriptorMutationDiagnosticKey& a_key)
	{
		std::scoped_lock lock(s_preNGDescriptorMutationDiagnosticLock);
		for (const auto& loggedKey : s_preNGDescriptorMutationDiagnosticKeys) {
			if (SamePreNGDescriptorMutationKey(loggedKey, a_key)) {
				return false;
			}
		}

		if (s_preNGDescriptorMutationDiagnosticKeys.size() >= kPreNGMaxDescriptorMutationDiagnostics) {
			return false;
		}

		s_preNGDescriptorMutationDiagnosticKeys.push_back(a_key);
		return true;
	}

	void LogPreNGDescriptorMutation(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_modifiedVertexDescriptor,
		std::int32_t a_modifiedPixelDescriptor,
		const char* a_mutateState,
		const char* a_reason)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader) && !IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const PreNGDescriptorMutationDiagnosticKey key{
			a_shader->shaderType,
			a_originalVertexDescriptor,
			a_originalPixelDescriptor,
			a_modifiedVertexDescriptor,
			a_modifiedPixelDescriptor,
			a_mutateState ? a_mutateState : "<null>",
			a_reason ? a_reason : "<null>"
		};
		if (!ShouldLogPreNGDescriptorMutation(key)) {
			return;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const auto techniqueFamily = (static_cast<std::uint32_t>(a_modifiedPixelDescriptor) >> 8) & 0x3F;
		logger::info(
			"[BSShaderHooks] PreNG descriptor lookup mutation shaderType={} fxp={} techniqueFamily={} originalVS=0x{:X} originalPS=0x{:X} modifiedVS=0x{:X} modifiedPS=0x{:X} descriptorBridge=available shaderDB=held replacement=held customMutate={} reason={}",
			a_shader->shaderType,
			fxpFilename,
			techniqueFamily,
			static_cast<std::uint32_t>(a_originalVertexDescriptor),
			static_cast<std::uint32_t>(a_originalPixelDescriptor),
			static_cast<std::uint32_t>(a_modifiedVertexDescriptor),
			static_cast<std::uint32_t>(a_modifiedPixelDescriptor),
			a_mutateState,
			a_reason);
	}

	struct PreNGDescriptorBindDiagnosticKey
	{
		std::int32_t shaderType = 0;
		std::int32_t vertexDescriptor = 0;
		std::int32_t hullDescriptor = 0;
		std::int32_t domainDescriptor = 0;
		std::int32_t pixelDescriptor = 0;
		std::string bindState;
		std::string reason;
	};

	bool SamePreNGDescriptorBindKey(const PreNGDescriptorBindDiagnosticKey& a_lhs, const PreNGDescriptorBindDiagnosticKey& a_rhs)
	{
		return a_lhs.shaderType == a_rhs.shaderType &&
		       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
		       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
		       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
		       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
		       a_lhs.bindState == a_rhs.bindState &&
		       a_lhs.reason == a_rhs.reason;
	}

	std::mutex s_preNGDescriptorBindDiagnosticLock;
	std::vector<PreNGDescriptorBindDiagnosticKey> s_preNGDescriptorBindDiagnosticKeys;

	bool ShouldLogPreNGDescriptorBind(const PreNGDescriptorBindDiagnosticKey& a_key)
	{
		std::scoped_lock lock(s_preNGDescriptorBindDiagnosticLock);
		for (const auto& loggedKey : s_preNGDescriptorBindDiagnosticKeys) {
			if (SamePreNGDescriptorBindKey(loggedKey, a_key)) {
				return false;
			}
		}

		if (s_preNGDescriptorBindDiagnosticKeys.size() >= kPreNGMaxDescriptorBindDiagnostics) {
			return false;
		}

		s_preNGDescriptorBindDiagnosticKeys.push_back(a_key);
		return true;
	}

	void LogPreNGDescriptorBind(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader,
		std::uintptr_t a_hullEntry,
		std::uintptr_t a_domainEntry,
		const char* a_bindState,
		const char* a_reason);

	bool TryBindPreNGDFCompositeDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
		auto* currentVertexShader = reinterpret_cast<RE::BSGraphics::VertexShader*>(vertexEntry);

		if (vertexEntry == 0 || vertexD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-current-vs-entry-unavailable");
			return false;
		}
		const auto pixelD3D = reinterpret_cast<std::uintptr_t>(a_pixelShader ? a_pixelShader->shader : nullptr);
		if (!a_pixelShader || pixelD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-owned-ps-unavailable");
			return false;
		}
		if (globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				a_found,
				vanillaPixelD3D,
				pixelD3D);
		}
		if (!globals::features::lightLimitFix.loaded ||
			!globals::features::lightLimitFix.HasPreNGDFCompositeDescriptorConsumerData()) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"held",
				"dfcomposite-consumer-data-unavailable");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-bind-helper-or-pixel-global-unavailable");
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-pixel-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-dfcomposite-pixel-current-vs-bound");

		if (globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.BindPreNGDFCompositeDescriptorResourcesToPixelShader();
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"descriptor-dfcomposite-safe-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				true,
				pixelD3D);
		}
		return true;
	}

	void TracePreNGShaderLookup(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader)) {
			return;
		}

		const auto calls = ++s_preNGBSLightingShaderLookupCalls;
		const PreNGShaderLookupDiagnosticKey key{
			a_shader->shaderType,
			a_vertexDescriptor,
			a_hullDescriptor,
			a_domainDescriptor,
			a_pixelDescriptor,
			a_found
		};
		if (!ShouldLogPreNGShaderLookup(key)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto vertexDescriptor = static_cast<std::uint32_t>(a_vertexDescriptor);
		const auto hullDescriptor = static_cast<std::uint32_t>(a_hullDescriptor);
		const auto domainDescriptor = static_cast<std::uint32_t>(a_domainDescriptor);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto techniqueFamily = (pixelDescriptor >> 8) & 0x3F;
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(ShaderStage::Vertex, *a_shader, vertexDescriptor, fxpFilename, vertexD3D != 0, vertexEntry, vertexD3D);
		shaderCache->ObserveDescriptorShader(ShaderStage::Pixel, *a_shader, pixelDescriptor, fxpFilename, pixelD3D != 0, pixelEntry, pixelD3D);
		const auto pixelDescriptorState = shaderCache->GetDescriptorShaderState(ShaderStage::Pixel, a_shader->shaderType, pixelDescriptor, fxpFilename);
		const auto descriptorCacheState = pixelDescriptorState ?
			(pixelDescriptorState->found ? "vanilla-observed" : "miss-observed") :
			"missing";
		constexpr bool descriptorBridgeAvailable = true;

		logger::info(
			"[BSShaderHooks] PreNG shader lookup reached calls={} shaderType={} fxp={} techniqueFamily={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} found={} currentVS=0x{:X} currentHS=0x{:X} currentDS=0x{:X} currentPS=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} descriptorBridge={} descriptorCache={} shaderDB=held replacement=held customCompile=held customBind=held",
			calls,
			a_shader->shaderType,
			fxpFilename,
			techniqueFamily,
			vertexDescriptor,
			hullDescriptor,
			domainDescriptor,
			pixelDescriptor,
			a_found,
			vertexEntry,
			hullEntry,
			domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			descriptorBridgeAvailable ? "available" : "missing",
			descriptorCacheState);

		globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
			"shader-lookup",
			static_cast<std::int32_t>(a_shader->shaderType),
			vertexDescriptor,
			pixelDescriptor,
			a_found,
			pixelD3D);
	}

	void TracePreNGDFCompositeDescriptor(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto vertexDescriptor = static_cast<std::uint32_t>(a_vertexDescriptor);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);

		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(ShaderStage::Vertex, *a_shader, vertexDescriptor, fxpFilename, vertexD3D != 0, vertexEntry, vertexD3D);
		shaderCache->ObserveDescriptorShader(ShaderStage::Pixel, *a_shader, pixelDescriptor, fxpFilename, pixelD3D != 0, pixelEntry, pixelD3D);
		const auto pixelDescriptorState = shaderCache->GetDescriptorShaderState(ShaderStage::Pixel, a_shader->shaderType, pixelDescriptor, fxpFilename);
		const auto descriptorCacheState = pixelDescriptorState ?
			(pixelDescriptorState->found ? "vanilla-observed" : "miss-observed") :
			"missing";
		const auto metadata = shaderCache->GetMetadataForD3DShaderObject(ShaderStage::Pixel, pixelD3D);

		const auto observation = ++s_preNGDFCompositeDescriptorObservations;
		if (observation > 16 && !IsPreNGPowerOfTwo(observation)) {
			return;
		}

		logger::info(
			"[BSShaderHooks] PreNG DFComposite descriptor observed observations={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} found={} currentVS=0x{:X} currentHS=0x{:X} currentDS=0x{:X} currentPS=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} descriptorCache={} metadata={} asm=0x{:08X} hash=0x{:08X} size={} buffers={} textures={} samples={} replacement=held bind=held",
			observation,
			a_shader->shaderType,
			fxpFilename,
			vertexDescriptor,
			static_cast<std::uint32_t>(a_hullDescriptor),
			static_cast<std::uint32_t>(a_domainDescriptor),
			pixelDescriptor,
			a_found,
			vertexEntry,
			hullEntry,
			domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			descriptorCacheState,
			metadata ? metadata->uid : "<none>",
			metadata ? metadata->asmHash : 0,
			metadata ? metadata->hash : 0,
			metadata ? metadata->size : 0,
			metadata ? std::to_string(metadata->constantBufferSizes[2]) : "<none>",
			metadata ? std::to_string(metadata->textureSlotMask) : "<none>",
			metadata ? std::to_string(metadata->sampleInstructionCount) : "<none>");
	}

	void ObservePreNGDFCompositeDescriptorShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);

		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(
			ShaderStage::Vertex,
			*a_shader,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			fxpFilename,
			vertexD3D != 0,
			vertexEntry,
			vertexD3D);
		shaderCache->ObserveDescriptorShader(
			ShaderStage::Pixel,
			*a_shader,
			static_cast<std::uint32_t>(a_pixelDescriptor),
			fxpFilename,
			a_found,
			pixelEntry,
			pixelD3D);
	}

	void LogPreNGDescriptorBind(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader,
		std::uintptr_t a_hullEntry,
		std::uintptr_t a_domainEntry,
		const char* a_bindState,
		const char* a_reason)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader) && !IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const PreNGDescriptorBindDiagnosticKey key{
			a_shader->shaderType,
			a_vertexDescriptor,
			a_hullDescriptor,
			a_domainDescriptor,
			a_pixelDescriptor,
			a_bindState ? a_bindState : "<null>",
			a_reason ? a_reason : "<null>"
		};
		if (!ShouldLogPreNGDescriptorBind(key)) {
			return;
		}

		const auto vertexEntry = reinterpret_cast<std::uintptr_t>(a_vertexShader);
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		const auto vertexD3D = vertexEntry != 0 ? ReadPreNGShaderEntryD3DObject(vertexEntry) : 0;
		const auto pixelD3D = pixelEntry != 0 ? ReadPreNGShaderEntryD3DObject(pixelEntry) : 0;
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const auto compileState =
			a_vertexShader && a_pixelShader ?
				(IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)) ?
						"owned-pixel-current-vs-ready" :
						"owned-entry-ready") :
				"owned-entry-missing";

		logger::info(
			"[BSShaderHooks] PreNG descriptor custom bind shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} vsEntry=0x{:X} hsEntry=0x{:X} dsEntry=0x{:X} psEntry=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} customCompile={} customBind={} reason={}",
			a_shader->shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			static_cast<std::uint32_t>(a_hullDescriptor),
			static_cast<std::uint32_t>(a_domainDescriptor),
			static_cast<std::uint32_t>(a_pixelDescriptor),
			vertexEntry,
			a_hullEntry,
			a_domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			compileState,
			a_bindState,
			a_reason);
	}

	void DumpPreNGDFLightVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFLightVanillaDumpAttempts;
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logDump = [&](const char* a_state, const char* a_reason, std::uintptr_t a_pixelEntry, std::uintptr_t a_pixelD3D) {
			const bool forceLog = std::strcmp(a_reason, "dump-limit-reached") == 0;
			if (!forceLog && attempt > 16 && !IsPreNGPowerOfTwo(attempt)) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFLight vanilla shader dump attempts={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} lookupResult={} currentPS=0x{:X} psD3D=0x{:X} dump={} reason={} maxDumps={}",
				attempt,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_lookupResult,
				a_pixelEntry,
				a_pixelD3D,
				a_state,
				a_reason,
				kPreNGMaxDFLightVanillaDumpDiagnostics);
		};

		if (a_lookupResult == 0) {
			logDump("failed", "vanilla-lookup-miss", 0, 0);
			return;
		}

		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		if (pixelEntry == 0 || pixelD3D == 0) {
			logDump("failed", "current-ps-unavailable", pixelEntry, pixelD3D);
			return;
		}

		if (!ShouldDumpPreNGDFLightVanillaObject(pixelDescriptor, pixelD3D)) {
			bool dumpLimitReached = false;
			{
				std::scoped_lock lock(s_preNGDFLightVanillaDumpLock);
				dumpLimitReached = s_preNGDFLightVanillaDumpKeys.size() >= kPreNGMaxDFLightVanillaDumpDiagnostics;
			}
			if (dumpLimitReached && !s_preNGDFLightVanillaDumpLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logDump("held", "dump-limit-reached", pixelEntry, pixelD3D);
			}
			return;
		}

		const auto label = std::format(
			"{}_PS0x{:08X}_VS0x{:08X}",
			GetPreNGDFLightVanillaDumpFamily(pixelDescriptor),
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(ShaderStage::Pixel, pixelD3D, label);
		logDump(dumped ? "dumped" : "failed", dumped ? "targeted-vanilla-dump-written" : "bytecode-not-observed", pixelEntry, pixelD3D);
	}

	void DumpPreNGDFCompositeVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFCompositeVanillaDumpAttempts;
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logDump = [&](const char* a_state, const char* a_reason, std::uintptr_t a_pixelEntry, std::uintptr_t a_pixelD3D) {
			const bool forceLog = std::strcmp(a_reason, "dump-limit-reached") == 0;
			if (!forceLog && attempt > 16 && !IsPreNGPowerOfTwo(attempt)) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFComposite vanilla shader dump attempts={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} lookupResult={} currentPS=0x{:X} psD3D=0x{:X} dump={} reason={} maxDumps={}",
				attempt,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_lookupResult,
				a_pixelEntry,
				a_pixelD3D,
				a_state,
				a_reason,
				kPreNGMaxDFCompositeVanillaDumpDiagnostics);
		};

		if (a_lookupResult == 0) {
			logDump("failed", "vanilla-lookup-miss", 0, 0);
			return;
		}

		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		if (pixelEntry == 0 || pixelD3D == 0) {
			logDump("failed", "current-ps-unavailable", pixelEntry, pixelD3D);
			return;
		}

		if (!ShouldDumpPreNGDFCompositeVanillaObject(pixelDescriptor, pixelD3D)) {
			bool dumpLimitReached = false;
			{
				std::scoped_lock lock(s_preNGDFCompositeVanillaDumpLock);
				dumpLimitReached = s_preNGDFCompositeVanillaDumpKeys.size() >= kPreNGMaxDFCompositeVanillaDumpDiagnostics;
			}
			if (dumpLimitReached && !s_preNGDFCompositeVanillaDumpLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logDump("held", "dump-limit-reached", pixelEntry, pixelD3D);
			}
			return;
		}

		const auto label = std::format(
			"DFComposite_PS0x{:08X}_VS0x{:08X}",
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(
			ShaderStage::Pixel,
			pixelD3D,
			label,
			"DFComposite");
		logDump(dumped ? "dumped" : "failed", dumped ? "targeted-vanilla-dump-written" : "bytecode-not-observed", pixelEntry, pixelD3D);
	}

	RE::BSGraphics::PixelShader* GetPreNGDFLightFullShadowedCandidatePixelShader(std::uint32_t a_descriptor)
	{
		std::scoped_lock lock(s_preNGDFLightFullShadowedCandidateLock);
		auto& state = GetPreNGDFLightFullShadowedCandidateState(a_descriptor);
		if (state.shader && state.entry.shader) {
			return std::addressof(state.entry);
		}
		if (state.attempted) {
			return nullptr;
		}
		state.attempted = true;

		auto* device = Runtime::GetSingleton()->GetDevice();
		if (!device) {
			logger::warn(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} reason=device-unavailable",
				a_descriptor,
				kPreNGDFLightFullShadowedCandidateSource);
			return nullptr;
		}

		std::vector<std::pair<std::string, std::string>> defineStorage;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature || !feature->loaded) {
				continue;
			}
			auto name = feature->GetShaderDefineName();
			if (!name.empty()) {
				defineStorage.emplace_back(std::move(name), "1");
			}
		}
		defineStorage.emplace_back("FO4CS_DFLIGHT_FULL_SHADOWED_CANDIDATE", "1");

		std::vector<D3D_SHADER_MACRO> defines;
		defines.reserve(defineStorage.size() + 1);
		for (auto& [name, value] : defineStorage) {
			defines.push_back({ name.c_str(), value.c_str() });
		}
		defines.push_back({});

		auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
			kPreNGDFLightFullShadowedCandidateSource,
			"ps_5_0",
			defines.data(),
			"main");
		if (!bytecode) {
			logger::warn(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} reason=compile-failed",
				a_descriptor,
				kPreNGDFLightFullShadowedCandidateSource);
			return nullptr;
		}

		ID3D11PixelShader* shader = nullptr;
		const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
		if (FAILED(hr) || !shader) {
			logger::warn(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} bytecode={} hr=0x{:08X} reason=CreatePixelShader-failed",
				a_descriptor,
				kPreNGDFLightFullShadowedCandidateSource,
				bytecode->size(),
				static_cast<std::uint32_t>(hr));
			return nullptr;
		}

		state.shader.attach(shader);
		state.bytecode = std::move(*bytecode);
		state.entry.id = a_descriptor;
		state.entry.shader = state.shader.get();

		auto* shaderCache = ShaderCache::GetSingleton();
		const auto metadata = shaderCache->GetMetadataForBytecode(
			ShaderStage::Pixel,
			state.bytecode.data(),
			state.bytecode.size());
		if (metadata) {
			shaderCache->ObserveD3DShaderObject(ShaderStage::Pixel, reinterpret_cast<std::uintptr_t>(state.shader.get()), *metadata);
		}

		logger::info(
			"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS created descriptor=0x{:X} source={} bytecode={} psD3D=0x{:X} metadata={} asm=0x{:08X} hash=0x{:08X}; replacement=narrow bind=held",
			a_descriptor,
			kPreNGDFLightFullShadowedCandidateSource,
			state.bytecode.size(),
			reinterpret_cast<std::uintptr_t>(state.shader.get()),
			metadata ? metadata->uid : "<none>",
			metadata ? metadata->asmHash : 0,
			metadata ? metadata->hash : 0);

		return std::addressof(state.entry);
	}

	bool TryBindPreNGDFLightFullShadowedCandidate(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFLightFullShadowedBindAttempts;
		const auto bindBudget = GetPreNGDFLightFullShadowedCandidateBindBudget();
		const bool shouldLog = attempt <= kPreNGMaxDFLightFullShadowedCandidateBindLogs || IsPreNGPowerOfTwo(attempt);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logBind = [&](const char* a_state, const char* a_reason, RE::BSGraphics::PixelShader* a_pixelShader, std::uintptr_t a_pixelD3D, std::uint32_t a_bindIndex) {
			const bool forceLog = std::strcmp(a_reason, "proof-bind-limit-reached") == 0;
			if (!shouldLog && !forceLog) {
				return;
			}
			logger::info(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate bind attempts={} binds={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} psEntry=0x{:X} psD3D=0x{:X} lookupResult={} customBind={} reason={} maxProofBinds={}",
				attempt,
				a_bindIndex,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				reinterpret_cast<std::uintptr_t>(a_pixelShader),
				a_pixelD3D,
				a_lookupResult,
				a_state,
				a_reason,
				bindBudget);
		};

		if (a_lookupResult == 0) {
			logBind("failed", "vanilla-lookup-miss", nullptr, 0, s_preNGDFLightFullShadowedBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		const auto bindIndex = s_preNGDFLightFullShadowedBoundCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (bindIndex > bindBudget) {
			if (!s_preNGDFLightFullShadowedBindLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logBind("held", "proof-bind-limit-reached", nullptr, 0, bindIndex - 1);
			}
			return false;
		}

		auto* pixelShader = GetPreNGDFLightFullShadowedCandidatePixelShader(pixelDescriptor);
		const auto pixelD3D = pixelShader && pixelShader->shader ? reinterpret_cast<std::uintptr_t>(pixelShader->shader) : 0;
		if (!pixelShader || !pixelD3D) {
			logBind("failed", "candidate-ps-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		if (vertexEntry == 0) {
			logBind("failed", "current-vs-entry-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			logBind("failed", "bind-helper-or-pixel-global-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			logBind("failed", "pixel-global-write-failed", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		logBind("bound", "full-shadowed-candidate-bound", pixelShader, pixelD3D, bindIndex);

		if (shouldLog && globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"dflight-full-shadowed-candidate-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				pixelDescriptor,
				true,
				pixelD3D);
		}

		return true;
	}

	bool TryBindPreNGDescriptorShaders(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());

		if (!ShouldBindPreNGDescriptorShaders()) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				a_vertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_BIND-off");
			return false;
		}

		if (!a_vertexShader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-vs");
			return false;
		}
		if (!a_pixelShader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-ps");
			return false;
		}
		if (!a_vertexShader->shader || !a_pixelShader->shader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-d3d-object");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		if (!IsReadableMemory(bindAddr, 16)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "bind-helper-unreadable");
			return false;
		}

		const auto vertexGlobal = F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsWritableMemory(vertexGlobal, sizeof(std::uintptr_t)) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "shader-global-unwritable");
			return false;
		}

		const auto vertexEntry = reinterpret_cast<std::uintptr_t>(a_vertexShader);
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(vertexGlobal, vertexEntry) || !WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "shader-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-entry-bound");
		return true;
	}

	bool TryBindPreNGDFLightDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		auto* currentVertexShader = reinterpret_cast<RE::BSGraphics::VertexShader*>(vertexEntry);

		const bool allowNarrowDFLightFullContractBind =
			ShouldBindPreNGDFLightFullContractDescriptorShader() &&
			IsPreNGDFLightFullContractDescriptorShader(
				a_shader,
				static_cast<std::uint32_t>(a_pixelDescriptor));
		if (!ShouldBindPreNGDescriptorShaders() && !allowNarrowDFLightFullContractBind) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_BIND-off");
			return false;
		}
		if (vertexEntry == 0 || vertexD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "current-vs-entry-unavailable");
			return false;
		}
		const auto pixelD3D = reinterpret_cast<std::uintptr_t>(a_pixelShader ? a_pixelShader->shader : nullptr);
		if (!a_pixelShader || pixelD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-ps");
			return false;
		}
		if (IsPreNGDFLightFullShadowedDescriptorConsumerShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)) &&
			(!globals::features::lightLimitFix.loaded ||
			 !globals::features::lightLimitFix.HasPreNGDFLightDescriptorConsumerData())) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"held",
				"full-shadowed-prepared-local-light-data-unavailable");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "bind-helper-or-pixel-global-unavailable");
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "pixel-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-dflight-pixel-current-vs-bound");

		if (globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.BindPreNGDFLightDescriptorResourcesToPixelShader();
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"descriptor-dflight-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				true,
				pixelD3D);
		}
		return true;
	}

	bool ValidatePreNGShaderPath(std::uintptr_t a_imageBase, std::uintptr_t a_vtableAddr)
	{
		const auto setupTechnique = F4Runtime::PreNG::BS_LIGHTING_SHADER_SETUP_TECHNIQUE.address();
		const auto shaderLookup = F4Runtime::PreNG::BS_SHADER_LOOKUP.address();
		const auto bindShaders = F4Runtime::PreNG::BIND_SHADERS.address();
		const bool vtableReadable = IsReadableMemory(a_vtableAddr + (0x02 * sizeof(std::uintptr_t)), sizeof(std::uintptr_t));
		const bool setupReadable = IsReadableMemory(setupTechnique, 16);
		const bool lookupReadable = IsReadableMemory(shaderLookup, 16);
		const bool bindReadable = IsReadableMemory(bindShaders, 16);
		std::uintptr_t observedSetupTechnique = 0;
		if (vtableReadable) {
			ReadPreNGValue(a_vtableAddr + (0x02 * sizeof(std::uintptr_t)), observedSetupTechnique);
		}

		const bool setupMatches = observedSetupTechnique == setupTechnique;
		logger::info(
			"[BSShaderHooks] PreNG active shader path validation base=0x{:X} vtable=0x{:X} vfunc[0x02]=0x{:X} expectedSetupTechnique=0x{:X} shaderLookup=0x{:X} bindShaders=0x{:X} setupMatches={} readable(vtable={}, setup={}, lookup={}, bind={})",
			a_imageBase,
			a_vtableAddr,
			observedSetupTechnique,
			setupTechnique,
			shaderLookup,
			bindShaders,
			setupMatches,
			vtableReadable,
			setupReadable,
			lookupReadable,
			bindReadable);

		return setupMatches && setupReadable && lookupReadable && bindReadable;
	}

	struct PreNGBSShaderLookup
	{
		static std::uint8_t thunk(
			RE::BSShader* a_shader,
			std::int32_t a_vertexDescriptor,
			std::int32_t a_hullDescriptor,
			std::int32_t a_domainDescriptor,
			std::int32_t a_pixelDescriptor)
		{
			if (!func) {
				return 0;
			}
			const bool shaderLookupTraceActive =
				ShouldEnablePreNGShaderLookupDiagnostic() &&
				!ShouldBypassPreNGShaderLookupHeavyDiagnostics();
			const bool descriptorPathActive =
				ShouldMutatePreNGDescriptorShaders() ||
				ShouldCompilePreNGDescriptorShadersForDiagnostic() ||
				ShouldBindPreNGDescriptorShaders();
			const bool dflightFullShadowedCandidate =
				ShouldBindPreNGDFLightFullShadowedCandidate() &&
				IsPreNGDFLightFullShadowedCandidateLookup(a_shader, a_pixelDescriptor);
			const bool dflightVanillaDump =
				ShouldDumpPreNGDFLightVanillaShader() &&
				IsPreNGDFLightVanillaDumpLookup(a_shader, a_pixelDescriptor);
			const bool dflightDescriptorObserveActive =
				ShouldObservePreNGDFLightDescriptors() &&
				IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeObserveActive =
				ShouldObservePreNGDFCompositeDescriptors() &&
				IsPreNGDFCompositeDescriptorShader(a_shader);
			const bool dfCompositeDescriptorCompileActive =
				ShouldCompilePreNGDFCompositeDescriptorShader() &&
				IsPreNGDFCompositeContractDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeResourceBindActive =
				ShouldBindPreNGDFCompositeDescriptorResources() &&
				IsPreNGDFCompositeContractDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeSafeBindActive =
				ShouldBindPreNGDFCompositeSafeDescriptorShader() &&
				IsPreNGDFCompositeSafeBindDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeVanillaDump =
				ShouldDumpPreNGDFCompositeVanillaShader() &&
				IsPreNGDFCompositeVanillaDumpLookup(a_shader);
			if (!shaderLookupTraceActive &&
				!descriptorPathActive &&
				!dflightDescriptorObserveActive &&
				!dflightFullShadowedCandidate &&
				!dflightVanillaDump &&
				!dfCompositeObserveActive &&
				!dfCompositeDescriptorCompileActive &&
				!dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeVanillaDump) {
				return func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);
			}

			auto lookupVertexDescriptor = a_vertexDescriptor;
			auto lookupPixelDescriptor = a_pixelDescriptor;
			const bool isLightingDescriptor = IsPreNGLightingDescriptorShader(a_shader);
			const bool shaderLookupDiagnosticActive = shaderLookupTraceActive;
			const bool descriptorLookupActive = descriptorPathActive && isLightingDescriptor;
			if (!shaderLookupDiagnosticActive &&
				!descriptorLookupActive &&
				!dflightDescriptorObserveActive &&
				!dflightFullShadowedCandidate &&
				!dflightVanillaDump &&
				!dfCompositeObserveActive &&
				!dfCompositeDescriptorCompileActive &&
				!dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeVanillaDump) {
				return func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);
			}

			const bool isBSLightingDescriptor =
				descriptorLookupActive &&
				a_shader &&
				a_shader->shaderType == kPreNGBSLightingShaderType;
			const bool isDFLightLLFConsumerDescriptor =
				dflightDescriptorObserveActive ||
				(descriptorLookupActive &&
				 IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)));
			const bool isDFLightFullShadowedDescriptorConsumer =
				descriptorLookupActive &&
				IsPreNGDFLightFullShadowedDescriptorConsumerShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool canActivelyReplaceDescriptor =
				descriptorLookupActive &&
				CanActivelyReplacePreNGLightingDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			if (descriptorLookupActive) {
				if (ShouldMutatePreNGDescriptorShaders()) {
					if (canActivelyReplaceDescriptor) {
						if (isBSLightingDescriptor) {
							lookupVertexDescriptor = static_cast<std::int32_t>(
								NormalizePreNGLightingVertexDescriptor(static_cast<std::uint32_t>(a_vertexDescriptor)));
							lookupPixelDescriptor = static_cast<std::int32_t>(
								NormalizePreNGLightingPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor)));
						}
						const auto changed = lookupVertexDescriptor != a_vertexDescriptor ||
						                     lookupPixelDescriptor != a_pixelDescriptor;
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"applied",
							isDFLightFullShadowedDescriptorConsumer ?
								"fo4-dflight-full-shadowed-consumer-preserved" :
								(isDFLightLLFConsumerDescriptor ?
										"fo4-dflight-llf-consumer-preserved" :
										(changed ? "fo4-lighting-normalized" : "fo4-lighting-normalized-unchanged")));
					} else if (shaderLookupTraceActive) {
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"held",
							"active-descriptor-path-held");
					}
				} else if (shaderLookupTraceActive || canActivelyReplaceDescriptor) {
					LogPreNGDescriptorMutation(
						a_shader,
						a_vertexDescriptor,
						a_pixelDescriptor,
						lookupVertexDescriptor,
						lookupPixelDescriptor,
						"gated",
						"FO4CS_LLF_PRENG_DESCRIPTOR_MUTATE-off");
				}
			}

			const auto result = func(
				a_shader,
				lookupVertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				lookupPixelDescriptor);
			if (shaderLookupDiagnosticActive) {
				TracePreNGShaderLookupEntry(
					a_shader,
					a_vertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					a_pixelDescriptor,
					lookupVertexDescriptor,
					lookupPixelDescriptor,
					result != 0);
				TracePreNGShaderLookup(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result != 0);
				if (shaderLookupTraceActive) {
					MaybeCompletePreNGShaderLookupHeavyDiagnostics();
				}
			}
			if (dflightVanillaDump) {
				DumpPreNGDFLightVanillaShader(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (dfCompositeObserveActive) {
				TracePreNGDFCompositeDescriptor(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result != 0);
			}
			if (dfCompositeDescriptorCompileActive) {
				ObservePreNGDFCompositeDescriptorShader(
					a_shader,
					lookupVertexDescriptor,
					lookupPixelDescriptor,
					result != 0);
				(void)ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
			}
			static std::atomic_bool dfCompositeResourceProofComplete = false;
			static std::atomic_bool dfCompositeResourceProofCompleteLogged = false;
			if (dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeResourceProofComplete.load(std::memory_order_relaxed) &&
				globals::features::lightLimitFix.loaded) {
				const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
				const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
				auto* ownedPixelShader = ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
				const auto ownedPixelD3D = reinterpret_cast<std::uintptr_t>(ownedPixelShader ? ownedPixelShader->shader : nullptr);
				globals::features::lightLimitFix.NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
					static_cast<std::uint32_t>(lookupVertexDescriptor),
					static_cast<std::uint32_t>(lookupPixelDescriptor),
					result != 0,
					vanillaPixelD3D,
					ownedPixelD3D);
				if (globals::features::lightLimitFix.HasPreNGDFCompositeDescriptorConsumerData()) {
					const auto resourceState =
						globals::features::lightLimitFix.BindPreNGDFCompositeDescriptorResourcesToPixelShader();

					static std::atomic_uint32_t dfCompositeResourceAuditCount = 0;
					const auto auditIndex = ++dfCompositeResourceAuditCount;
					if (auditIndex <= 8 || auditIndex % 512 == 0) {
						globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
							"descriptor-dfcomposite-resource-bind",
							a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
							static_cast<std::uint32_t>(lookupVertexDescriptor),
							static_cast<std::uint32_t>(lookupPixelDescriptor),
							result != 0,
							vanillaPixelD3D);
					}
					if (resourceState.strictCBBound && resourceState.clusterSRVsBound &&
						!dfCompositeResourceProofComplete.exchange(true, std::memory_order_relaxed) &&
						!dfCompositeResourceProofCompleteLogged.exchange(true, std::memory_order_relaxed)) {
						logger::info(
							"[BSShaderHooks] PreNG DFComposite resource-only proof reached b3/t35-t37 completion; future resource-only DFComposite binds are held until a visible-safe bind gate is enabled");
					}
				}
			}
			if (dfCompositeSafeBindActive) {
				auto* pixelShader = ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (TryBindPreNGDFCompositeDescriptorPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						result != 0,
						pixelShader)) {
					return 1;
				}
				return result;
			}
			if (dfCompositeVanillaDump) {
				DumpPreNGDFCompositeVanillaShader(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (dflightFullShadowedCandidate) {
				TryBindPreNGDFLightFullShadowedCandidate(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (isDFLightLLFConsumerDescriptor && (canActivelyReplaceDescriptor || dflightDescriptorObserveActive)) {
				const bool allowDFLightDescriptorBind =
					isDFLightFullShadowedDescriptorConsumer ||
					ShouldBindPreNGDFLightFullContractDescriptorShader();
				const bool allowDFLightFullContractCompileOnly =
					!allowDFLightDescriptorBind &&
					ShouldCompilePreNGDFLightFullContractDescriptorShader() &&
					IsPreNGDFLightFullContractDescriptorShader(
						a_shader,
						static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (!allowDFLightDescriptorBind) {
					if (allowDFLightFullContractCompileOnly) {
						(void)ShaderCache::GetSingleton()->GetPixelShader(
							*a_shader,
							static_cast<std::uint32_t>(lookupPixelDescriptor));
					}
					LogPreNGDFLightFullContractDescriptorBindHeld(
						a_shader,
						lookupVertexDescriptor,
						lookupPixelDescriptor,
						result != 0);
					return result;
				}

				auto* pixelShader = ShaderCache::GetSingleton()->GetPixelShader(*a_shader, static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (globals::features::lightLimitFix.loaded) {
					const auto ownedPixelD3D = reinterpret_cast<std::uintptr_t>(pixelShader ? pixelShader->shader : nullptr);
					globals::features::lightLimitFix.NotifyPreNGDFLightLLFConsumerDescriptorObserved(
						static_cast<std::uint32_t>(lookupVertexDescriptor),
						static_cast<std::uint32_t>(lookupPixelDescriptor),
						result != 0,
						ownedPixelD3D);
				}
				if (TryBindPreNGDFLightDescriptorPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						pixelShader)) {
					return 1;
				}
				return result;
			}
			if (result != 0 || !canActivelyReplaceDescriptor) {
				return result;
			}

			auto* shaderCache = ShaderCache::GetSingleton();
			auto* vertexShader = shaderCache->GetVertexShader(*a_shader, static_cast<std::uint32_t>(lookupVertexDescriptor));
			auto* pixelShader = shaderCache->GetPixelShader(*a_shader, static_cast<std::uint32_t>(lookupPixelDescriptor));
			return TryBindPreNGDescriptorShaders(
				a_shader,
				lookupVertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				lookupPixelDescriptor,
				vertexShader,
				pixelShader) ?
				1 :
				0;
		}

		static inline std::uint8_t (*func)(RE::BSShader*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr;
	};

	void LogPreNGShaderLookupDetourPatch(std::uintptr_t a_lookupAddr)
	{
		std::uint8_t opcode = 0;
		std::int32_t rel32 = 0;
		const bool readable = IsReadableMemory(a_lookupAddr, 5);
		if (readable) {
			ReadPreNGValue(a_lookupAddr, opcode);
			ReadPreNGValue(a_lookupAddr + 1, rel32);
		}

		std::uintptr_t branchTarget = 0;
		if (readable && opcode == 0xE9) {
			branchTarget = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(a_lookupAddr + 5) + rel32);
		}

		const auto thunk = reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::thunk);
		const auto original = reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::func);
		const bool branchTargetReadable = branchTarget != 0 && IsReadableMemory(branchTarget, 16);
		const bool directThunk = branchTarget == thunk;
		const bool detoursTrampoline = original != 0 && branchTargetReadable && branchTarget != thunk;
		const bool patchVerified = directThunk || detoursTrampoline;
		logger::info(
			"[BSShaderHooks] PreNG shader lookup detour patch check lookup=0x{:X} readable={} opcode=0x{:02X} branchTarget=0x{:X} thunk=0x{:X} patchVerified={} branchTargetReadable={} directThunk={} detoursTrampoline={} original=0x{:X}",
			a_lookupAddr,
			readable,
			opcode,
			branchTarget,
			thunk,
			patchVerified,
			branchTargetReadable,
			directThunk,
			detoursTrampoline,
			original);
	}
#endif

	// ── Deferred shader replacement queue ─────────────────────────

	struct PendingReplace { RE::BSShader* shader; };
	static std::vector<PendingReplace> s_pending;

	static void DrainPending()
	{
		if (s_pending.empty()) return;

		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		auto* device = runtime->GetDevice();
		if (!device || runtime->GetFrameCount() < kStableFrame) return;

		logger::info("[BSShaderHooks] DrainPending: {} queued shader(s) at frame {}",
		             s_pending.size(), runtime->GetFrameCount());

		// Move out so ReplacePixelShaders can re-enqueue on failure
		auto pending = std::move(s_pending);
		s_pending.clear();

		for (auto& item : pending)
			ReplacePixelShaders(item.shader);
	}

	// ── Hook thunk ────────────────────────────────────────────────

	struct BSShader_ReloadShaders
	{
		static void thunk(RE::BSShader* shader, bool a_clear)
		{
			logger::info("[BSShaderHooks] thunk: type={} clear={}", shader->shaderType, a_clear);

			func(shader, a_clear);  // let game load originals first

			auto* runtime = CommunityShaders::Runtime::GetSingleton();
			if (!runtime->GetDevice() || runtime->GetFrameCount() < kStableFrame) {
				// Defer: device not stable yet — enqueue for later
				s_pending.push_back({ shader });
				return;
			}

			ReplacePixelShaders(shader);
		}
		static inline void (*func)(RE::BSShader*, bool) = nullptr;
	};

	void ReplacePixelShaders(RE::BSShader* shader)
	{
#if defined(FALLOUT_PRE_NG)
		(void)shader;
		static bool loggedPreNGHold = false;
		if (!loggedPreNGHold) {
			logger::info("[BSShaderHooks] PreNG pixel shader replacement held; LLF is advancing through the Skyrim-style engine-lighting path, not ShaderDB hash activation");
			loggedPreNGHold = true;
		}
		return;
#else
		auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
		if (!device) return;

		for (auto* entry : shader->pixelShaders) {
			if (!entry->shader) continue;

			auto pixelDesc = entry->id;
			auto vertexDesc = entry->id;
			ModifyShaderLookup(*shader, vertexDesc, pixelDesc);

			auto* newPS = CompileReplacementPS(device, *shader, pixelDesc);
			if (newPS) {
				entry->shader->Release();
				entry->shader = reinterpret_cast<decltype(entry->shader)>(newPS);
				logger::info("[BSShaderHooks] Replaced PS: type={} desc=0x{:08X}",
				             shader->shaderType, pixelDesc);
			}
		}
#endif
	}

	// ── Shader compilation ─────────────────────────────────────
	//
	// Compiles Lighting.hlsl with Feature defines injected.
	// Called by ReplacePixelShaders for each pixel shader entry.

	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device* device,
		const RE::BSShader& /*shader*/,
		std::uint32_t /*descriptor*/)
	{
		std::vector<D3D_SHADER_MACRO> defines;

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded) continue;
			auto name = feature->GetShaderDefineName();
			if (name.empty()) continue;
			defines.push_back({ name.data(), "1" });
		}

		D3D_SHADER_MACRO nullTerm{};
		defines.push_back(nullTerm);

		auto* compiler = ShaderCompiler::GetSingleton();
		auto bytecode = compiler->CompileFromFile(
			"Lighting.hlsl", "ps_5_0", defines.data(), "main");

		if (!bytecode) return nullptr;

		ID3D11PixelShader* ps = nullptr;
		if (FAILED(device->CreatePixelShader(
			bytecode->data(), bytecode->size(), nullptr, &ps))) {
			return nullptr;
		}
		return ps;
	}

	// ── ModifyShaderLookup ──────────────────────────────────────

	void ModifyShaderLookup(const RE::BSShader& a_shader,
	                        std::uint32_t& a_vertexDescriptor,
	                        std::uint32_t& a_pixelDescriptor)
	{
#if defined(FALLOUT_PRE_NG)
		if (!CanActivelyReplacePreNGLightingDescriptorShader(&a_shader, a_pixelDescriptor) || !ShouldMutatePreNGDescriptorShaders()) {
			return;
		}

		if (a_shader.shaderType == kPreNGBSLightingShaderType) {
			a_vertexDescriptor = NormalizePreNGLightingVertexDescriptor(a_vertexDescriptor);
			a_pixelDescriptor = NormalizePreNGLightingPixelDescriptor(a_pixelDescriptor);
		}
#else
		(void)a_shader;
		(void)a_vertexDescriptor;
		(void)a_pixelDescriptor;
#endif
	}

	// ── Install / Frame hook ────────────────────────────────────

	void BSShaderHooks::Install()
	{
		const auto imageBase = RE::FO4Runtime::ModuleBase();

#if defined(FALLOUT_PRE_NG)
		const auto vtableAddr = F4Runtime::PreNG::BS_LIGHTING_SHADER_VTABLE.address();
		const bool preNGShaderPathValid = ValidatePreNGShaderPath(imageBase, vtableAddr);
#else
		auto vtableReloc = REL::Relocation<std::uintptr_t>(
			RE::VTABLE::BSLightingShader[0]);
		auto vtableAddr = vtableReloc.address();
#endif
		auto* vtable = reinterpret_cast<std::uintptr_t*>(vtableAddr);
		auto reloadShadersFn = vtable[0x0B];

		logger::info("[BSShaderHooks] imageBase=0x{:X} vtable=0x{:X} vfunc[0x0B]=0x{:X}",
		             imageBase, vtableAddr, reloadShadersFn);

		BSShader_ReloadShaders::func = reinterpret_cast<void(*)(RE::BSShader*, bool)>(
			Detours::X64::DetourFunction(
				reloadShadersFn,
				reinterpret_cast<std::uintptr_t>(BSShader_ReloadShaders::thunk)));

		logger::info("[BSShaderHooks] Detoured ReloadShaders; drain gate at frame {}",
		             kStableFrame);

#if defined(FALLOUT_PRE_NG)
		const bool preNGShaderLookupDiagEnabled = ShouldEnablePreNGShaderLookupDiagnostic();
		const bool preNGDescriptorMutateEnabled = ShouldMutatePreNGDescriptorShaders();
		const bool preNGDescriptorCompileEnabled = ShouldCompilePreNGDescriptorShadersForDiagnostic();
		const bool preNGDescriptorBindEnabled = ShouldBindPreNGDescriptorShaders();
		const bool preNGDFLightFullContractDescriptorCompileEnabled = ShouldCompilePreNGDFLightFullContractDescriptorShader();
		const bool preNGDFLightFullContractDescriptorBindEnabled = ShouldBindPreNGDFLightFullContractDescriptorShader();
		const bool preNGDFLightDescriptorObserveEnabled = ShouldObservePreNGDFLightDescriptors();
		const bool preNGDFCompositeDescriptorObserveEnabled = ShouldObservePreNGDFCompositeDescriptors();
		const bool preNGDFCompositeDescriptorCompileEnabled = ShouldCompilePreNGDFCompositeDescriptorShader();
		const bool preNGDFCompositeResourceBindEnabled = ShouldBindPreNGDFCompositeDescriptorResources();
		const bool preNGDFCompositeSafeBindEnabled = ShouldBindPreNGDFCompositeSafeDescriptorShader();
		const bool preNGDFCompositeFogSafeBindEnabled = ShouldBindPreNGDFCompositeFogSafeDescriptorShader();
		const bool preNGDFCompositeVanillaDumpEnabled = ShouldDumpPreNGDFCompositeVanillaShader();
		const bool preNGDescriptorPathEnabled =
			preNGShaderLookupDiagEnabled ||
			preNGDescriptorMutateEnabled ||
			preNGDescriptorCompileEnabled ||
			preNGDescriptorBindEnabled ||
			preNGDFLightDescriptorObserveEnabled ||
			preNGDFCompositeDescriptorObserveEnabled ||
			preNGDFCompositeDescriptorCompileEnabled ||
			preNGDFCompositeResourceBindEnabled ||
			preNGDFCompositeSafeBindEnabled;
		const bool preNGDFLightFullShadowedBindEnabled = ShouldBindPreNGDFLightFullShadowedCandidate();
		const bool preNGDFLightFullShadowedDescriptorConsumerEnabled = ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer();
		const bool preNGDFLightVanillaDumpEnabled = ShouldDumpPreNGDFLightVanillaShader();
		if (preNGShaderPathValid && (preNGDescriptorPathEnabled || preNGDFLightFullShadowedBindEnabled || preNGDFLightVanillaDumpEnabled || preNGDFCompositeVanillaDumpEnabled)) {
			const auto lookupAddr = F4Runtime::PreNG::BS_SHADER_LOOKUP.address();
			PreNGBSShaderLookup::func = reinterpret_cast<decltype(PreNGBSShaderLookup::func)>(
				Detours::X64::DetourFunction(
					lookupAddr,
					reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::thunk)));
			logger::info(
				"[BSShaderHooks] Detoured PreNG shader lookup diagnostic/descriptor/bind/dump at 0x{:X}; original=0x{:X}; lookupDiag={} descriptorMutate={} descriptorCompile={} descriptorBind={} dflightDescriptorObserve={} dfCompositeDescriptorObserve={} dfCompositeDescriptorCompile={} dfCompositeResourceBind={} dfCompositeSafeBind={} dfCompositeFogSafeBind={} dflightFullContractDescriptorCompile={} dflightFullContractDescriptorBind={} dflightFullShadowedDescriptorConsumer={} dflightFullShadowedBind={} dflightVanillaDump={} dfCompositeVanillaDump={} shader replacement remains held except gated descriptor-owned consumers and narrow dump/observe/compile/resource gates",
				lookupAddr,
				reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::func),
				preNGShaderLookupDiagEnabled,
				preNGDescriptorMutateEnabled,
				preNGDescriptorCompileEnabled,
				preNGDescriptorBindEnabled,
				preNGDFLightDescriptorObserveEnabled,
				preNGDFCompositeDescriptorObserveEnabled,
				preNGDFCompositeDescriptorCompileEnabled,
				preNGDFCompositeResourceBindEnabled,
				preNGDFCompositeSafeBindEnabled,
				preNGDFCompositeFogSafeBindEnabled,
				preNGDFLightFullContractDescriptorCompileEnabled,
				preNGDFLightFullContractDescriptorBindEnabled,
				preNGDFLightFullShadowedDescriptorConsumerEnabled,
				preNGDFLightFullShadowedBindEnabled,
				preNGDFLightVanillaDumpEnabled,
				preNGDFCompositeVanillaDumpEnabled);
			LogPreNGShaderLookupDetourPatch(lookupAddr);
		} else if (!preNGShaderPathValid) {
			logger::warn("[BSShaderHooks] PreNG shader lookup diagnostic skipped; active shader path validation failed");
		} else {
			logger::info(
				"[BSShaderHooks] PreNG shader lookup diagnostic/descriptor path held; set {}=1 to enable descriptor path tracing, {}=1/{}=1/{}=1 for descriptor mutation/compile/bind, {}=1 for the narrow DFLight full-shadowed candidate bind proof, {}=1 for vanilla DFLight shader dump, or {}=1/{}=1/{}=1/{}=1/{}=1/{}=1 for DFComposite observe/compile/resource-bind/safe-bind/fog-safe-bind/dump; shader replacement remains held",
				kPreNGShaderLookupDiagEnv,
				kPreNGDescriptorMutateEnv,
				kPreNGDescriptorCompileEnv,
				kPreNGDescriptorBindEnv,
				kPreNGDFLightFullShadowedBindEnv,
				kPreNGDFLightVanillaDumpEnv,
				kPreNGDFCompositeDescriptorObserveEnv,
				kPreNGDFCompositeDescriptorCompileEnv,
				kPreNGDFCompositeResourceBindEnv,
				kPreNGDFCompositeSafeBindEnv,
				kPreNGDFCompositeFogSafeBindEnv,
				kPreNGDFCompositeVanillaDumpEnv);
		}
#endif
	}

	void BSShaderHooks::OnFrame()
	{
		DrainPending();
	}
}
