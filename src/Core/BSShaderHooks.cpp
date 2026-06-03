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
	// PreNG shader-path offsets (verified via FO4 1.10.163 static analysis):
	//   BSLightingShader vtable      @ 0x14309AAB8
	//   SetupTechnique vfunc 0x02    @ 0x14289CEC0
	//   Descriptor shader lookup     @ 0x142891DB0
	//   Unified VS/PS bind helper    @ 0x141D10400
#if defined(FALLOUT_PRE_NG)
	static constexpr std::uintptr_t kPreNGStaticImageBase = 0x140000000ull;
	static constexpr std::uintptr_t kPreNGBSLightingShaderVTableVA = 0x14309AAB8ull;
	static constexpr std::uintptr_t kPreNGBSLightingShaderSetupTechniqueVA = 0x14289CEC0ull;
	static constexpr std::uintptr_t kPreNGBSShaderLookupVA = 0x142891DB0ull;
	static constexpr std::uintptr_t kPreNGBindShadersVA = 0x141D10400ull;
	static constexpr std::uintptr_t kPreNGRendererStateVA = 0x1461E0900ull;
	static constexpr std::uintptr_t kPreNGCurrentVertexShaderEntryVA = 0x146732E10ull;
	static constexpr std::uintptr_t kPreNGCurrentHullShaderEntryVA = 0x146732E18ull;
	static constexpr std::uintptr_t kPreNGCurrentDomainShaderEntryVA = 0x146732E20ull;
	static constexpr std::uintptr_t kPreNGCurrentPixelShaderEntryVA = 0x146732E28ull;
	static constexpr std::int32_t kPreNGBSLightingShaderType = 8;
	static constexpr std::int32_t kPreNGDFLightingShaderType = 4;
	static constexpr std::string_view kPreNGDFLightingFxpName = "dflight";
	static constexpr std::size_t kPreNGMaxShaderLookupDiagnostics = 48;
	static constexpr std::uint32_t kPreNGMaxShaderLookupHeavyDiagnostics = 16;
	static constexpr std::size_t kPreNGMaxDescriptorMutationDiagnostics = 64;
	static constexpr std::size_t kPreNGMaxDescriptorBindDiagnostics = 64;
	static constexpr const char* kPreNGShaderLookupDiagEnv = "FO4CS_LLF_PRENG_SHADER_LOOKUP_DIAG";
	static constexpr const char* kPreNGDescriptorMutateEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_MUTATE";
	static constexpr const char* kPreNGDescriptorCompileEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDescriptorBindEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_BIND";
	static constexpr const char* kPreNGDFLightFullShadowedBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND";
	static constexpr const char* kPreNGDFLightFullShadowedBindBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND_BUDGET";
	static constexpr const char* kPreNGDFLightVanillaDumpEnv = "FO4CS_LLF_PRENG_DFLIGHT_VANILLA_DUMP";
	static constexpr std::size_t kPreNGMaxFxpFilenameLength = 96;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc920 = 0x09200202u;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc922 = 0x09220202u;
	static constexpr std::uint32_t kPreNGDefaultDFLightFullShadowedCandidateBindBudget = 16;
	static constexpr std::uint32_t kPreNGMinDFLightFullShadowedCandidateBindBudget = 1;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindBudget = 2048;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindLogs = 16;
	static constexpr std::size_t kPreNGMaxDFLightVanillaDumpDiagnostics = 8;
	static constexpr std::string_view kPreNGDFLightFullShadowedCandidateSource = "LightLimitFix\\DFLightFullShadowedPS.hlsl";
#endif
	static constexpr std::uint64_t kStableFrame = 5;

	// ── Forward declarations ────────────────────────────────────

	void ReplacePixelShaders(RE::BSShader* shader);
	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device*, const RE::BSShader&, std::uint32_t);
#if defined(FALLOUT_PRE_NG)
	std::uintptr_t PreNGRuntimeAddress(std::uintptr_t a_staticVA)
	{
		return static_cast<std::uintptr_t>(REL::Module::get().base()) + (a_staticVA - kPreNGStaticImageBase);
	}

	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<const void*>(a_address), &mbi, sizeof(mbi)) == 0) {
			return false;
		}

		const auto regionBegin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
		const auto regionEnd = regionBegin + mbi.RegionSize;
		const auto readEnd = a_address + a_size;
		if (readEnd < a_address || mbi.State != MEM_COMMIT || a_address < regionBegin || readEnd > regionEnd) {
			return false;
		}

		return (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
	}

	bool IsWritableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<const void*>(a_address), &mbi, sizeof(mbi)) == 0) {
			return false;
		}

		const auto regionBegin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
		const auto regionEnd = regionBegin + mbi.RegionSize;
		const auto writeEnd = a_address + a_size;
		if (writeEnd < a_address || mbi.State != MEM_COMMIT || a_address < regionBegin || writeEnd > regionEnd) {
			return false;
		}
		if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
			return false;
		}

		const auto protect = mbi.Protect & 0xFF;
		return protect == PAGE_READWRITE ||
		       protect == PAGE_WRITECOPY ||
		       protect == PAGE_EXECUTE_READWRITE ||
		       protect == PAGE_EXECUTE_WRITECOPY;
	}

	template <class T>
	bool WritePreNGValue(std::uintptr_t a_address, const T& a_value)
	{
		if (!IsWritableMemory(a_address, sizeof(T))) {
			return false;
		}

		std::memcpy(reinterpret_cast<void*>(a_address), &a_value, sizeof(T));
		return true;
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
			const auto state = ReadPreNGEnvironmentUInt(kPreNGDFLightFullShadowedBindBudgetEnv);
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
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate bind budget resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{}",
				kPreNGDFLightFullShadowedBindBudgetEnv,
				resolved,
				PreNGEnvironmentValueSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultDFLightFullShadowedCandidateBindBudget,
				kPreNGMinDFLightFullShadowedCandidateBindBudget,
				kPreNGMaxDFLightFullShadowedCandidateBindBudget);
			return resolved;
		}();
		return budget;
	}

	bool ShouldBindPreNGDescriptorShaders()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorBindEnv);
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

	bool ShouldDumpPreNGDFLightVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightVanillaDumpEnv);
		return enabled;
	}

	std::uint32_t NormalizePreNGLightingVertexDescriptor(std::uint32_t a_descriptor)
	{
		return a_descriptor & 0x3F0F;
	}

	std::uint32_t NormalizePreNGLightingPixelDescriptor(std::uint32_t a_descriptor)
	{
		if ((a_descriptor & 4u) == 0) {
			a_descriptor &= ~2u;
		}
		return a_descriptor | 1u;
	}

	template <class T>
	bool ReadPreNGValue(std::uintptr_t a_address, T& a_value)
	{
		if (!IsReadableMemory(a_address, sizeof(T))) {
			return false;
		}

		std::memcpy(&a_value, reinterpret_cast<const void*>(a_address), sizeof(T));
		return true;
	}

	std::uintptr_t ReadPreNGPointer(std::uintptr_t a_address)
	{
		std::uintptr_t value = 0;
		ReadPreNGValue(a_address, value);
		return value;
	}

	std::uintptr_t ReadPreNGShaderEntryD3DObject(std::uintptr_t a_entry)
	{
		if (a_entry == 0) {
			return 0;
		}

		return ReadPreNGPointer(a_entry + 0x8);
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

	bool IsPreNGDFLightFxpName(std::string_view a_fxpFilename)
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

		return normalized == kPreNGDFLightingFxpName;
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

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGLightingDescriptorShader(static_cast<std::int32_t>(a_shader->shaderType), fxpFilename);
	}

	bool CanActivelyReplacePreNGLightingDescriptorShader(std::int32_t a_shaderType)
	{
		return a_shaderType == kPreNGBSLightingShaderType;
	}

	bool CanActivelyReplacePreNGLightingDescriptorShader(const RE::BSShader* a_shader)
	{
		return a_shader &&
		       CanActivelyReplacePreNGLightingDescriptorShader(static_cast<std::int32_t>(a_shader->shaderType));
	}

	bool IsPreNGDFLightFullShadowedPixelDescriptor(std::uint32_t a_descriptor)
	{
		return a_descriptor == kPreNGDFLightFullShadowedPixelDesc920 ||
		       a_descriptor == kPreNGDFLightFullShadowedPixelDesc922;
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

	std::mutex s_preNGShaderLookupDiagnosticLock;
	std::vector<PreNGShaderLookupDiagnosticKey> s_preNGShaderLookupDiagnosticKeys;
	std::atomic_uint32_t s_preNGBSShaderLookupEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSShaderLookupLightingEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSShaderLookupNullEntryCalls = 0;
	std::atomic_uint32_t s_preNGBSLightingShaderLookupCalls = 0;
	std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsComplete = false;
	std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsLogged = false;

	bool ShouldBypassPreNGShaderLookupHeavyDiagnostics()
	{
		return s_preNGShaderLookupHeavyDiagnosticsComplete.load(std::memory_order_relaxed);
	}

	void MaybeCompletePreNGShaderLookupHeavyDiagnostics()
	{
		const auto lightingCalls = s_preNGBSLightingShaderLookupCalls.load(std::memory_order_relaxed);
		if (lightingCalls < kPreNGMaxShaderLookupHeavyDiagnostics) {
			return;
		}

		s_preNGShaderLookupHeavyDiagnosticsComplete.store(true, std::memory_order_relaxed);
		if (!s_preNGShaderLookupHeavyDiagnosticsLogged.exchange(true, std::memory_order_relaxed)) {
			std::size_t uniqueLookups = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupDiagnosticLock);
				uniqueLookups = s_preNGShaderLookupDiagnosticKeys.size();
			}
			logger::info(
				"[BSShaderHooks] PreNG shader lookup heavy diagnostic complete; bypassing hot-path metadata/audit work after lightingCalls={} uniqueLookups={} max={}; shader replacement remains held",
				lightingCalls,
				uniqueLookups,
				kPreNGMaxShaderLookupHeavyDiagnostics);
		}
	}

	bool IsPreNGPowerOfTwo(std::uint32_t a_value)
	{
		return a_value != 0 && (a_value & (a_value - 1)) == 0;
	}

	bool ShouldLogPreNGShaderLookupEntry(
		std::uint32_t a_totalCalls,
		std::uint32_t a_lightingCalls,
		std::uint32_t a_nullCalls,
		bool a_isLighting,
		bool a_isNull)
	{
		return a_totalCalls <= 16 ||
		       IsPreNGPowerOfTwo(a_totalCalls) ||
		       (a_isLighting && (a_lightingCalls <= 16 || IsPreNGPowerOfTwo(a_lightingCalls))) ||
		       (a_isNull && a_nullCalls <= 8);
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
		const auto lightingCalls = isLighting ? ++s_preNGBSShaderLookupLightingEntryCalls : s_preNGBSShaderLookupLightingEntryCalls.load();
		const auto nullCalls = isNull ? ++s_preNGBSShaderLookupNullEntryCalls : s_preNGBSShaderLookupNullEntryCalls.load();

		if (!ShouldLogPreNGShaderLookupEntry(totalCalls, lightingCalls, nullCalls, isLighting, isNull)) {
			return;
		}

		const bool mutated = a_originalVertexDescriptor != a_lookupVertexDescriptor ||
		                     a_originalPixelDescriptor != a_lookupPixelDescriptor;

		logger::info(
			"[BSShaderHooks] PreNG shader lookup entry diagnostic total={} lighting={} null={} shaderType={} fxp={} originalVS=0x{:X} originalHS=0x{:X} originalDS=0x{:X} originalPS=0x{:X} lookupVS=0x{:X} lookupPS=0x{:X} found={} mutated={} mutateGate={} bindGate={}",
			totalCalls,
			lightingCalls,
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
		if (!IsPreNGLightingDescriptorShader(a_shader)) {
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

		const auto vertexEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentVertexShaderEntryVA));
		const auto hullEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentHullShaderEntryVA));
		const auto domainEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentDomainShaderEntryVA));
		const auto pixelEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentPixelShaderEntryVA));
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
		if (!IsPreNGLightingDescriptorShader(a_shader)) {
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
		const auto vertexD3D = a_vertexShader ? reinterpret_cast<std::uintptr_t>(a_vertexShader->shader) : 0;
		const auto pixelD3D = a_pixelShader ? reinterpret_cast<std::uintptr_t>(a_pixelShader->shader) : 0;
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const auto compileState = a_vertexShader && a_pixelShader ? "owned-entry-ready" : "owned-entry-missing";

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

		const auto pixelEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentPixelShaderEntryVA));
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
			"DFLightFullShadowed_PS0x{:08X}_VS0x{:08X}",
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(ShaderStage::Pixel, pixelD3D, label);
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

		const auto vertexEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentVertexShaderEntryVA));
		const auto hullEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentHullShaderEntryVA));
		const auto domainEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentDomainShaderEntryVA));
		if (vertexEntry == 0) {
			logBind("failed", "current-vs-entry-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto bindAddr = PreNGRuntimeAddress(kPreNGBindShadersVA);
		const auto pixelGlobal = PreNGRuntimeAddress(kPreNGCurrentPixelShaderEntryVA);
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
		bindShaders(PreNGRuntimeAddress(kPreNGRendererStateVA), vertexEntry, hullEntry, domainEntry, pixelEntry);
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
		const auto hullEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentHullShaderEntryVA));
		const auto domainEntry = ReadPreNGPointer(PreNGRuntimeAddress(kPreNGCurrentDomainShaderEntryVA));

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

		const auto bindAddr = PreNGRuntimeAddress(kPreNGBindShadersVA);
		if (!IsReadableMemory(bindAddr, 16)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "bind-helper-unreadable");
			return false;
		}

		const auto vertexGlobal = PreNGRuntimeAddress(kPreNGCurrentVertexShaderEntryVA);
		const auto pixelGlobal = PreNGRuntimeAddress(kPreNGCurrentPixelShaderEntryVA);
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
		bindShaders(PreNGRuntimeAddress(kPreNGRendererStateVA), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-entry-bound");
		return true;
	}

	bool ValidatePreNGShaderPath(std::uintptr_t a_imageBase, std::uintptr_t a_vtableAddr)
	{
		const auto setupTechnique = PreNGRuntimeAddress(kPreNGBSLightingShaderSetupTechniqueVA);
		const auto shaderLookup = PreNGRuntimeAddress(kPreNGBSShaderLookupVA);
		const auto bindShaders = PreNGRuntimeAddress(kPreNGBindShadersVA);
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
			const bool shaderLookupDiagnosticActive =
				ShouldEnablePreNGShaderLookupDiagnostic() &&
				!ShouldBypassPreNGShaderLookupHeavyDiagnostics();
			const bool dflightFullShadowedCandidate =
				ShouldBindPreNGDFLightFullShadowedCandidate() &&
				IsPreNGDFLightFullShadowedCandidateLookup(a_shader, a_pixelDescriptor);
			const bool dflightVanillaDump =
				ShouldDumpPreNGDFLightVanillaShader() &&
				IsPreNGDFLightFullShadowedCandidateLookup(a_shader, a_pixelDescriptor);
			if (!shaderLookupDiagnosticActive && !dflightFullShadowedCandidate && !dflightVanillaDump) {
				return func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);
			}

			auto lookupVertexDescriptor = a_vertexDescriptor;
			auto lookupPixelDescriptor = a_pixelDescriptor;
			const bool isLightingDescriptor = IsPreNGLightingDescriptorShader(a_shader);
			const bool canActivelyReplaceDescriptor =
				shaderLookupDiagnosticActive && CanActivelyReplacePreNGLightingDescriptorShader(a_shader);
			if (shaderLookupDiagnosticActive && isLightingDescriptor) {
				if (ShouldMutatePreNGDescriptorShaders()) {
					if (canActivelyReplaceDescriptor) {
						lookupVertexDescriptor = static_cast<std::int32_t>(
							NormalizePreNGLightingVertexDescriptor(static_cast<std::uint32_t>(a_vertexDescriptor)));
						lookupPixelDescriptor = static_cast<std::int32_t>(
							NormalizePreNGLightingPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor)));
						const auto changed = lookupVertexDescriptor != a_vertexDescriptor ||
						                     lookupPixelDescriptor != a_pixelDescriptor;
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"applied",
							changed ? "fo4-lighting-normalized" : "fo4-lighting-normalized-unchanged");
					} else {
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"held",
							"active-descriptor-path-held");
					}
				} else {
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
				MaybeCompletePreNGShaderLookupHeavyDiagnostics();
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
			if (dflightFullShadowedCandidate) {
				TryBindPreNGDFLightFullShadowedCandidate(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (!shaderLookupDiagnosticActive) {
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
		if (!CanActivelyReplacePreNGLightingDescriptorShader(&a_shader) || !ShouldMutatePreNGDescriptorShaders()) {
			return;
		}

		a_vertexDescriptor = NormalizePreNGLightingVertexDescriptor(a_vertexDescriptor);
		a_pixelDescriptor = NormalizePreNGLightingPixelDescriptor(a_pixelDescriptor);
#else
		(void)a_shader;
		(void)a_vertexDescriptor;
		(void)a_pixelDescriptor;
#endif
	}

	// ── Install / Frame hook ────────────────────────────────────

	void BSShaderHooks::Install()
	{
#if defined(FALLOUT_POST_AE)
		auto imageBase = REX::FModule::GetExecutingModule().GetBaseAddress();
#else
		auto imageBase = REL::Module::get().base();
#endif

#if defined(FALLOUT_PRE_NG)
		const auto vtableAddr = PreNGRuntimeAddress(kPreNGBSLightingShaderVTableVA);
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
		const bool preNGDFLightFullShadowedBindEnabled = ShouldBindPreNGDFLightFullShadowedCandidate();
		const bool preNGDFLightVanillaDumpEnabled = ShouldDumpPreNGDFLightVanillaShader();
		if (preNGShaderPathValid && (preNGShaderLookupDiagEnabled || preNGDFLightFullShadowedBindEnabled || preNGDFLightVanillaDumpEnabled)) {
			const auto lookupAddr = PreNGRuntimeAddress(kPreNGBSShaderLookupVA);
			PreNGBSShaderLookup::func = reinterpret_cast<decltype(PreNGBSShaderLookup::func)>(
				Detours::X64::DetourFunction(
					lookupAddr,
					reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::thunk)));
			logger::info(
				"[BSShaderHooks] Detoured PreNG shader lookup diagnostic/bind/dump at 0x{:X}; original=0x{:X}; lookupDiag={} dflightFullShadowedBind={} dflightVanillaDump={} shader replacement remains held except narrow DFLight proof gate",
				lookupAddr,
				reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::func),
				preNGShaderLookupDiagEnabled,
				preNGDFLightFullShadowedBindEnabled,
				preNGDFLightVanillaDumpEnabled);
			LogPreNGShaderLookupDetourPatch(lookupAddr);
		} else if (!preNGShaderPathValid) {
			logger::warn("[BSShaderHooks] PreNG shader lookup diagnostic skipped; active shader path validation failed");
		} else {
			logger::info(
				"[BSShaderHooks] PreNG shader lookup diagnostic held; set {}=1 to enable descriptor path tracing/bind diagnostics, {}=1 for the narrow DFLight full-shadowed candidate bind proof, or {}=1 for vanilla DFLight shader dump; mutateGate={} compileGate={} bindGate={}; shader replacement remains held",
				kPreNGShaderLookupDiagEnv,
				kPreNGDFLightFullShadowedBindEnv,
				kPreNGDFLightVanillaDumpEnv,
				ShouldMutatePreNGDescriptorShaders() ? "on" : "off",
				ShouldCompilePreNGDescriptorShadersForDiagnostic() ? "on" : "off",
				ShouldBindPreNGDescriptorShaders() ? "on" : "off");
		}
#endif
	}

	void BSShaderHooks::OnFrame()
	{
		DrainPending();
	}
}
