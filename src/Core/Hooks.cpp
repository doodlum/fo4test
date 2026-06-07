#include "Core/Hooks.h"

#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"
#include "Features/LightLimitFix.h"

#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
	namespace
	{
		using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
		using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
		using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);

		CreateVertexShaderFn createVertexShader = nullptr;
		CreatePixelShaderFn createPixelShader = nullptr;
		CreateComputeShaderFn createComputeShader = nullptr;
		bool installedDeviceHooks = false;

#if defined(FALLOUT_PRE_NG)
		using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
		using PSSetShaderResourcesFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
		using PSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
		using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
		using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
		using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
		using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
		using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
		using DrawIndexedInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using DrawInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using IASetPrimitiveTopologyFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY);
		using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		using RSSetViewportsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
		using CopyResourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
		using ExecuteCommandListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);

		CreateDeferredContextFn createDeferredContext = nullptr;
		constexpr const char* kPreNGShaderLookupDiagEnv = "FO4CS_LLF_PRENG_SHADER_LOOKUP_DIAG";
		constexpr const char* kPreNGShaderObjectMetadataEnv = "FO4CS_LLF_PRENG_SHADER_OBJECT_METADATA";
		constexpr const char* kPreNGDFLightVanillaDumpEnv = "FO4CS_LLF_PRENG_DFLIGHT_VANILLA_DUMP";
		constexpr const char* kPreNGDFCompositeVanillaDumpEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VANILLA_DUMP";
		constexpr const char* kPreNGDFLightDrawStateEnv = "FO4CS_LLF_PRENG_DFLIGHT_DRAW_STATE";
		constexpr const char* kPreNGDFLightZeroAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_ZERO_ADD_PASS";
		constexpr const char* kPreNGDFLightDrawStateStrictCBBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_STRICT_CB";
		constexpr const char* kPreNGDFLightDrawStateClusterSRVBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_CLUSTER_SRVS";
		constexpr const char* kPreNGDFLightDrawStateProofBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_DRAW_STATE_PROOF_BUDGET";
		constexpr const char* kPreNGDFLightResourceNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_RESOURCE_NOOP_PASS";
		constexpr const char* kPreNGDFLightFullContractNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_NOOP_PASS";
		constexpr const char* kPreNGDFLightLLFAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PASS";
		constexpr const char* kPreNGDFLightLegacyAdditiveProofEnv = "FO4CS_LLF_PRENG_DFLIGHT_LEGACY_ADDITIVE_PROOF";
		constexpr const char* kPreNGDFLightLLFAdditiveBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_BUDGET";
		constexpr const char* kPreNGDFLightLLFAdditivePersistentEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PERSISTENT";
		constexpr const char* kPreNGDFLightLLFAdditiveFrameBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_FRAME_BUDGET";
		constexpr const char* kPreNGDFLightLLFAdditiveScale1024Env = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_SCALE_1024";
		constexpr const char* kPreNGDFLightLLFAdditiveMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_MAX_LIGHTS";
		constexpr const char* kPreNGDFLightZeroAdditiveSource = "LightLimitFix\\DFLightZeroAdditivePS.hlsl";
		constexpr const char* kPreNGDFLightResourceNoOpSource = "LightLimitFix\\DFLightResourceNoOpPS.hlsl";
		constexpr const char* kPreNGDFLightFullContractNoOpSource = "LightLimitFix\\DFLightFullContractNoOpPS.hlsl";
		constexpr const char* kPreNGDFLightLLFAdditiveSource = "LightLimitFix\\DFLightLLFAdditivePS.hlsl";
		constexpr const char* kTraceLLFPSEnv = "FO4CS_TRACE_LLF_PS";
		constexpr std::uint32_t kPreNGDFLightVanillaFullShadowed920AsmHash = 0xFB077F61u;
		constexpr std::uint32_t kPreNGDFLightVanillaFullShadowed922AsmHash = 0xA2D7B576u;
		constexpr std::uint32_t kPreNGDefaultDFLightDrawStateProofSamples = 128;
		constexpr std::uint32_t kPreNGMaxDFLightDrawStateProofSamples = 8192;

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
		bool ParsePreNGEnvironmentUIntValue(const char* a_value, std::uint32_t& a_result)
		{
			if (!a_value || *a_value == '\0') {
				return false;
			}

			std::uint32_t parsed = 0;
			const auto* const end = a_value + std::strlen(a_value);
			const auto [ptr, ec] = std::from_chars(a_value, end, parsed);
			if (ec != std::errc{} || ptr != end) {
				return false;
			}

			a_result = parsed;
			return true;
		}

		std::optional<std::uint32_t> ReadPreNGEnvironmentUInt(const char* a_name)
		{
			char value[16]{};
			std::uint32_t parsed = 0;
			if (ReadPreNGRegistryEnvironmentValue(a_name, HKEY_CURRENT_USER, "Environment", value)) {
				return ParsePreNGEnvironmentUIntValue(value, parsed) ? std::optional<std::uint32_t>{ parsed } : std::nullopt;
			}

			if (ReadPreNGRegistryEnvironmentValue(
					a_name,
					HKEY_LOCAL_MACHINE,
					"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
					value)) {
				return ParsePreNGEnvironmentUIntValue(value, parsed) ? std::optional<std::uint32_t>{ parsed } : std::nullopt;
			}

			SetLastError(ERROR_SUCCESS);
			const auto length = GetEnvironmentVariableA(a_name, value, static_cast<DWORD>(sizeof(value)));
			if (length > 0) {
				return length < sizeof(value) && ParsePreNGEnvironmentUIntValue(value, parsed) ?
				           std::optional<std::uint32_t>{ parsed } :
				           std::nullopt;
			}
			if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
				return std::nullopt;
			}

			return std::nullopt;
		}

		bool ShouldMapPreNGShaderObjectMetadata()
		{
			static const bool enabled =
				ReadPreNGEnvironmentSwitch(kPreNGShaderObjectMetadataEnv) ||
				ReadPreNGEnvironmentSwitch(kPreNGShaderLookupDiagEnv);
			return enabled;
		}

		bool ShouldCapturePreNGDFLightVanillaDumpBytecode()
		{
			static const bool enabled =
				ReadPreNGEnvironmentSwitch(kPreNGDFLightVanillaDumpEnv) ||
				ReadPreNGEnvironmentSwitch(kPreNGDFCompositeVanillaDumpEnv);
			return enabled;
		}

		bool ShouldTracePreNGDFLightDrawState()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateEnv);
			return enabled;
		}

		bool ShouldRunPreNGDFLightZeroAdditivePass()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightZeroAdditivePassEnv);
			return enabled;
		}

		bool ShouldBindPreNGDFLightDrawStateStrictCB()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
			return enabled;
		}

		bool ShouldBindPreNGDFLightDrawStateClusterSRVs()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
			return enabled;
		}

		std::atomic_uint32_t& PreNGDFLightDrawStateProofSamples()
		{
			static std::atomic_uint32_t samples = 0;
			return samples;
		}

		std::atomic_bool& PreNGDFLightDrawStateProofComplete()
		{
			static std::atomic_bool complete = false;
			return complete;
		}

		std::atomic_bool& PreNGDFLightDrawStateProofLimitLogged()
		{
			static std::atomic_bool logged = false;
			return logged;
		}

		std::uint32_t GetPreNGDFLightDrawStateProofBudget()
		{
			static const std::uint32_t budget = [] {
				const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightDrawStateProofBudgetEnv);
				if (!configured) {
					return kPreNGDefaultDFLightDrawStateProofSamples;
				}
				if (*configured > kPreNGMaxDFLightDrawStateProofSamples) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight draw-state proof budget clamped env={} requested={} max={}",
						kPreNGDFLightDrawStateProofBudgetEnv,
						*configured,
						kPreNGMaxDFLightDrawStateProofSamples);
					return kPreNGMaxDFLightDrawStateProofSamples;
				}
				return *configured;
			}();
			return budget;
		}

		bool ShouldRunPreNGDFLightDrawStateProof()
		{
			return ShouldTracePreNGDFLightDrawState() ||
			       ShouldBindPreNGDFLightDrawStateStrictCB() ||
			       ShouldBindPreNGDFLightDrawStateClusterSRVs();
		}

		bool IsPreNGDFLightDrawStateProofOpen()
		{
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			return ShouldRunPreNGDFLightDrawStateProof() &&
			       budget > 0 &&
			       !PreNGDFLightDrawStateProofComplete().load(std::memory_order_relaxed) &&
			       PreNGDFLightDrawStateProofSamples().load(std::memory_order_relaxed) < budget;
		}

		std::optional<std::uint32_t> TryReservePreNGDFLightDrawStateProofSample()
		{
			if (!IsPreNGDFLightDrawStateProofOpen()) {
				return std::nullopt;
			}
			const auto sample = PreNGDFLightDrawStateProofSamples().fetch_add(1, std::memory_order_relaxed) + 1;
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			if (sample > budget) {
				if (!PreNGDFLightDrawStateProofLimitLogged().exchange(true, std::memory_order_relaxed)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight draw-state proof budget exhausted; holding draw-state bind/audit samples={} budget={} env={}",
						sample - 1,
						budget,
						kPreNGDFLightDrawStateProofBudgetEnv);
				}
				return std::nullopt;
			}
			return sample;
		}

		void MarkPreNGDFLightDrawStateProofComplete(std::uint32_t a_sample)
		{
			if (!PreNGDFLightDrawStateProofComplete().exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight draw-state proof complete; holding draw-state bind/audit after sample={} budget={}",
					a_sample,
					GetPreNGDFLightDrawStateProofBudget());
			}
		}

		void MaybeLogPreNGDFLightDrawStateProofBudgetReached(std::uint32_t a_sample)
		{
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			if (budget > 0 &&
				a_sample >= budget &&
				!PreNGDFLightDrawStateProofComplete().load(std::memory_order_relaxed) &&
				!PreNGDFLightDrawStateProofLimitLogged().exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight draw-state proof budget reached without complete audit; holding draw-state bind/audit samples={} budget={} env={}",
					a_sample,
					budget,
					kPreNGDFLightDrawStateProofBudgetEnv);
			}
		}

		bool ShouldRunPreNGDFLightResourceNoOpPass()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
			return enabled;
		}

		bool ShouldRunPreNGDFLightFullContractNoOpPass()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
			return enabled;
		}

		bool ShouldRunPreNGDFLightLLFAdditivePass()
		{
			static const bool enabled = [] {
				const bool requested = ReadPreNGEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
				const bool legacyProofAllowed = ReadPreNGEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
				if (requested && !legacyProofAllowed) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass held in draw hooks; this legacy proof path is not the Skyrim-CS LLF direction. Set {}=1 only to reproduce the old additive proof.",
						kPreNGDFLightLegacyAdditiveProofEnv);
				} else if (requested) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive legacy proof enabled in draw hooks; do not use this path for final LLF validation.");
				}
				return requested && legacyProofAllowed;
			}();
			return enabled;
		}

		bool ShouldPersistPreNGDFLightLLFAdditivePass()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightLLFAdditivePersistentEnv);
			return ShouldRunPreNGDFLightLLFAdditivePass() && enabled;
		}

		bool ShouldTrackPreNGDFLightDrawTargets()
		{
			return IsPreNGDFLightDrawStateProofOpen() ||
			       ShouldRunPreNGDFLightZeroAdditivePass() ||
			       ShouldRunPreNGDFLightResourceNoOpPass() ||
			       ShouldRunPreNGDFLightFullContractNoOpPass() ||
			       ShouldRunPreNGDFLightLLFAdditivePass();
		}

		struct RenderTargetInfo
		{
			bool valid = false;
			UINT width = 0;
			UINT height = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		};

		struct DrawContextHooks
		{
			PSSetShaderResourcesFn psSetShaderResources = nullptr;
			PSSetShaderFn psSetShader = nullptr;
			DrawIndexedFn drawIndexed = nullptr;
			DrawFn draw = nullptr;
			DrawIndexedInstancedFn drawIndexedInstanced = nullptr;
			DrawInstancedFn drawInstanced = nullptr;
			DrawAutoFn drawAuto = nullptr;
			DrawIndexedInstancedIndirectFn drawIndexedInstancedIndirect = nullptr;
			DrawInstancedIndirectFn drawInstancedIndirect = nullptr;
			IASetPrimitiveTopologyFn iaSetPrimitiveTopology = nullptr;
			OMSetRenderTargetsFn omSetRenderTargets = nullptr;
			RSSetViewportsFn rsSetViewports = nullptr;
			CopyResourceFn copyResource = nullptr;
			ExecuteCommandListFn executeCommandList = nullptr;
		};

		DrawContextHooks fallbackDrawContextHooks;
		std::unordered_map<std::uintptr_t, DrawContextHooks> drawContextHooksByVTable;
		ID3D11Device* observedD3D11Device = nullptr;
		ID3D11DeviceContext* observedImmediateContext = nullptr;
		ID3D11DeviceContext* observedRendererContext = nullptr;
		bool installedContextHooks = false;
		bool rendererContextUnavailableLogged = false;
		bool llfOnFrameLogged = false;
		std::mutex llfCandidateLock;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> llfCandidatePixelShaders;
		std::unordered_set<std::string> loggedLLFPixelCandidates;
		std::unordered_set<std::string> loggedLLFDrawHookKinds;
		std::unordered_set<std::string> loggedLLFContextHookKinds;
		std::unordered_set<std::string> loggedLLFDrawContexts;
		std::unordered_set<std::string> loggedLLFContextDiagnostics;
		std::unordered_set<std::string> loggedLLFMissingOriginals;
		std::unordered_set<std::string> loggedLLFPixelShaderBindings;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundObservedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTrackedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTargetPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundUnknownPixelShaders;
		std::unordered_set<ID3D11PixelShader*> loggedLLFUnknownPixelShaderBindings;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> countedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyReasons;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyDraws;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderNearTargetDraws;
		std::unordered_map<std::string, std::size_t> llfBoundPixelShaderSurveyReasonCounts;
		bool loggedLLFBoundPixelShaderSurveyLimit = false;
		bool loggedLLFBoundPixelShaderSurveyDrawLimit = false;
		bool loggedLLFBoundPixelShaderNearTargetDrawLimit = false;
		std::unordered_map<std::string, std::size_t> llfStateSnapshotCounts;
		std::unordered_map<std::uintptr_t, std::uintptr_t> llfDirectDrawTrampolines;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> observedPixelShaderMetadata;
		std::unordered_map<ID3D11DeviceContext*, ShaderCache::ShaderMetadata> llfBoundPixelShaderMetadataByContext;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> dflightDrawStatePixelShaders;
		std::unordered_map<ID3D11DeviceContext*, ShaderCache::ShaderMetadata> dflightDrawStateBoundPixelShaderByContext;
		std::unordered_set<std::string> loggedDFLightDrawStatePixelShaders;
		std::unordered_set<std::string> loggedDFLightDrawStateBindings;
		std::unordered_set<std::string> loggedDFLightDrawStateDraws;
		std::mutex dflightZeroAdditivePassLock;
		winrt::com_ptr<ID3D11PixelShader> dflightZeroAdditivePixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightResourceNoOpPixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightFullContractNoOpPixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightLLFAdditivePixelShader;
		winrt::com_ptr<ID3D11Buffer> dflightLLFAdditiveControlsCB;
		winrt::com_ptr<ID3D11BlendState> dflightZeroAdditiveBlendState;
		winrt::com_ptr<ID3D11DepthStencilState> dflightZeroAdditiveDepthState;
		bool dflightZeroAdditivePassResourcesAttempted = false;
		bool dflightResourceNoOpPassResourcesAttempted = false;
		bool dflightFullContractNoOpPassResourcesAttempted = false;
		bool dflightLLFAdditivePassResourcesAttempted = false;
		bool dflightLLFAdditiveControlsAttempted = false;
		bool dflightResourceNoOpPassResourcesReadyLogged = false;
		bool dflightFullContractNoOpPassResourcesReadyLogged = false;
		bool dflightLLFAdditivePassResourcesReadyLogged = false;
		bool dflightAdditiveDrawStatesAttempted = false;
		std::size_t dflightResourceNoOpPixelShaderBytecodeSize = 0;
		std::size_t dflightFullContractNoOpPixelShaderBytecodeSize = 0;
		std::size_t dflightLLFAdditivePixelShaderBytecodeSize = 0;
		std::atomic_uint32_t dflightZeroAdditivePassDrawCount = 0;
		std::atomic_uint32_t dflightResourceNoOpPassDrawCount = 0;
		std::atomic_uint32_t dflightFullContractNoOpPassDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameOrdinal = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameBudgetSkipCount = 0;
		std::atomic_bool dflightZeroAdditivePassLimitLogged = false;
		std::atomic_bool dflightResourceNoOpPassLimitLogged = false;
		std::atomic_bool dflightFullContractNoOpPassLimitLogged = false;
		std::atomic_bool dflightLLFAdditivePassLimitLogged = false;

		constexpr std::size_t kMaxLLFStateSnapshotsPerShaderKind = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyLogs = 96;
		constexpr std::size_t kLLFBoundPixelShaderSurveySummaryInterval = 128;
		constexpr std::size_t kMaxLLFUnknownPixelShaderBindingLogs = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyDrawLogs = 128;
		constexpr std::size_t kMaxLLFBoundPixelShaderNearTargetDrawLogs = 64;
		constexpr std::size_t kLLFBoundPixelShaderInventorySummaryInterval = 128;
		constexpr std::size_t kMaxDFLightDrawStateLogs = 24;
		constexpr std::uint32_t kMaxDFLightZeroAdditivePassDraws = 8;
		constexpr std::uint32_t kMaxDFLightResourceNoOpPassDraws = 8;
		constexpr std::uint32_t kMaxDFLightFullContractNoOpPassDraws = 8;
		constexpr std::uint32_t kDefaultDFLightLLFAdditivePassDraws = 8;
		constexpr std::uint32_t kMinDFLightLLFAdditivePassDraws = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassDraws = 4096;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassDrawLogs = 32;
		constexpr std::uint32_t kDFLightLLFAdditivePassDrawLogInterval = 256;
		constexpr std::uint32_t kDefaultDFLightLLFAdditivePassFrameDraws = 1;
		constexpr std::uint32_t kMinDFLightLLFAdditivePassFrameDraws = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassFrameDraws = 8;
		constexpr UINT kDFLightLLFAdditiveControlsCBSlot = 13;
		constexpr std::uint32_t kDefaultDFLightLLFAdditiveScale1024 = 64;
		constexpr std::uint32_t kMinDFLightLLFAdditiveScale1024 = 0;
		constexpr std::uint32_t kMaxDFLightLLFAdditiveScale1024 = 512;
		constexpr std::uint32_t kDefaultDFLightLLFAdditiveMaxLights = 32;
		constexpr std::uint32_t kMinDFLightLLFAdditiveMaxLights = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditiveMaxLights = 64;

		struct alignas(16) DFLightLLFAdditiveControls
		{
			float scale = static_cast<float>(kDefaultDFLightLLFAdditiveScale1024) / 1024.0f;
			std::uint32_t maxLights = kDefaultDFLightLLFAdditiveMaxLights;
			std::uint32_t flags = 0;
			std::uint32_t pad = 0;
		};
		static_assert(sizeof(DFLightLLFAdditiveControls) == 16);

		std::uint32_t GetPreNGDFLightLLFAdditivePassDrawBudget()
		{
			static const auto budget = []() {
				const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveBudgetEnv);
				auto resolved = configured.value_or(kDefaultDFLightLLFAdditivePassDraws);
				if (resolved < kMinDFLightLLFAdditivePassDraws) {
					resolved = kMinDFLightLLFAdditivePassDraws;
				} else if (resolved > kMaxDFLightLLFAdditivePassDraws) {
					resolved = kMaxDFLightLLFAdditivePassDraws;
				}

				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass draw budget resolved budget={} configured={} default={} min={} max={} env={}",
					resolved,
					configured ? std::to_string(*configured) : std::string("unset"),
					kDefaultDFLightLLFAdditivePassDraws,
					kMinDFLightLLFAdditivePassDraws,
					kMaxDFLightLLFAdditivePassDraws,
					kPreNGDFLightLLFAdditiveBudgetEnv);
				return resolved;
			}();
			return budget;
		}

		std::uint32_t GetPreNGDFLightLLFAdditivePassFrameBudget()
		{
			static const auto budget = []() {
				const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveFrameBudgetEnv);
				auto resolved = configured.value_or(kDefaultDFLightLLFAdditivePassFrameDraws);
				if (resolved < kMinDFLightLLFAdditivePassFrameDraws) {
					resolved = kMinDFLightLLFAdditivePassFrameDraws;
				} else if (resolved > kMaxDFLightLLFAdditivePassFrameDraws) {
					resolved = kMaxDFLightLLFAdditivePassFrameDraws;
				}

				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass per-frame draw budget resolved frameBudget={} configured={} default={} min={} max={} env={}",
					resolved,
					configured ? std::to_string(*configured) : std::string("unset"),
					kDefaultDFLightLLFAdditivePassFrameDraws,
					kMinDFLightLLFAdditivePassFrameDraws,
					kMaxDFLightLLFAdditivePassFrameDraws,
					kPreNGDFLightLLFAdditiveFrameBudgetEnv);
				return resolved;
			}();
			return budget;
		}

		std::uint32_t GetPreNGDFLightLLFAdditiveScale1024()
		{
			static const auto scale1024 = []() {
				const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveScale1024Env);
				auto resolved = configured.value_or(kDefaultDFLightLLFAdditiveScale1024);
				if (resolved < kMinDFLightLLFAdditiveScale1024) {
					resolved = kMinDFLightLLFAdditiveScale1024;
				} else if (resolved > kMaxDFLightLLFAdditiveScale1024) {
					resolved = kMaxDFLightLLFAdditiveScale1024;
				}

				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass scale resolved scale1024={} configured={} default={} min={} max={} env={}",
					resolved,
					configured ? std::to_string(*configured) : std::string("unset"),
					kDefaultDFLightLLFAdditiveScale1024,
					kMinDFLightLLFAdditiveScale1024,
					kMaxDFLightLLFAdditiveScale1024,
					kPreNGDFLightLLFAdditiveScale1024Env);
				return resolved;
			}();
			return scale1024;
		}

		std::uint32_t GetPreNGDFLightLLFAdditiveMaxLights()
		{
			static const auto maxLights = []() {
				const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveMaxLightsEnv);
				auto resolved = configured.value_or(kDefaultDFLightLLFAdditiveMaxLights);
				if (resolved < kMinDFLightLLFAdditiveMaxLights) {
					resolved = kMinDFLightLLFAdditiveMaxLights;
				} else if (resolved > kMaxDFLightLLFAdditiveMaxLights) {
					resolved = kMaxDFLightLLFAdditiveMaxLights;
				}

				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass max lights resolved maxLights={} configured={} default={} min={} max={} env={}",
					resolved,
					configured ? std::to_string(*configured) : std::string("unset"),
					kDefaultDFLightLLFAdditiveMaxLights,
					kMinDFLightLLFAdditiveMaxLights,
					kMaxDFLightLLFAdditiveMaxLights,
					kPreNGDFLightLLFAdditiveMaxLightsEnv);
				return resolved;
			}();
			return maxLights;
		}

		DFLightLLFAdditiveControls GetPreNGDFLightLLFAdditiveControls()
		{
			DFLightLLFAdditiveControls controls{};
			controls.scale = static_cast<float>(GetPreNGDFLightLLFAdditiveScale1024()) / 1024.0f;
			controls.maxLights = GetPreNGDFLightLLFAdditiveMaxLights();
			return controls;
		}

		bool TryReservePreNGDFLightLLFAdditivePassFrameDraw()
		{
			const auto frameBudget = GetPreNGDFLightLLFAdditivePassFrameBudget();
			auto current = dflightLLFAdditivePassFrameDrawCount.load(std::memory_order_relaxed);
			while (current < frameBudget) {
				if (dflightLLFAdditivePassFrameDrawCount.compare_exchange_weak(
						current,
						current + 1,
						std::memory_order_relaxed,
						std::memory_order_relaxed)) {
					return true;
				}
			}

			const auto skipIndex = dflightLLFAdditivePassFrameBudgetSkipCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if (skipIndex <= 8 || skipIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass per-frame draw budget reached skips={} frame={} frameBudget={} env={}",
					skipIndex,
					dflightLLFAdditivePassFrameOrdinal.load(std::memory_order_relaxed),
					frameBudget,
					kPreNGDFLightLLFAdditiveFrameBudgetEnv);
			}
			return false;
		}

		void AdvancePreNGDFLightLLFAdditivePassFrame()
		{
			if (!ShouldRunPreNGDFLightLLFAdditivePass()) {
				return;
			}

			dflightLLFAdditivePassFrameOrdinal.fetch_add(1, std::memory_order_relaxed);
			dflightLLFAdditivePassFrameDrawCount.store(0, std::memory_order_relaxed);
		}

		bool ShouldLogPreNGDFLightLLFAdditivePassDraw(std::uint32_t a_drawIndex, std::uint32_t a_drawLimit, bool a_persistent)
		{
			const auto oneBasedDrawIndex = a_drawIndex + 1;
			return oneBasedDrawIndex <= kMaxDFLightLLFAdditivePassDrawLogs ||
			       (!a_persistent && oneBasedDrawIndex == a_drawLimit) ||
			       (kDFLightLLFAdditivePassDrawLogInterval > 0 &&
			           oneBasedDrawIndex % kDFLightLLFAdditivePassDrawLogInterval == 0);
		}
		constexpr UINT kMaxLLFLoggedShaderResourceViews = 8;

		std::string FormatBufferSlots(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
				const auto size = a_metadata.constantBufferSizes[slot];
				if (size == 0) {
					continue;
				}

				if (!first) {
					result << ',';
				}
				result << slot << ':' << size;
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::string FormatTextureSlots(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index) {
				if (index > 0) {
					result << ',';
				}
				result << a_metadata.textureSlots[index];
			}

			return a_metadata.textureSlots.empty() ? "none" : result.str();
		}

		std::string FormatTextureDimensions(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			for (std::size_t index = 0; index < a_metadata.textureDimensions.size(); ++index) {
				if (index > 0) {
					result << ',';
				}
				const auto [dimension, slot] = a_metadata.textureDimensions[index];
				result << dimension << '@' << slot;
			}

			return a_metadata.textureDimensions.empty() ? "none" : result.str();
		}

		std::string FormatTextureSampleCounts(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot) {
				const auto count = a_metadata.textureSampleCounts[slot];
				if (count == 0) {
					continue;
				}
				if (!first) {
					result << ',';
				}
				result << slot << ':' << count;
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::uintptr_t ToAddress(const void* a_pointer)
		{
			return reinterpret_cast<std::uintptr_t>(a_pointer);
		}

		template <class T>
		std::uintptr_t ToFunctionAddress(T a_function)
		{
			static_assert(sizeof(T) == sizeof(std::uintptr_t));
			return std::bit_cast<std::uintptr_t>(a_function);
		}

		std::uintptr_t GetContextVTablePointer(ID3D11DeviceContext* a_context)
		{
			if (!a_context) {
				return 0;
			}

			return *reinterpret_cast<std::uintptr_t*>(a_context);
		}

		std::string FormatDrawVTableFunctions(std::uintptr_t a_vtable)
		{
			if (!a_vtable) {
				return "none";
			}

			const auto* entries = reinterpret_cast<const std::uintptr_t*>(a_vtable);
			return std::format(
				"8=0x{:X},9=0x{:X},12=0x{:X},13=0x{:X},20=0x{:X},21=0x{:X},24=0x{:X},33=0x{:X},38=0x{:X},39=0x{:X},40=0x{:X},44=0x{:X},47=0x{:X},58=0x{:X}",
				entries[8],
				entries[9],
				entries[12],
				entries[13],
				entries[20],
				entries[21],
				entries[24],
				entries[33],
				entries[38],
				entries[39],
				entries[40],
				entries[44],
				entries[47],
				entries[58]);
		}

		void TraceLightLimitFixContextDiagnostics(const char* a_source, const char* a_phase, ID3D11DeviceContext* a_context, const void* a_rendererData, const void* a_rendererDevice)
		{
			if (!a_context) {
				return;
			}

			const auto contextAddress = ToAddress(a_context);
			const auto vtable = GetContextVTablePointer(a_context);
			const auto functions = FormatDrawVTableFunctions(vtable);
			const bool sameAsImmediate = observedImmediateContext && a_context == observedImmediateContext;
			const auto key = std::format("{}:{}:{:X}:{:X}:{}", a_source, a_phase, contextAddress, vtable, functions);
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFContextDiagnostics.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] PreNG context diagnostics source={} phase={} context=0x{:X} vtable=0x{:X} immediateContext=0x{:X} sameAsImmediate={} rendererData=0x{:X} rendererDevice=0x{:X} funcs={}",
				a_source,
				a_phase,
				contextAddress,
				vtable,
				ToAddress(observedImmediateContext),
				sameAsImmediate,
				ToAddress(a_rendererData),
				ToAddress(a_rendererDevice),
				functions);
		}

		bool ShouldTraceLLFPixelCandidateDiagnostics()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kTraceLLFPSEnv);
			return enabled;
		}

		bool ShouldEnableLightLimitFixPixelCandidateDiagnostics()
		{
			return ShouldTraceLLFPixelCandidateDiagnostics() || ShouldTrackPreNGDFLightDrawTargets();
		}

		bool ShouldTraceLLFPixelCandidates(const ShaderCache& a_cache)
		{
			(void)a_cache;
			return ShouldTraceLLFPixelCandidateDiagnostics();
		}

		bool HasTextureSlot(const ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_slot)
		{
			for (const auto slot : a_metadata.textureSlots) {
				if (slot == a_slot) {
					return true;
				}
			}

			return false;
		}


		bool HasTextureDimension(const ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_dimension, std::uint32_t a_slot)
		{
			for (const auto [dimension, slot] : a_metadata.textureDimensions) {
				if (dimension == a_dimension && slot == a_slot) {
					return true;
				}
			}

			return false;
		}

		bool IsLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       !a_metadata.hasImmediateConstantBuffer &&
			       a_metadata.immediateConstantBufferRows == 0;
		}

		bool IsLightLimitFixPixelImmediateConstantNearTarget(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0);
		}

		bool IsLightLimitFixPixelTrackedCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return IsLightLimitFixPixelCandidate(a_metadata) ||
			       IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata);
		}

		bool IsLightLimitFixPixelSurveyMatch(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			return hasLightCB && hasLightingTexture && a_metadata.outputCount == 2;
		}

		bool IsPreNGDFLightVanillaFullShadowedShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return a_metadata.constantBufferSizes[2] == 448 &&
			       a_metadata.constantBufferSizes[12] == 496 &&
			       a_metadata.textureSlots.size() == 5 &&
			       a_metadata.textureSlotMask == 0x2Fu &&
			       HasTextureSlot(a_metadata, 0) &&
			       HasTextureSlot(a_metadata, 1) &&
			       HasTextureSlot(a_metadata, 2) &&
			       HasTextureSlot(a_metadata, 3) &&
			       HasTextureSlot(a_metadata, 5) &&
			       HasTextureDimension(a_metadata, 4, 0) &&
			       HasTextureDimension(a_metadata, 4, 1) &&
			       HasTextureDimension(a_metadata, 4, 2) &&
			       HasTextureDimension(a_metadata, 4, 3) &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[0] == 1 &&
			       a_metadata.textureSampleCounts[1] == 1 &&
			       a_metadata.textureSampleCounts[2] == 1 &&
			       a_metadata.textureSampleCounts[3] == 1 &&
			       a_metadata.textureSampleCounts[5] == 6 &&
			       a_metadata.inputTextureCount == 5 &&
			       a_metadata.inputCount == 1 &&
			       a_metadata.inputMask == 0x1 &&
			       a_metadata.outputCount == 2 &&
			       a_metadata.outputMask == 0x3 &&
			       a_metadata.sampleInstructionCount == 10 &&
			       a_metadata.hasImmediateConstantBuffer &&
			       a_metadata.immediateConstantBufferRows == 1000 &&
			       !a_metadata.hasDiscard;
		}

		bool IsPreNGDFLightLLFConsumerCandidateShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return a_metadata.constantBufferSizes[2] == 448 &&
			       a_metadata.constantBufferSizes[3] > 0 &&
			       a_metadata.constantBufferSizes[12] == 496 &&
			       a_metadata.textureSlots.size() >= 8 &&
			       HasTextureSlot(a_metadata, 0) &&
			       HasTextureSlot(a_metadata, 1) &&
			       HasTextureSlot(a_metadata, 2) &&
			       HasTextureSlot(a_metadata, 3) &&
			       HasTextureSlot(a_metadata, 5) &&
			       HasTextureSlot(a_metadata, 35) &&
			       HasTextureSlot(a_metadata, 36) &&
			       HasTextureSlot(a_metadata, 37) &&
			       HasTextureDimension(a_metadata, 4, 0) &&
			       HasTextureDimension(a_metadata, 4, 1) &&
			       HasTextureDimension(a_metadata, 4, 2) &&
			       HasTextureDimension(a_metadata, 4, 3) &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[0] == 1 &&
			       a_metadata.textureSampleCounts[1] == 1 &&
			       a_metadata.textureSampleCounts[2] == 1 &&
			       a_metadata.textureSampleCounts[3] == 1 &&
			       a_metadata.textureSampleCounts[5] == 6 &&
			       a_metadata.textureSampleCounts[35] > 0 &&
			       a_metadata.textureSampleCounts[36] > 0 &&
			       a_metadata.textureSampleCounts[37] > 0 &&
			       a_metadata.inputTextureCount >= 8 &&
			       a_metadata.inputCount == 1 &&
			       a_metadata.inputMask == 0x1 &&
			       a_metadata.outputCount == 2 &&
			       a_metadata.outputMask == 0x3 &&
			       a_metadata.sampleInstructionCount >= 10 &&
			       !a_metadata.hasDiscard;
		}

		bool IsPreNGDFLightDrawStateTarget(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool vanillaFullShadowedHash =
				a_metadata.asmHash == kPreNGDFLightVanillaFullShadowed920AsmHash ||
				a_metadata.asmHash == kPreNGDFLightVanillaFullShadowed922AsmHash;

			return (vanillaFullShadowedHash && IsPreNGDFLightVanillaFullShadowedShape(a_metadata)) ||
			       IsPreNGDFLightLLFConsumerCandidateShape(a_metadata);
		}

		std::string FormatLightLimitFixPixelShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTextureCubeAtSlot5 = HasTextureDimension(a_metadata, 5, 5);
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return std::format(
				"cb2={} t5={} tex5dim={} out2={} posInput={} target={} tracked={} samples={} t5samples={} immRows={}",
				hasLightCB,
				hasLightingTexture,
				hasTextureCubeAtSlot5,
				hasTwoOutputs,
				positionOnlyInput,
				IsLightLimitFixPixelCandidate(a_metadata),
				IsLightLimitFixPixelTrackedCandidate(a_metadata),
				a_metadata.sampleInstructionCount,
				a_metadata.textureSampleCounts[5],
				a_metadata.immediateConstantBufferRows);
		}

		std::string ClassifyLightLimitFixSurveyRejection(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "target";
			}

			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			if (!hasLightCB) {
				return "noCB2";
			}

			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			if (!hasLightingTexture) {
				return "noT5";
			}

			if (!HasTextureDimension(a_metadata, 5, 5)) {
				return "noT5Cube";
			}

			if (a_metadata.textureSampleCounts[5] == 0) {
				return "noT5Sample";
			}

			if (a_metadata.outputCount != 2) {
				return "output";
			}

			if (a_metadata.inputCount != 1 || a_metadata.inputMask != 0x1) {
				return "input";
			}

			if (a_metadata.hasDiscard) {
				return "discard";
			}

			if (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0) {
				return "immediateCB";
			}

			return "other";
		}

		std::string FormatLightLimitFixSurveyReasonCountsLocked()
		{
			constexpr std::string_view kReasonOrder[] = {
				"noCB2",
				"noT5",
				"noT5Cube",
				"noT5Sample",
				"output",
				"input",
				"discard",
				"immediateCB",
				"other"
			};

			std::ostringstream result;
			bool first = true;
			for (const auto reason : kReasonOrder) {
				const auto it = llfBoundPixelShaderSurveyReasonCounts.find(std::string(reason));
				if (it == llfBoundPixelShaderSurveyReasonCounts.end() || it->second == 0) {
					continue;
				}

				if (!first) {
					result << ',';
				}
				result << reason << ':' << it->second;
				first = false;
			}

			return first ? "none" : result.str();
		}

		bool ShouldSurveyBoundPixelShader(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return IsLightLimitFixPixelSurveyMatch(a_metadata) ||
			       hasLightCB ||
			       hasLightingTexture ||
			       (hasTwoOutputs && positionOnlyInput);
		}

		std::string ClassifyLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "fullscreen-lighting";
			}

			if (IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata)) {
				return "fullscreen-lighting-immediate";
			}

			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			if (!positionOnlyInput || a_metadata.hasDiscard) {
				return "geometry-material";
			}

			if (a_metadata.hasImmediateConstantBuffer) {
				return "fullscreen-other";
			}

			return HasTextureDimension(a_metadata, 5, 5) ? "fullscreen-lighting" : "fullscreen-other";
		}

		std::size_t CountTrackedLightLimitFixPixelTargetsLocked()
		{
			std::size_t result = 0;
			for (const auto& [shader, metadata] : llfCandidatePixelShaders) {
				if (IsLightLimitFixPixelCandidate(metadata)) {
					++result;
				}
			}

			return result;
		}

		void TrackLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_pixelShader) {
				return;
			}

			std::scoped_lock lock(llfCandidateLock);
			llfCandidatePixelShaders[a_pixelShader] = a_metadata;
		}

		void TrackObservedPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_pixelShader) {
				return;
			}

			std::scoped_lock lock(llfCandidateLock);
			observedPixelShaderMetadata[a_pixelShader] = a_metadata;
		}

		void TraceLightLimitFixPixelCandidate(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			const auto key = std::format("{}:{:08X}", a_metadata.uid, a_metadata.hash);
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFPixelCandidates.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] Candidate PS asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} device=0x{:X} shader=0x{:X} size={} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				a_metadata.asmHash,
				a_metadata.hash,
				a_metadata.uid,
				ClassifyLightLimitFixPixelCandidate(a_metadata),
				IsLightLimitFixPixelCandidate(a_metadata),
				ToAddress(a_device),
				ToAddress(a_pixelShader),
				a_metadata.size,
				FormatBufferSlots(a_metadata),
				FormatTextureSlots(a_metadata),
				FormatTextureDimensions(a_metadata),
				a_metadata.inputCount,
				a_metadata.outputCount,
				a_metadata.inputMask,
				a_metadata.outputMask,
				a_metadata.instructionCount,
				a_metadata.sampleInstructionCount,
				FormatTextureSampleCounts(a_metadata),
				a_metadata.hasDiscard,
				a_metadata.hasImmediateConstantBuffer,
				a_metadata.immediateConstantBufferRows);
		}

		std::optional<ShaderCache::ShaderMetadata> GetTrackedLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = llfCandidatePixelShaders.find(a_pixelShader); it != llfCandidatePixelShaders.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		std::optional<ShaderCache::ShaderMetadata> GetObservedPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = observedPixelShaderMetadata.find(a_pixelShader); it != observedPixelShaderMetadata.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		std::optional<ShaderCache::ShaderMetadata> GetTrackedPreNGDFLightDrawStatePixelShader(ID3D11PixelShader* a_pixelShader)
		{
			if (!a_pixelShader) {
				return std::nullopt;
			}

			std::scoped_lock lock(llfCandidateLock);
			if (auto it = dflightDrawStatePixelShaders.find(a_pixelShader); it != dflightDrawStatePixelShaders.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		void TrackPreNGDFLightDrawStatePixelShader(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_pixelShader || !ShouldTrackPreNGDFLightDrawTargets() || !IsPreNGDFLightDrawStateTarget(a_metadata)) {
				return;
			}

			const auto key = std::format("{}:{:08X}:{:X}", a_metadata.uid, a_metadata.asmHash, ToAddress(a_pixelShader));
			bool shouldLog = false;
			std::size_t trackedCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				dflightDrawStatePixelShaders[a_pixelShader] = a_metadata;
				if (loggedDFLightDrawStatePixelShaders.size() < kMaxDFLightDrawStateLogs) {
					shouldLog = loggedDFLightDrawStatePixelShaders.insert(key).second;
				}
				trackedCount = dflightDrawStatePixelShaders.size();
			}

			if (!shouldLog) {
				return;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state target PS observed asmHash=0x{:08X} hash=0x{:08X} uid={} device=0x{:X} shader=0x{:X} tracked={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} immediateRows={}",
				a_metadata.asmHash,
				a_metadata.hash,
				a_metadata.uid,
				ToAddress(a_device),
				ToAddress(a_pixelShader),
				trackedCount,
				FormatBufferSlots(a_metadata),
				FormatTextureSlots(a_metadata),
				FormatTextureDimensions(a_metadata),
				a_metadata.instructionCount,
				a_metadata.sampleInstructionCount,
				FormatTextureSampleCounts(a_metadata),
				a_metadata.immediateConstantBufferRows);
		}

		void TrackPreNGDFLightDrawStateBoundPixelShader(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !ShouldTrackPreNGDFLightDrawTargets()) {
				return;
			}

			const auto metadata = GetTrackedPreNGDFLightDrawStatePixelShader(a_pixelShader);
			bool shouldLog = false;
			std::size_t boundContextCount = 0;
			if (metadata) {
				const auto key = std::format("{}:{:08X}:{:X}:{:X}", metadata->uid, metadata->asmHash, ToAddress(a_context), ToAddress(a_pixelShader));
				std::scoped_lock lock(llfCandidateLock);
				dflightDrawStateBoundPixelShaderByContext[a_context] = *metadata;
				if (loggedDFLightDrawStateBindings.size() < kMaxDFLightDrawStateLogs) {
					shouldLog = loggedDFLightDrawStateBindings.insert(key).second;
				}
				boundContextCount = dflightDrawStateBoundPixelShaderByContext.size();
			} else {
				std::scoped_lock lock(llfCandidateLock);
				dflightDrawStateBoundPixelShaderByContext.erase(a_context);
			}

			if (!metadata || !shouldLog) {
				return;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state target PS bound asmHash=0x{:08X} hash=0x{:08X} uid={} context=0x{:X} vtable=0x{:X} shader=0x{:X} boundContexts={} buffers={} textures={} textureDims={} textureSamples={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader),
				boundContextCount,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				FormatTextureSampleCounts(*metadata),
				metadata->immediateConstantBufferRows);
		}

		std::optional<ShaderCache::ShaderMetadata> GetBoundPreNGDFLightDrawStatePixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context || !ShouldTrackPreNGDFLightDrawTargets()) {
				return std::nullopt;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = dflightDrawStateBoundPixelShaderByContext.find(a_context); it != dflightDrawStateBoundPixelShaderByContext.end()) {
					return it->second;
				}
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return std::nullopt;
			}

			auto metadata = GetTrackedPreNGDFLightDrawStatePixelShader(pixelShader.get());
			if (metadata) {
				TrackPreNGDFLightDrawStateBoundPixelShader(a_context, pixelShader.get());
			}
			return metadata;
		}

		void TrackLightLimitFixBoundPixelShader(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context) {
				return;
			}

			auto metadata = a_pixelShader ? GetTrackedLightLimitFixPixelShader(a_pixelShader) : std::nullopt;
			std::scoped_lock lock(llfCandidateLock);
			if (metadata) {
				llfBoundPixelShaderMetadataByContext[a_context] = *metadata;
			} else {
				llfBoundPixelShaderMetadataByContext.erase(a_context);
			}
		}

		std::optional<ShaderCache::ShaderMetadata> GetBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context) {
				return std::nullopt;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = llfBoundPixelShaderMetadataByContext.find(a_context); it != llfBoundPixelShaderMetadataByContext.end()) {
					return it->second;
				}
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return std::nullopt;
			}

			return GetTrackedLightLimitFixPixelShader(pixelShader.get());
		}

		bool HasCachedBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return false;
			}

			std::scoped_lock lock(llfCandidateLock);
			return llfBoundPixelShaderMetadataByContext.find(a_context) != llfBoundPixelShaderMetadataByContext.end();
		}

		void TraceLightLimitFixDrawHookHealth(const char* a_drawKind)
		{
			auto* cache = ShaderCache::GetSingleton();
			if (!ShouldTraceLLFPixelCandidates(*cache)) {
				return;
			}

			std::size_t trackedCandidateCount = 0;
			std::size_t trackedTargetCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFDrawHookKinds.insert(a_drawKind).second) {
					return;
				}
				trackedCandidateCount = llfCandidatePixelShaders.size();
				trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
			}

			logger::info(
				"[LightLimitFix] PreNG draw hook reached draw={} trackedCandidatePS={} trackedTargetPS={}",
				a_drawKind,
				trackedCandidateCount,
				trackedTargetCount);
		}

		void TraceLightLimitFixContextHookHealth(ID3D11DeviceContext* a_context, const char* a_hookKind)
		{
			auto* cache = ShaderCache::GetSingleton();
			if (!ShouldTraceLLFPixelCandidates(*cache)) {
				return;
			}

			std::size_t trackedCandidateCount = 0;
			std::size_t trackedTargetCount = 0;
			const auto key = std::format("{}:{:X}:{:X}", a_hookKind, ToAddress(a_context), GetContextVTablePointer(a_context));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFContextHookKinds.insert(key).second) {
					return;
				}
				trackedCandidateCount = llfCandidatePixelShaders.size();
				trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
			}

			logger::info(
				"[LightLimitFix] PreNG context hook reached kind={} context=0x{:X} vtable=0x{:X} trackedCandidatePS={} trackedTargetPS={}",
				a_hookKind,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				trackedCandidateCount,
				trackedTargetCount);
		}

		void TraceLightLimitFixPixelShaderBinding(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetTrackedLightLimitFixPixelShader(a_pixelShader);
			if (!metadata) {
				return;
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto key = std::format("{}:{:08X}:{:X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_context), ToAddress(a_pixelShader));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFPixelShaderBindings.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] Candidate PS bound asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader),
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		void TraceLightLimitFixBoundPixelShaderInventory(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetObservedPixelShader(a_pixelShader);
			const bool latestObserved = metadata.has_value();
			const bool latestTracked = metadata && IsLightLimitFixPixelTrackedCandidate(*metadata);
			const bool latestTarget = metadata && IsLightLimitFixPixelCandidate(*metadata);

			bool shouldLogSummary = false;
			bool shouldLogUnknownSample = false;
			std::size_t boundUniqueCount = 0;
			std::size_t observedBoundCount = 0;
			std::size_t unknownBoundCount = 0;
			std::size_t trackedBoundCount = 0;
			std::size_t targetBoundCount = 0;
			std::size_t observedCreatedCount = 0;
			std::size_t trackedCreatedCount = 0;
			std::size_t targetCreatedCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				const bool inserted = countedLLFBoundPixelShaders.insert(a_pixelShader).second;
				if (latestObserved) {
					countedLLFBoundObservedPixelShaders.insert(a_pixelShader);
				} else {
					countedLLFBoundUnknownPixelShaders.insert(a_pixelShader);
					if (loggedLLFUnknownPixelShaderBindings.size() < kMaxLLFUnknownPixelShaderBindingLogs &&
						loggedLLFUnknownPixelShaderBindings.insert(a_pixelShader).second) {
						shouldLogUnknownSample = true;
					}
				}

				if (latestTracked) {
					countedLLFBoundTrackedPixelShaders.insert(a_pixelShader);
				}

				if (latestTarget) {
					countedLLFBoundTargetPixelShaders.insert(a_pixelShader);
				}

				boundUniqueCount = countedLLFBoundPixelShaders.size();
				observedBoundCount = countedLLFBoundObservedPixelShaders.size();
				unknownBoundCount = countedLLFBoundUnknownPixelShaders.size();
				trackedBoundCount = countedLLFBoundTrackedPixelShaders.size();
				targetBoundCount = countedLLFBoundTargetPixelShaders.size();
				observedCreatedCount = observedPixelShaderMetadata.size();
				trackedCreatedCount = llfCandidatePixelShaders.size();
				targetCreatedCount = CountTrackedLightLimitFixPixelTargetsLocked();

				shouldLogSummary = inserted &&
					(boundUniqueCount == 1 ||
					 boundUniqueCount == 32 ||
					 boundUniqueCount == 64 ||
					 boundUniqueCount % kLLFBoundPixelShaderInventorySummaryInterval == 0 ||
					 latestTracked ||
					 latestTarget);
			}

			if (shouldLogUnknownSample) {
				logger::info(
					"[LightLimitFix] Bound PS unknown sample uniqueUnknown={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
					unknownBoundCount,
					ToAddress(a_context),
					GetContextVTablePointer(a_context),
					ToAddress(a_pixelShader));
			}

			if (!shouldLogSummary) {
				return;
			}

			logger::info(
				"[LightLimitFix] Bound PS inventory unique={} observed={} unknown={} trackedBound={} targetBound={} observedCreated={} trackedCreated={} targetCreated={} latestObserved={} latestTracked={} latestTarget={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
				boundUniqueCount,
				observedBoundCount,
				unknownBoundCount,
				trackedBoundCount,
				targetBoundCount,
				observedCreatedCount,
				trackedCreatedCount,
				targetCreatedCount,
				latestObserved,
				latestTracked,
				latestTarget,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader));
		}

		void TraceLightLimitFixBoundPixelShaderSurvey(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetObservedPixelShader(a_pixelShader);
			if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
				return;
			}

			const auto key = std::format("{}:{:08X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_pixelShader));
			const auto reason = ClassifyLightLimitFixSurveyRejection(*metadata);
			bool limitReached = false;
			bool shouldLogReasonSample = false;
			bool shouldLogSummary = false;
			bool shouldLog = false;
			std::size_t uniqueSurveyCount = 0;
			std::size_t exampleCount = 0;
			std::string reasonCounts;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!countedLLFBoundPixelShaderSurvey.insert(key).second) {
					return;
				}

				++llfBoundPixelShaderSurveyReasonCounts[reason];
				uniqueSurveyCount = countedLLFBoundPixelShaderSurvey.size();

				if (loggedLLFBoundPixelShaderSurveyReasons.insert(reason).second) {
					shouldLogReasonSample = true;
				}

				if (loggedLLFBoundPixelShaderSurvey.size() < kMaxLLFBoundPixelShaderSurveyLogs) {
					loggedLLFBoundPixelShaderSurvey.insert(key);
					shouldLog = true;
				} else if (!loggedLLFBoundPixelShaderSurveyLimit) {
					loggedLLFBoundPixelShaderSurveyLimit = true;
					limitReached = true;
				}

				exampleCount = loggedLLFBoundPixelShaderSurvey.size();
				if (uniqueSurveyCount == kMaxLLFBoundPixelShaderSurveyLogs + 1 ||
					(uniqueSurveyCount > kMaxLLFBoundPixelShaderSurveyLogs && uniqueSurveyCount % kLLFBoundPixelShaderSurveySummaryInterval == 0)) {
					shouldLogSummary = true;
					reasonCounts = FormatLightLimitFixSurveyReasonCountsLocked();
				}
			}

			if (shouldLogSummary) {
				logger::info(
					"[LightLimitFix] Bound PS survey reason summary unique={} examplesLogged={} counts={}",
					uniqueSurveyCount,
					exampleCount,
					reasonCounts);
			}

			if (shouldLogReasonSample && !shouldLog) {
				logger::info(
					"[LightLimitFix] Bound PS survey reason sample reason={} asmHash=0x{:08X} hash=0x{:08X} uid={} shape=\"{}\" buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
					reason,
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					FormatLightLimitFixPixelShape(*metadata),
					FormatBufferSlots(*metadata),
					FormatTextureSlots(*metadata),
					FormatTextureDimensions(*metadata),
					metadata->inputCount,
					metadata->outputCount,
					metadata->inputMask,
					metadata->outputMask,
					metadata->instructionCount,
					metadata->sampleInstructionCount,
					FormatTextureSampleCounts(*metadata),
					metadata->hasDiscard,
					metadata->hasImmediateConstantBuffer,
					metadata->immediateConstantBufferRows);
			}

			if (limitReached) {
				logger::info(
					"[LightLimitFix] Bound PS survey limit reached; suppressing additional noncandidate bound shader summaries limit={} unique={} counts={}",
					kMaxLLFBoundPixelShaderSurveyLogs,
					uniqueSurveyCount,
					reasonCounts.empty() ? "none" : reasonCounts);
			}
			if (!shouldLog) {
				return;
			}

			logger::info(
				"[LightLimitFix] Bound PS survey asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				reason,
				FormatLightLimitFixPixelShape(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader),
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->inputCount,
				metadata->outputCount,
				metadata->inputMask,
				metadata->outputMask,
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		RenderTargetInfo GetRenderTargetInfo(ID3D11RenderTargetView* a_renderTargetView)
		{
			RenderTargetInfo result;
			if (!a_renderTargetView) {
				return result;
			}

			result.valid = true;

			D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
			a_renderTargetView->GetDesc(&viewDesc);
			result.format = viewDesc.Format;

			ID3D11Resource* resource = nullptr;
			a_renderTargetView->GetResource(&resource);
			if (!resource) {
				return result;
			}

			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				result.width = textureDesc.Width;
				result.height = textureDesc.Height;
				if (result.format == DXGI_FORMAT_UNKNOWN) {
					result.format = textureDesc.Format;
				}
				texture->Release();
			}
			resource->Release();

			return result;
		}

		std::string FormatRenderTargetInfo(const RenderTargetInfo& a_info)
		{
			if (!a_info.valid) {
				return "none";
			}

			return std::format("{}x{}:fmt{}", a_info.width, a_info.height, static_cast<std::uint32_t>(a_info.format));
		}

		std::string FormatViewport(const D3D11_VIEWPORT& a_viewport, UINT a_viewportCount)
		{
			if (a_viewportCount == 0) {
				return "none";
			}

			return std::format(
				"{:.0f}x{:.0f}+{:.0f},{:.0f}",
				a_viewport.Width,
				a_viewport.Height,
				a_viewport.TopLeftX,
				a_viewport.TopLeftY);
		}

		std::string FormatShaderResourceViewInfo(ID3D11ShaderResourceView* a_shaderResourceView)
		{
			if (!a_shaderResourceView) {
				return "null";
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_shaderResourceView->GetDesc(&viewDesc);

			ID3D11Resource* resource = nullptr;
			a_shaderResourceView->GetResource(&resource);
			if (!resource) {
				return std::format("viewDim{}:no-resource", static_cast<std::uint32_t>(viewDesc.ViewDimension));
			}

			std::string result;
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				result = std::format(
					"viewDim{}:{}x{}:fmt{}",
					static_cast<std::uint32_t>(viewDesc.ViewDimension),
					textureDesc.Width,
					textureDesc.Height,
					static_cast<std::uint32_t>(textureDesc.Format));
				texture->Release();
			} else {
				D3D11_RESOURCE_DIMENSION resourceDimension{};
				resource->GetType(&resourceDimension);
				result = std::format(
					"viewDim{}:resourceDim{}:fmt{}",
					static_cast<std::uint32_t>(viewDesc.ViewDimension),
					static_cast<std::uint32_t>(resourceDimension),
					static_cast<std::uint32_t>(viewDesc.Format));
			}

			resource->Release();
			return result;
		}

		std::string FormatShaderResourceViews(UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews)
		{
			if (!a_shaderResourceViews || a_viewCount == 0) {
				return "none";
			}

			std::ostringstream result;
			const auto loggedViewCount = a_viewCount < kMaxLLFLoggedShaderResourceViews ? a_viewCount : kMaxLLFLoggedShaderResourceViews;
			for (UINT index = 0; index < loggedViewCount; ++index) {
				if (index > 0) {
					result << ',';
				}
				result << (a_startSlot + index) << '=' << FormatShaderResourceViewInfo(a_shaderResourceViews[index]);
			}
			if (a_viewCount > loggedViewCount) {
				result << ",...+" << (a_viewCount - loggedViewCount);
			}
			return result.str();
		}

		std::string FormatConstantBufferInfo(ID3D11Buffer* a_buffer)
		{
			if (!a_buffer) {
				return "null";
			}

			D3D11_BUFFER_DESC desc{};
			a_buffer->GetDesc(&desc);
			return std::format(
				"{}:usage{}:bind0x{:X}:cpu0x{:X}",
				desc.ByteWidth,
				static_cast<std::uint32_t>(desc.Usage),
				desc.BindFlags,
				desc.CPUAccessFlags);
		}

		std::string FormatCurrentPixelShaderConstantBuffers(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_context) {
				return "none";
			}

			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
				if (a_metadata.constantBufferSizes[slot] == 0 || slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
					continue;
				}

				ID3D11Buffer* buffer = nullptr;
				a_context->PSGetConstantBuffers(static_cast<UINT>(slot), 1, &buffer);
				if (!first) {
					result << ',';
				}
				result << "cb" << slot << '=' << FormatConstantBufferInfo(buffer);
				if (buffer) {
					buffer->Release();
				}
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::string FormatCurrentPixelShaderResourceViews(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_context || a_metadata.textureSlots.empty()) {
				return "none";
			}

			std::ostringstream result;
			bool first = true;
			std::unordered_set<std::uint32_t> seenSlots;
			for (const auto slot : a_metadata.textureSlots) {
				if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT || !seenSlots.insert(slot).second) {
					continue;
				}

				ID3D11ShaderResourceView* view = nullptr;
				a_context->PSGetShaderResources(slot, 1, &view);
				if (!first) {
					result << ',';
				}
				result << slot << '=' << FormatShaderResourceViewInfo(view);
				if (view) {
					view->Release();
				}
				first = false;
			}

			return first ? "none" : result.str();
		}

		void TracePreNGDFLightDrawStateContext(ID3D11DeviceContext* a_context, const char* a_drawKind, std::string_view a_drawCounts)
		{
			if (!a_context || !ShouldRunPreNGDFLightDrawStateProof() || !IsPreNGDFLightDrawStateProofOpen()) {
				return;
			}

			const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
			if (!metadata) {
				return;
			}

			const auto proofSample = TryReservePreNGDFLightDrawStateProofSample();
			if (!proofSample) {
				return;
			}

			if (globals::features::lightLimitFix.loaded) {
				const auto strictState = globals::features::lightLimitFix.BindPreNGDFLightDrawStateStrictLightCB(a_context);
				globals::features::lightLimitFix.BindPreNGDFLightDrawStateClusterSRVs(a_context, strictState.strictCBBound);
			}

			bool bindingComplete = false;
			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			const auto pixelShaderAddress = ToAddress(pixelShader.get());
			if (globals::features::lightLimitFix.loaded) {
				bindingComplete = globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
					"dflight-draw-state",
					4,
					0,
					0,
					pixelShaderAddress != 0,
					pixelShaderAddress,
					a_context);
			}
			if (bindingComplete) {
				MarkPreNGDFLightDrawStateProofComplete(*proofSample);
				return;
			}
			MaybeLogPreNGDFLightDrawStateProofBudgetReached(*proofSample);

			if (!ShouldTracePreNGDFLightDrawState()) {
				return;
			}

			D3D11_PRIMITIVE_TOPOLOGY topology{};
			a_context->IAGetPrimitiveTopology(&topology);

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			a_context->RSGetViewports(&viewportCount, &viewport);
			const auto viewportDescription = FormatViewport(viewport, viewportCount);

			ID3D11RenderTargetView* renderTargets[2]{};
			a_context->OMGetRenderTargets(2, renderTargets, nullptr);
			const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
			const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
			for (auto* renderTarget : renderTargets) {
				if (renderTarget) {
					renderTarget->Release();
				}
			}

			const auto rt0Description = FormatRenderTargetInfo(rt0);
			const auto rt1Description = FormatRenderTargetInfo(rt1);
			const auto counts = std::string{ a_drawCounts };
			const auto key = std::format(
				"{:08X}:{}:{}:{}:{}:{}:{}",
				metadata->asmHash,
				metadata->uid,
				a_drawKind,
				counts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description + ":" + rt1Description);

			bool shouldLog = false;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (loggedDFLightDrawStateDraws.size() < kMaxDFLightDrawStateLogs) {
					shouldLog = loggedDFLightDrawStateDraws.insert(key).second;
				}
			}

			if (!shouldLog) {
				return;
			}

			const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
			const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state draw asmHash=0x{:08X} hash=0x{:08X} uid={} draw={} counts={} context=0x{:X} vtable=0x{:X} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} immediateRows={} boundCBs={} boundSRVs={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				a_drawKind,
				counts,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->immediateConstantBufferRows,
				boundConstantBuffers,
				boundShaderResources);
		}

		bool EnsurePreNGDFLightAdditiveNoOpDrawStates(ID3D11Device* a_device, const char* a_label)
		{
			if (!a_device) {
				return false;
			}

			if (dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				return true;
			}
			if (dflightAdditiveDrawStatesAttempted) {
				return false;
			}
			dflightAdditiveDrawStatesAttempted = true;

			D3D11_BLEND_DESC blendDesc{};
			auto& targetBlend = blendDesc.RenderTarget[0];
			targetBlend.BlendEnable = TRUE;
			targetBlend.SrcBlend = D3D11_BLEND_ONE;
			targetBlend.DestBlend = D3D11_BLEND_ONE;
			targetBlend.BlendOp = D3D11_BLEND_OP_ADD;
			targetBlend.SrcBlendAlpha = D3D11_BLEND_ZERO;
			targetBlend.DestBlendAlpha = D3D11_BLEND_ONE;
			targetBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			targetBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			auto hr = a_device->CreateBlendState(&blendDesc, dflightZeroAdditiveBlendState.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight {} additive blend state create failed hr=0x{:08X} pass=held",
					a_label,
					static_cast<std::uint32_t>(hr));
				return false;
			}

			D3D11_DEPTH_STENCIL_DESC depthDesc{};
			depthDesc.DepthEnable = FALSE;
			depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
			depthDesc.StencilEnable = FALSE;
			depthDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			depthDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			depthDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			depthDesc.BackFace = depthDesc.FrontFace;

			hr = a_device->CreateDepthStencilState(&depthDesc, dflightZeroAdditiveDepthState.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight {} additive depth state create failed hr=0x{:08X} pass=held",
					a_label,
					static_cast<std::uint32_t>(hr));
				return false;
			}

			return true;
		}

		bool EnsurePreNGDFLightZeroAdditivePassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightZeroAdditivePixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				return true;
			}
			if (dflightZeroAdditivePassResourcesAttempted) {
				return false;
			}
			dflightZeroAdditivePassResourcesAttempted = true;

			auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
				kPreNGDFLightZeroAdditiveSource,
				"ps_5_0",
				nullptr,
				"main");
			if (!bytecode) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight zero-additive pass shader compile failed source={} pass=held",
					kPreNGDFLightZeroAdditiveSource);
				return false;
			}

			auto hr = createPixelShader ?
				createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightZeroAdditivePixelShader.put()) :
				a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightZeroAdditivePixelShader.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight zero-additive pass pixel shader create failed hr=0x{:08X} pass=held",
					static_cast<std::uint32_t>(hr));
				return false;
			}
			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "zero-additive pass")) {
				return false;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight zero-additive pass resources ready source={} bytecode={}",
				kPreNGDFLightZeroAdditiveSource,
				bytecode->size());
			return true;
		}

		bool EnsurePreNGDFLightResourceNoOpPassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightResourceNoOpPixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				if (!dflightResourceNoOpPassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight resource no-op pass resources ready source={} bytecode={}",
						kPreNGDFLightResourceNoOpSource,
						dflightResourceNoOpPixelShaderBytecodeSize);
					dflightResourceNoOpPassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightResourceNoOpPixelShader) {
				if (dflightResourceNoOpPassResourcesAttempted) {
					return false;
				}
				dflightResourceNoOpPassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightResourceNoOpSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight resource no-op pass shader compile failed source={} pass=held",
						kPreNGDFLightResourceNoOpSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightResourceNoOpPixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightResourceNoOpPixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight resource no-op pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightResourceNoOpPixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "resource no-op pass")) {
				return false;
			}
			if (!dflightResourceNoOpPassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight resource no-op pass resources ready source={} bytecode={}",
					kPreNGDFLightResourceNoOpSource,
					dflightResourceNoOpPixelShaderBytecodeSize);
				dflightResourceNoOpPassResourcesReadyLogged = true;
			}
			return true;
		}

		bool EnsurePreNGDFLightFullContractNoOpPassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightFullContractNoOpPixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				if (!dflightFullContractNoOpPassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight full contract no-op pass resources ready source={} bytecode={}",
						kPreNGDFLightFullContractNoOpSource,
						dflightFullContractNoOpPixelShaderBytecodeSize);
					dflightFullContractNoOpPassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightFullContractNoOpPixelShader) {
				if (dflightFullContractNoOpPassResourcesAttempted) {
					return false;
				}
				dflightFullContractNoOpPassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightFullContractNoOpSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight full contract no-op pass shader compile failed source={} pass=held",
						kPreNGDFLightFullContractNoOpSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightFullContractNoOpPixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightFullContractNoOpPixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight full contract no-op pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightFullContractNoOpPixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "full contract no-op pass")) {
				return false;
			}
			if (!dflightFullContractNoOpPassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight full contract no-op pass resources ready source={} bytecode={}",
					kPreNGDFLightFullContractNoOpSource,
					dflightFullContractNoOpPixelShaderBytecodeSize);
				dflightFullContractNoOpPassResourcesReadyLogged = true;
			}
			return true;
		}

		bool EnsurePreNGDFLightLLFAdditivePassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightLLFAdditivePixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState && dflightLLFAdditiveControlsCB) {
				if (!dflightLLFAdditivePassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass resources ready source={} bytecode={}",
						kPreNGDFLightLLFAdditiveSource,
						dflightLLFAdditivePixelShaderBytecodeSize);
					dflightLLFAdditivePassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightLLFAdditivePixelShader) {
				if (dflightLLFAdditivePassResourcesAttempted) {
					return false;
				}
				dflightLLFAdditivePassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightLLFAdditiveSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass shader compile failed source={} pass=held",
						kPreNGDFLightLLFAdditiveSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightLLFAdditivePixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightLLFAdditivePixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightLLFAdditivePixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "LLF additive pass")) {
				return false;
			}
			if (!dflightLLFAdditiveControlsCB) {
				if (dflightLLFAdditiveControlsAttempted) {
					return false;
				}
				dflightLLFAdditiveControlsAttempted = true;

				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(DFLightLLFAdditiveControls);
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

				const auto controls = GetPreNGDFLightLLFAdditiveControls();
				D3D11_SUBRESOURCE_DATA initialData{};
				initialData.pSysMem = &controls;
				const auto hr = a_device->CreateBuffer(&desc, &initialData, dflightLLFAdditiveControlsCB.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass controls CB create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
			}
			if (!dflightLLFAdditivePassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass resources ready source={} bytecode={}",
					kPreNGDFLightLLFAdditiveSource,
					dflightLLFAdditivePixelShaderBytecodeSize);
				dflightLLFAdditivePassResourcesReadyLogged = true;
			}
			return true;
		}

		void RunPreNGDFLightZeroAdditivePass(
			ID3D11DeviceContext* a_context,
			DrawIndexedFn a_originalDrawIndexed,
			UINT a_indexCount,
			UINT a_startIndexLocation,
			INT a_baseVertexLocation)
		{
			if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightZeroAdditivePass()) {
				return;
			}

			const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
			if (!metadata) {
				return;
			}

			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device || !EnsurePreNGDFLightZeroAdditivePassResources(device.get())) {
				return;
			}

			const auto drawIndex = dflightZeroAdditivePassDrawCount.fetch_add(1);
			if (drawIndex >= kMaxDFLightZeroAdditivePassDraws) {
				if (!dflightZeroAdditivePassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight zero-additive pass draw limit reached limit={}",
						kMaxDFLightZeroAdditivePassDraws);
				}
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
			UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
			a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

			winrt::com_ptr<ID3D11BlendState> oldBlendState;
			FLOAT oldBlendFactor[4]{};
			UINT oldSampleMask = 0;
			a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

			winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
			UINT oldStencilRef = 0;
			a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

			FLOAT additiveBlendFactor[4]{};
			a_context->PSSetShader(dflightZeroAdditivePixelShader.get(), nullptr, 0);
			a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
			a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

			a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);

			a_context->PSSetShader(
				oldPixelShader.get(),
				oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
				oldClassInstanceCount);
			a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
			a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);

			for (UINT index = 0; index < oldClassInstanceCount; ++index) {
				if (oldClassInstances[index]) {
					oldClassInstances[index]->Release();
				}
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight zero-additive pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				drawIndex + 1,
				a_indexCount,
				a_startIndexLocation,
				a_baseVertexLocation,
				ToAddress(a_context),
				GetContextVTablePointer(a_context));
		}

		void RunPreNGDFLightResourceNoOpPass(
			ID3D11DeviceContext* a_context,
			DrawIndexedFn a_originalDrawIndexed,
			UINT a_indexCount,
			UINT a_startIndexLocation,
			INT a_baseVertexLocation)
		{
			if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightResourceNoOpPass()) {
				return;
			}

			const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
			if (!metadata || !globals::features::lightLimitFix.loaded) {
				return;
			}

			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device || !EnsurePreNGDFLightResourceNoOpPassResources(device.get())) {
				return;
			}
			if (dflightResourceNoOpPassDrawCount.load(std::memory_order_relaxed) >= kMaxDFLightResourceNoOpPassDraws) {
				if (!dflightResourceNoOpPassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight resource no-op pass draw limit reached limit={}",
						kMaxDFLightResourceNoOpPassDraws);
				}
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
			UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
			a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

			winrt::com_ptr<ID3D11BlendState> oldBlendState;
			FLOAT oldBlendFactor[4]{};
			UINT oldSampleMask = 0;
			a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

			winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
			UINT oldStencilRef = 0;
			a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

			winrt::com_ptr<ID3D11Buffer> oldStrictCB;
			a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
			std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
			a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			FLOAT additiveBlendFactor[4]{};
			a_context->PSSetShader(dflightResourceNoOpPixelShader.get(), nullptr, 0);
			a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
			a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

			const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightResourceNoOpPass(a_context);
			if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
				const auto drawIndex = dflightResourceNoOpPassDrawCount.fetch_add(1);
				if (drawIndex < kMaxDFLightResourceNoOpPassDraws) {
					a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
					logger::info(
						"[LightLimitFix] PreNG DFLight resource no-op pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
						metadata->asmHash,
						metadata->hash,
						metadata->uid,
						drawIndex + 1,
						a_indexCount,
						a_startIndexLocation,
						a_baseVertexLocation,
						ToAddress(a_context),
						GetContextVTablePointer(a_context),
						resourceState.lightCount,
						resourceState.strictLightCount,
						resourceState.shadowBitMask);
				} else if (!dflightResourceNoOpPassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight resource no-op pass draw limit reached limit={}",
						kMaxDFLightResourceNoOpPassDraws);
				}
			}

			a_context->PSSetShader(
				oldPixelShader.get(),
				oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
				oldClassInstanceCount);
			a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
			a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
			ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
			a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
			a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			for (UINT index = 0; index < oldClassInstanceCount; ++index) {
				if (oldClassInstances[index]) {
					oldClassInstances[index]->Release();
				}
			}
			for (auto* oldSRV : oldClusterSRVs) {
				if (oldSRV) {
					oldSRV->Release();
				}
			}
		}

		void RunPreNGDFLightFullContractNoOpPass(
			ID3D11DeviceContext* a_context,
			DrawIndexedFn a_originalDrawIndexed,
			UINT a_indexCount,
			UINT a_startIndexLocation,
			INT a_baseVertexLocation)
		{
			if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightFullContractNoOpPass()) {
				return;
			}

			const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
			if (!metadata || !globals::features::lightLimitFix.loaded) {
				return;
			}

			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device || !EnsurePreNGDFLightFullContractNoOpPassResources(device.get())) {
				return;
			}
			if (dflightFullContractNoOpPassDrawCount.load(std::memory_order_relaxed) >= kMaxDFLightFullContractNoOpPassDraws) {
				if (!dflightFullContractNoOpPassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight full contract no-op pass draw limit reached limit={}",
						kMaxDFLightFullContractNoOpPassDraws);
				}
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
			UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
			a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

			winrt::com_ptr<ID3D11BlendState> oldBlendState;
			FLOAT oldBlendFactor[4]{};
			UINT oldSampleMask = 0;
			a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

			winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
			UINT oldStencilRef = 0;
			a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

			winrt::com_ptr<ID3D11Buffer> oldStrictCB;
			a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
			std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
			a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			FLOAT additiveBlendFactor[4]{};
			a_context->PSSetShader(dflightFullContractNoOpPixelShader.get(), nullptr, 0);
			a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
			a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

			const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightFullContractNoOpPass(a_context);
			if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
				const auto drawIndex = dflightFullContractNoOpPassDrawCount.fetch_add(1);
				if (drawIndex < kMaxDFLightFullContractNoOpPassDraws) {
					a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
					logger::info(
						"[LightLimitFix] PreNG DFLight full contract no-op pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
						metadata->asmHash,
						metadata->hash,
						metadata->uid,
						drawIndex + 1,
						a_indexCount,
						a_startIndexLocation,
						a_baseVertexLocation,
						ToAddress(a_context),
						GetContextVTablePointer(a_context),
						resourceState.lightCount,
						resourceState.strictLightCount,
						resourceState.shadowBitMask);
				} else if (!dflightFullContractNoOpPassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight full contract no-op pass draw limit reached limit={}",
						kMaxDFLightFullContractNoOpPassDraws);
				}
			}

			a_context->PSSetShader(
				oldPixelShader.get(),
				oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
				oldClassInstanceCount);
			a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
			a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
			ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
			a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
			a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			for (UINT index = 0; index < oldClassInstanceCount; ++index) {
				if (oldClassInstances[index]) {
					oldClassInstances[index]->Release();
				}
			}
			for (auto* oldSRV : oldClusterSRVs) {
				if (oldSRV) {
					oldSRV->Release();
				}
			}
		}

		void RunPreNGDFLightLLFAdditivePass(
			ID3D11DeviceContext* a_context,
			DrawIndexedFn a_originalDrawIndexed,
			UINT a_indexCount,
			UINT a_startIndexLocation,
			INT a_baseVertexLocation)
		{
			if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightLLFAdditivePass()) {
				return;
			}

			const auto drawLimit = GetPreNGDFLightLLFAdditivePassDrawBudget();
			const bool persistent = ShouldPersistPreNGDFLightLLFAdditivePass();
			const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
			if (!metadata || !globals::features::lightLimitFix.loaded) {
				return;
			}

			if (!persistent && dflightLLFAdditivePassDrawCount.load(std::memory_order_relaxed) >= drawLimit) {
				if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
						drawLimit);
				}
				return;
			}

			if (!TryReservePreNGDFLightLLFAdditivePassFrameDraw()) {
				return;
			}

			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device || !EnsurePreNGDFLightLLFAdditivePassResources(device.get())) {
				return;
			}
			if (!persistent && dflightLLFAdditivePassDrawCount.load(std::memory_order_relaxed) >= drawLimit) {
				if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
						drawLimit);
				}
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
			UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
			a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

			winrt::com_ptr<ID3D11BlendState> oldBlendState;
			FLOAT oldBlendFactor[4]{};
			UINT oldSampleMask = 0;
			a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

			winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
			UINT oldStencilRef = 0;
			a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

			winrt::com_ptr<ID3D11Buffer> oldStrictCB;
			a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
			winrt::com_ptr<ID3D11Buffer> oldControlsCB;
			a_context->PSGetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, oldControlsCB.put());
			std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
			a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			FLOAT additiveBlendFactor[4]{};
			a_context->PSSetShader(dflightLLFAdditivePixelShader.get(), nullptr, 0);
			a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
			a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

			const auto controls = GetPreNGDFLightLLFAdditiveControls();
			a_context->UpdateSubresource(dflightLLFAdditiveControlsCB.get(), 0, nullptr, &controls, 0, 0);
			ID3D11Buffer* controlsCB = dflightLLFAdditiveControlsCB.get();
			a_context->PSSetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, &controlsCB);

			const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightLLFAdditivePass(a_context);
			if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
				const auto drawIndex = dflightLLFAdditivePassDrawCount.fetch_add(1);
				if (persistent || drawIndex < drawLimit) {
					a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
					if (ShouldLogPreNGDFLightLLFAdditivePassDraw(drawIndex, drawLimit, persistent)) {
						logger::info(
							"[LightLimitFix] PreNG DFLight LLF additive pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
							metadata->asmHash,
							metadata->hash,
							metadata->uid,
							drawIndex + 1,
							a_indexCount,
							a_startIndexLocation,
							a_baseVertexLocation,
							ToAddress(a_context),
							GetContextVTablePointer(a_context),
							resourceState.lightCount,
							resourceState.strictLightCount,
							resourceState.shadowBitMask);
					}
				} else if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
						drawLimit);
				}
			}

			a_context->PSSetShader(
				oldPixelShader.get(),
				oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
				oldClassInstanceCount);
			a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
			a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
			ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
			a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
			ID3D11Buffer* oldControlsCBRaw = oldControlsCB.get();
			a_context->PSSetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, &oldControlsCBRaw);
			a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

			for (UINT index = 0; index < oldClassInstanceCount; ++index) {
				if (oldClassInstances[index]) {
					oldClassInstances[index]->Release();
				}
			}
			for (auto* oldSRV : oldClusterSRVs) {
				if (oldSRV) {
					oldSRV->Release();
				}
			}
		}

		void TraceLightLimitFixStateContext(ID3D11DeviceContext* a_context, const char* a_stateKind, std::string_view a_stateDetails)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetBoundLightLimitFixPixelShader(a_context);
			if (!metadata) {
				return;
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto snapshotKey = std::format("{:08X}:{}:{}", metadata->asmHash, category, a_stateKind);
			std::size_t snapshotCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				snapshotCount = ++llfStateSnapshotCounts[snapshotKey];
			}
			if (snapshotCount > kMaxLLFStateSnapshotsPerShaderKind) {
				if (snapshotCount == kMaxLLFStateSnapshotsPerShaderKind + 1) {
					logger::info(
						"[LightLimitFix] Candidate PS state limit reached asmHash=0x{:08X} uid={} category={} target={} kind={} limit={}",
						metadata->asmHash,
						metadata->uid,
						category,
						IsLightLimitFixPixelCandidate(*metadata),
						a_stateKind,
						kMaxLLFStateSnapshotsPerShaderKind);
				}
				return;
			}

			D3D11_PRIMITIVE_TOPOLOGY topology{};
			a_context->IAGetPrimitiveTopology(&topology);

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			a_context->RSGetViewports(&viewportCount, &viewport);
			const auto viewportDescription = FormatViewport(viewport, viewportCount);

			ID3D11RenderTargetView* renderTargets[2]{};
			a_context->OMGetRenderTargets(2, renderTargets, nullptr);
			const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
			const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
			for (auto* renderTarget : renderTargets) {
				if (renderTarget) {
					renderTarget->Release();
				}
			}

			const auto rt0Description = FormatRenderTargetInfo(rt0);
			const auto rt1Description = FormatRenderTargetInfo(rt1);
			const auto details = std::string{ a_stateDetails };

			logger::info(
				"[LightLimitFix] Candidate PS state kind={} asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} topology={} viewport={} rt0={} rt1={} state={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				a_stateKind,
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				details,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		void TraceLightLimitFixDrawContext(ID3D11DeviceContext* a_context, const char* a_drawKind, std::string_view a_drawCounts)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return;
			}

			auto metadata = GetTrackedLightLimitFixPixelShader(pixelShader.get());
			bool surveyDraw = false;
			std::string surveyReason;
			if (!metadata) {
				metadata = GetObservedPixelShader(pixelShader.get());
				if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
					return;
				}

				surveyDraw = true;
				surveyReason = ClassifyLightLimitFixSurveyRejection(*metadata);
			}

			D3D11_PRIMITIVE_TOPOLOGY topology{};
			a_context->IAGetPrimitiveTopology(&topology);

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			a_context->RSGetViewports(&viewportCount, &viewport);
			const auto viewportDescription = FormatViewport(viewport, viewportCount);

			ID3D11RenderTargetView* renderTargets[2]{};
			a_context->OMGetRenderTargets(2, renderTargets, nullptr);
			const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
			const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
			for (auto* renderTarget : renderTargets) {
				if (renderTarget) {
					renderTarget->Release();
				}
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto rt0Description = FormatRenderTargetInfo(rt0);
			const auto rt1Description = FormatRenderTargetInfo(rt1);
			const auto key = std::format(
				"{:08X}:{}:{}:{}:{}:{}:{}:{}",
				metadata->asmHash,
				metadata->uid,
				category,
				a_drawKind,
				a_drawCounts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description + ":" + rt1Description);

			if (surveyDraw && IsLightLimitFixPixelImmediateConstantNearTarget(*metadata)) {
				bool shouldLogNearTargetDraw = false;
				bool shouldLogNearTargetDrawLimit = false;
				std::size_t nearTargetDrawUniqueCount = 0;
				{
					std::scoped_lock lock(llfCandidateLock);
					if (loggedLLFBoundPixelShaderNearTargetDraws.size() < kMaxLLFBoundPixelShaderNearTargetDrawLogs) {
						shouldLogNearTargetDraw = loggedLLFBoundPixelShaderNearTargetDraws.insert(key).second;
					} else if (!loggedLLFBoundPixelShaderNearTargetDrawLimit) {
						loggedLLFBoundPixelShaderNearTargetDrawLimit = true;
						shouldLogNearTargetDrawLimit = true;
					}
					nearTargetDrawUniqueCount = loggedLLFBoundPixelShaderNearTargetDraws.size();
				}

				if (shouldLogNearTargetDrawLimit) {
					logger::info(
						"[LightLimitFix] Bound PS near-target draw limit reached unique={} limit={}",
						nearTargetDrawUniqueCount,
						kMaxLLFBoundPixelShaderNearTargetDrawLogs);
				}

				if (shouldLogNearTargetDraw) {
					const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
					const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
					logger::info(
						"[LightLimitFix] Bound PS near-target draw asmHash=0x{:08X} hash=0x{:08X} uid={} reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} boundCBs={} boundSRVs={}",
						metadata->asmHash,
						metadata->hash,
						metadata->uid,
						surveyReason,
						FormatLightLimitFixPixelShape(*metadata),
						a_drawKind,
						a_drawCounts,
						static_cast<std::uint32_t>(topology),
						viewportDescription,
						rt0Description,
						rt1Description,
						FormatBufferSlots(*metadata),
						FormatTextureSlots(*metadata),
						FormatTextureDimensions(*metadata),
						metadata->instructionCount,
						metadata->sampleInstructionCount,
						FormatTextureSampleCounts(*metadata),
						boundConstantBuffers,
						boundShaderResources);
				}
			}

			if (surveyDraw) {
				bool shouldLogSurveyDraw = false;
				bool shouldLogSurveyDrawLimit = false;
				std::size_t surveyDrawUniqueCount = 0;
				{
					std::scoped_lock lock(llfCandidateLock);
					if (loggedLLFBoundPixelShaderSurveyDraws.size() < kMaxLLFBoundPixelShaderSurveyDrawLogs) {
						shouldLogSurveyDraw = loggedLLFBoundPixelShaderSurveyDraws.insert(key).second;
					} else if (!loggedLLFBoundPixelShaderSurveyDrawLimit) {
						loggedLLFBoundPixelShaderSurveyDrawLimit = true;
						shouldLogSurveyDrawLimit = true;
					}
					surveyDrawUniqueCount = loggedLLFBoundPixelShaderSurveyDraws.size();
				}

				if (shouldLogSurveyDrawLimit) {
					logger::info(
						"[LightLimitFix] Bound PS survey draw limit reached unique={} limit={}",
						surveyDrawUniqueCount,
						kMaxLLFBoundPixelShaderSurveyDrawLogs);
				}

				if (!shouldLogSurveyDraw) {
					return;
				}

				logger::info(
					"[LightLimitFix] Bound PS survey draw asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					surveyReason,
					FormatLightLimitFixPixelShape(*metadata),
					a_drawKind,
					a_drawCounts,
					static_cast<std::uint32_t>(topology),
					viewportDescription,
					rt0Description,
					rt1Description,
					FormatBufferSlots(*metadata),
					FormatTextureSlots(*metadata),
					FormatTextureDimensions(*metadata),
					metadata->instructionCount,
					metadata->sampleInstructionCount,
					FormatTextureSampleCounts(*metadata),
					metadata->hasDiscard,
					metadata->hasImmediateConstantBuffer,
					metadata->immediateConstantBufferRows);
				return;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFDrawContexts.insert(key).second) {
					return;
				}
			}

			const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
			const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
			logger::info(
				"[LightLimitFix] Candidate PS draw asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={} boundCBs={} boundSRVs={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				a_drawKind,
				a_drawCounts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows,
				boundConstantBuffers,
				boundShaderResources);
		}

		DrawContextHooks GetDrawContextHooksForContext(ID3D11DeviceContext* a_context)
		{
			const auto vtable = GetContextVTablePointer(a_context);
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = drawContextHooksByVTable.find(vtable); it != drawContextHooksByVTable.end()) {
				return it->second;
			}

			return fallbackDrawContextHooks;
		}

		void TraceMissingContextOriginal(ID3D11DeviceContext* a_context, const char* a_hookKind)
		{
			const auto key = std::format("{}:{:X}:{:X}", a_hookKind, ToAddress(a_context), GetContextVTablePointer(a_context));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFMissingOriginals.insert(key).second) {
					return;
				}
			}

			logger::error(
				"[LightLimitFix] PreNG context hook missing original kind={} context=0x{:X} vtable=0x{:X}",
				a_hookKind,
				ToAddress(a_context),
				GetContextVTablePointer(a_context));
		}

		void STDMETHODCALLTYPE PSSetShaderHook(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader, ID3D11ClassInstance* const* a_classInstances, UINT a_classInstancesCount)
		{
			TraceLightLimitFixContextHookHealth(a_context, "PSSetShader");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.psSetShader) {
				hooks.psSetShader(a_context, a_pixelShader, a_classInstances, a_classInstancesCount);
				TrackPreNGDFLightDrawStateBoundPixelShader(a_context, a_pixelShader);
				TraceLightLimitFixBoundPixelShaderInventory(a_context, a_pixelShader);
				TrackLightLimitFixBoundPixelShader(a_context, a_pixelShader);
				TraceLightLimitFixPixelShaderBinding(a_context, a_pixelShader);
				TraceLightLimitFixBoundPixelShaderSurvey(a_context, a_pixelShader);
				TraceLightLimitFixStateContext(a_context, "PSSetShader", std::format("shader=0x{:X}", ToAddress(a_pixelShader)));
			} else {
				TraceMissingContextOriginal(a_context, "PSSetShader");
			}
		}

		void STDMETHODCALLTYPE PSSetShaderResourcesHook(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews)
		{
			TraceLightLimitFixContextHookHealth(a_context, "PSSetShaderResources");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.psSetShaderResources) {
				hooks.psSetShaderResources(a_context, a_startSlot, a_viewCount, a_shaderResourceViews);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"PSSetShaderResources",
						std::format(
							"startSlot={} viewCount={} views={}",
							a_startSlot,
							a_viewCount,
							FormatShaderResourceViews(a_startSlot, a_viewCount, a_shaderResourceViews)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "PSSetShaderResources");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexed");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexed",
				std::format("indexCount={} startIndex={} baseVertex={}", a_indexCount, a_startIndexLocation, a_baseVertexLocation));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawIndexed",
				std::format("indexCount={} startIndex={} baseVertex={}", a_indexCount, a_startIndexLocation, a_baseVertexLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexed) {
				hooks.drawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightLLFAdditivePass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightFullContractNoOpPass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightResourceNoOpPass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightZeroAdditivePass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexed");
			}
		}

		void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation)
		{
			TraceLightLimitFixDrawHookHealth("Draw");
			TraceLightLimitFixDrawContext(
				a_context,
				"Draw",
				std::format("vertexCount={} startVertex={}", a_vertexCount, a_startVertexLocation));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"Draw",
				std::format("vertexCount={} startVertex={}", a_vertexCount, a_startVertexLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.draw) {
				hooks.draw(a_context, a_vertexCount, a_startVertexLocation);
			} else {
				TraceMissingContextOriginal(a_context, "Draw");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexedInstanced");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexedInstanced",
				std::format(
					"indexCountPerInstance={} instanceCount={} startIndex={} baseVertex={} startInstance={}",
					a_indexCountPerInstance,
					a_instanceCount,
					a_startIndexLocation,
					a_baseVertexLocation,
					a_startInstanceLocation));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawIndexedInstanced",
				std::format(
					"indexCountPerInstance={} instanceCount={} startIndex={} baseVertex={} startInstance={}",
					a_indexCountPerInstance,
					a_instanceCount,
					a_startIndexLocation,
					a_baseVertexLocation,
					a_startInstanceLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexedInstanced) {
				hooks.drawIndexedInstanced(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, a_startInstanceLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexedInstanced");
			}
		}

		void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance, UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawInstanced");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawInstanced",
				std::format(
					"vertexCountPerInstance={} instanceCount={} startVertex={} startInstance={}",
					a_vertexCountPerInstance,
					a_instanceCount,
					a_startVertexLocation,
					a_startInstanceLocation));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawInstanced",
				std::format(
					"vertexCountPerInstance={} instanceCount={} startVertex={} startInstance={}",
					a_vertexCountPerInstance,
					a_instanceCount,
					a_startVertexLocation,
					a_startInstanceLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawInstanced) {
				hooks.drawInstanced(a_context, a_vertexCountPerInstance, a_instanceCount, a_startVertexLocation, a_startInstanceLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawInstanced");
			}
		}

		void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* a_context)
		{
			TraceLightLimitFixDrawHookHealth("DrawAuto");
			TraceLightLimitFixDrawContext(a_context, "DrawAuto", "auto=true");
			TracePreNGDFLightDrawStateContext(a_context, "DrawAuto", "auto=true");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawAuto) {
				hooks.drawAuto(a_context);
			} else {
				TraceMissingContextOriginal(a_context, "DrawAuto");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexedInstancedIndirect");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexedInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawIndexedInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexedInstancedIndirect) {
				hooks.drawIndexedInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexedInstancedIndirect");
			}
		}

		void STDMETHODCALLTYPE DrawInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
		{
			TraceLightLimitFixDrawHookHealth("DrawInstancedIndirect");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawInstancedIndirect) {
				hooks.drawInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
			} else {
				TraceMissingContextOriginal(a_context, "DrawInstancedIndirect");
			}
		}

		void STDMETHODCALLTYPE IASetPrimitiveTopologyHook(ID3D11DeviceContext* a_context, D3D11_PRIMITIVE_TOPOLOGY a_topology)
		{
			TraceLightLimitFixContextHookHealth(a_context, "IASetPrimitiveTopology");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.iaSetPrimitiveTopology) {
				hooks.iaSetPrimitiveTopology(a_context, a_topology);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"IASetPrimitiveTopology",
						std::format("topology={}", static_cast<std::uint32_t>(a_topology)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "IASetPrimitiveTopology");
			}
		}

		void STDMETHODCALLTYPE OMSetRenderTargetsHook(ID3D11DeviceContext* a_context, UINT a_renderTargetViewCount, ID3D11RenderTargetView* const* a_renderTargetViews, ID3D11DepthStencilView* a_depthStencilView)
		{
			TraceLightLimitFixContextHookHealth(a_context, "OMSetRenderTargets");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.omSetRenderTargets) {
				hooks.omSetRenderTargets(a_context, a_renderTargetViewCount, a_renderTargetViews, a_depthStencilView);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"OMSetRenderTargets",
						std::format(
							"rtvCount={} dsv=0x{:X}",
							a_renderTargetViewCount,
							ToAddress(a_depthStencilView)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "OMSetRenderTargets");
			}
		}

		void STDMETHODCALLTYPE RSSetViewportsHook(ID3D11DeviceContext* a_context, UINT a_viewportCount, const D3D11_VIEWPORT* a_viewports)
		{
			TraceLightLimitFixContextHookHealth(a_context, "RSSetViewports");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.rsSetViewports) {
				hooks.rsSetViewports(a_context, a_viewportCount, a_viewports);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"RSSetViewports",
						std::format(
							"viewportCount={} first={}",
							a_viewportCount,
							FormatViewport(a_viewports ? a_viewports[0] : D3D11_VIEWPORT{}, a_viewports && a_viewportCount > 0 ? 1 : 0)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "RSSetViewports");
			}
		}

		void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* a_context, ID3D11Resource* a_destinationResource, ID3D11Resource* a_sourceResource)
		{
			TraceLightLimitFixContextHookHealth(a_context, "CopyResource");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.copyResource) {
				hooks.copyResource(a_context, a_destinationResource, a_sourceResource);
			} else {
				TraceMissingContextOriginal(a_context, "CopyResource");
			}
		}

		void STDMETHODCALLTYPE ExecuteCommandListHook(ID3D11DeviceContext* a_context, ID3D11CommandList* a_commandList, BOOL a_restoreContextState)
		{
			TraceLightLimitFixContextHookHealth(a_context, "ExecuteCommandList");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.executeCommandList) {
				hooks.executeCommandList(a_context, a_commandList, a_restoreContextState);
			} else {
				TraceMissingContextOriginal(a_context, "ExecuteCommandList");
			}
		}

		template <class Fn, class HookFn>
		Fn InstallLightLimitFixDirectDrawDiagnostic(Fn a_original, HookFn a_hook, const char* a_drawKind, std::size_t& a_detourCount)
		{
			if (!a_original || !ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
				return a_original;
			}

			const auto originalAddress = ToFunctionAddress(a_original);
			if (!originalAddress) {
				return a_original;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = llfDirectDrawTrampolines.find(originalAddress); it != llfDirectDrawTrampolines.end()) {
					return std::bit_cast<Fn>(it->second);
				}
			}

			const auto hookAddress = ToFunctionAddress(a_hook);
			const auto trampoline = Detours::X64::DetourFunction(originalAddress, hookAddress);
			if (!trampoline) {
				logger::warn(
					"[LightLimitFix] PreNG direct draw diagnostic skipped draw={} original=0x{:X}; detour failed",
					a_drawKind,
					originalAddress);
				return a_original;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto [it, inserted] = llfDirectDrawTrampolines.emplace(originalAddress, trampoline); !inserted) {
					return std::bit_cast<Fn>(it->second);
				}
			}

			++a_detourCount;
			logger::info(
				"[LightLimitFix] PreNG direct draw diagnostic detoured draw={} original=0x{:X} trampoline=0x{:X}",
				a_drawKind,
				originalAddress,
				trampoline);
			return std::bit_cast<Fn>(trampoline);
		}

		void InstallLightLimitFixDrawContextDiagnostics(ID3D11DeviceContext* a_context, const char* a_source, const void* a_rendererData, const void* a_rendererDevice)
		{
			if (!ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
				return;
			}

			if (!a_context) {
				return;
			}

			const auto vtable = GetContextVTablePointer(a_context);
			if (!vtable) {
				logger::warn(
					"[LightLimitFix] PreNG draw-time diagnostics skipped source={} context=0x{:X}; missing vtable",
					a_source,
					ToAddress(a_context));
				return;
			}

			bool knownVTable = false;
			{
				std::scoped_lock lock(llfCandidateLock);
				knownVTable = drawContextHooksByVTable.find(vtable) != drawContextHooksByVTable.end();
			}

			if (knownVTable) {
				TraceLightLimitFixContextDiagnostics(a_source, "known-vtable", a_context, a_rendererData, a_rendererDevice);
				return;
			}

			TraceLightLimitFixContextDiagnostics(a_source, "prehook", a_context, a_rendererData, a_rendererDevice);

			DrawContextHooks hooks;
			hooks.psSetShaderResources = std::bit_cast<PSSetShaderResourcesFn>(Detours::X64::DetourClassVTable(vtable, &PSSetShaderResourcesHook, 8));
			hooks.psSetShader = std::bit_cast<PSSetShaderFn>(Detours::X64::DetourClassVTable(vtable, &PSSetShaderHook, 9));
			hooks.drawIndexed = std::bit_cast<DrawIndexedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedHook, 12));
			hooks.draw = std::bit_cast<DrawFn>(Detours::X64::DetourClassVTable(vtable, &DrawHook, 13));
			hooks.drawIndexedInstanced = std::bit_cast<DrawIndexedInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedHook, 20));
			hooks.drawInstanced = std::bit_cast<DrawInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedHook, 21));
			hooks.iaSetPrimitiveTopology = std::bit_cast<IASetPrimitiveTopologyFn>(Detours::X64::DetourClassVTable(vtable, &IASetPrimitiveTopologyHook, 24));
			hooks.omSetRenderTargets = std::bit_cast<OMSetRenderTargetsFn>(Detours::X64::DetourClassVTable(vtable, &OMSetRenderTargetsHook, 33));
			hooks.drawAuto = std::bit_cast<DrawAutoFn>(Detours::X64::DetourClassVTable(vtable, &DrawAutoHook, 38));
			hooks.drawIndexedInstancedIndirect = std::bit_cast<DrawIndexedInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedIndirectHook, 39));
			hooks.drawInstancedIndirect = std::bit_cast<DrawInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedIndirectHook, 40));
			hooks.rsSetViewports = std::bit_cast<RSSetViewportsFn>(Detours::X64::DetourClassVTable(vtable, &RSSetViewportsHook, 44));
			hooks.copyResource = std::bit_cast<CopyResourceFn>(Detours::X64::DetourClassVTable(vtable, &CopyResourceHook, 47));
			hooks.executeCommandList = std::bit_cast<ExecuteCommandListFn>(Detours::X64::DetourClassVTable(vtable, &ExecuteCommandListHook, 58));

			std::size_t directDrawDetourCount = 0;
			hooks.drawIndexed = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexed, &DrawIndexedHook, "DrawIndexed", directDrawDetourCount);
			hooks.draw = InstallLightLimitFixDirectDrawDiagnostic(hooks.draw, &DrawHook, "Draw", directDrawDetourCount);
			hooks.drawIndexedInstanced = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexedInstanced, &DrawIndexedInstancedHook, "DrawIndexedInstanced", directDrawDetourCount);
			hooks.drawInstanced = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawInstanced, &DrawInstancedHook, "DrawInstanced", directDrawDetourCount);
			hooks.drawAuto = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawAuto, &DrawAutoHook, "DrawAuto", directDrawDetourCount);
			hooks.drawIndexedInstancedIndirect = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexedInstancedIndirect, &DrawIndexedInstancedIndirectHook, "DrawIndexedInstancedIndirect", directDrawDetourCount);
			hooks.drawInstancedIndirect = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawInstancedIndirect, &DrawInstancedIndirectHook, "DrawInstancedIndirect", directDrawDetourCount);

			std::size_t hookVTableCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!installedContextHooks) {
					fallbackDrawContextHooks = hooks;
				}
				drawContextHooksByVTable[vtable] = hooks;
				installedContextHooks = true;
				hookVTableCount = drawContextHooksByVTable.size();
			}

			TraceLightLimitFixContextDiagnostics(a_source, "posthook", a_context, a_rendererData, a_rendererDevice);
			logger::info(
				"[LightLimitFix] PreNG draw-time candidate diagnostics installed source={} context=0x{:X} vtable=0x{:X} hookVTables={} directDrawDetours={} callThroughs=PSSetShaderResources=0x{:X},PSSetShader=0x{:X},DrawIndexed=0x{:X},Draw=0x{:X},DrawIndexedInstanced=0x{:X},DrawInstanced=0x{:X},IASetPrimitiveTopology=0x{:X},OMSetRenderTargets=0x{:X},DrawAuto=0x{:X},DrawIndexedInstancedIndirect=0x{:X},DrawInstancedIndirect=0x{:X},RSSetViewports=0x{:X},CopyResource=0x{:X},ExecuteCommandList=0x{:X}",
				a_source,
				ToAddress(a_context),
				vtable,
				hookVTableCount,
				directDrawDetourCount,
				ToFunctionAddress(hooks.psSetShaderResources),
				ToFunctionAddress(hooks.psSetShader),
				ToFunctionAddress(hooks.drawIndexed),
				ToFunctionAddress(hooks.draw),
				ToFunctionAddress(hooks.drawIndexedInstanced),
				ToFunctionAddress(hooks.drawInstanced),
				ToFunctionAddress(hooks.iaSetPrimitiveTopology),
				ToFunctionAddress(hooks.omSetRenderTargets),
				ToFunctionAddress(hooks.drawAuto),
				ToFunctionAddress(hooks.drawIndexedInstancedIndirect),
				ToFunctionAddress(hooks.drawInstancedIndirect),
				ToFunctionAddress(hooks.rsSetViewports),
				ToFunctionAddress(hooks.copyResource),
				ToFunctionAddress(hooks.executeCommandList));
		}

		void ProbeLightLimitFixRendererContext()
		{
			auto* rendererData = fo4cs::GetRendererData();
			if (!rendererData) {
				if (!rendererContextUnavailableLogged) {
					logger::warn("[LightLimitFix] PreNG rendererData unavailable during draw diagnostics probe");
					rendererContextUnavailableLogged = true;
				}
				return;
			}

			auto* rendererContext = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
			auto* rendererDevice = reinterpret_cast<ID3D11Device*>(rendererData->device);
			if (!rendererDevice) {
				rendererDevice = observedD3D11Device;
			}

			if (!rendererContext) {
				if (!rendererContextUnavailableLogged) {
					logger::warn(
						"[LightLimitFix] PreNG rendererData context unavailable during draw diagnostics probe rendererData=0x{:X} rendererDevice=0x{:X}",
						ToAddress(rendererData),
						ToAddress(rendererDevice));
					rendererContextUnavailableLogged = true;
				}
				return;
			}

			const bool changed = rendererContext != observedRendererContext;
			const auto vtable = GetContextVTablePointer(rendererContext);
			bool knownVTable = false;
			{
				std::scoped_lock lock(llfCandidateLock);
				knownVTable = vtable && drawContextHooksByVTable.find(vtable) != drawContextHooksByVTable.end();
			}

			if (!changed && knownVTable) {
				return;
			}

			observedRendererContext = rendererContext;
			TraceLightLimitFixContextDiagnostics("rendererData", "observed", rendererContext, rendererData, rendererDevice);
			InstallLightLimitFixDrawContextDiagnostics(rendererContext, "rendererData", rendererData, rendererDevice);
		}
#endif

		HRESULT STDMETHODCALLTYPE CreateVertexShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11VertexShader** a_vertexShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Vertex, a_bytecode, a_bytecodeLength);
			return createVertexShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_vertexShader);
		}

		HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11PixelShader** a_pixelShader)
		{
			auto* shaderCache = ShaderCache::GetSingleton();
			shaderCache->ObserveShader(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
#if defined(FALLOUT_PRE_NG)
			const bool tracePixelCandidates = ShouldTraceLLFPixelCandidates(*shaderCache);
			const bool mapShaderObjects = ShouldMapPreNGShaderObjectMetadata();
			const bool captureDFLightVanillaDumpBytecode = ShouldCapturePreNGDFLightVanillaDumpBytecode();
			const bool trackDFLightDrawTargets = ShouldTrackPreNGDFLightDrawTargets();
			std::optional<ShaderCache::ShaderMetadata> diagnosticMetadata;
			if (tracePixelCandidates || mapShaderObjects || captureDFLightVanillaDumpBytecode || trackDFLightDrawTargets) {
				diagnosticMetadata = shaderCache->GetMetadataForBytecode(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
			}
#endif
			const auto result = createPixelShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_pixelShader);
#if defined(FALLOUT_PRE_NG)
			if (SUCCEEDED(result) && a_pixelShader && *a_pixelShader && diagnosticMetadata) {
				if (captureDFLightVanillaDumpBytecode) {
					shaderCache->ObserveD3DShaderObjectBytecode(ShaderStage::Pixel, ToAddress(*a_pixelShader), *diagnosticMetadata, a_bytecode, a_bytecodeLength);
				} else if (mapShaderObjects) {
					shaderCache->ObserveD3DShaderObject(ShaderStage::Pixel, ToAddress(*a_pixelShader), *diagnosticMetadata);
				}
				const bool dflightDrawStateTarget =
					trackDFLightDrawTargets && IsPreNGDFLightDrawStateTarget(*diagnosticMetadata);
				if (dflightDrawStateTarget && !captureDFLightVanillaDumpBytecode && !mapShaderObjects) {
					shaderCache->ObserveD3DShaderObject(ShaderStage::Pixel, ToAddress(*a_pixelShader), *diagnosticMetadata);
				}
				if (tracePixelCandidates) {
					TrackObservedPixelShader(*a_pixelShader, *diagnosticMetadata);
					if (IsLightLimitFixPixelTrackedCandidate(*diagnosticMetadata)) {
						TrackLightLimitFixPixelShader(*a_pixelShader, *diagnosticMetadata);
						TraceLightLimitFixPixelCandidate(a_device, *a_pixelShader, *diagnosticMetadata);
					}
				}
				if (trackDFLightDrawTargets) {
					TrackPreNGDFLightDrawStatePixelShader(a_device, *a_pixelShader, *diagnosticMetadata);
				}
			}
#endif
			return result;
		}

		HRESULT STDMETHODCALLTYPE CreateComputeShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11ComputeShader** a_computeShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Compute, a_bytecode, a_bytecodeLength);
			return createComputeShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_computeShader);
		}

#if defined(FALLOUT_PRE_NG)
		HRESULT STDMETHODCALLTYPE CreateDeferredContextHook(ID3D11Device* a_device, UINT a_contextFlags, ID3D11DeviceContext** a_deferredContext)
		{
			const auto result = createDeferredContext(a_device, a_contextFlags, a_deferredContext);
			if (SUCCEEDED(result) && a_deferredContext && *a_deferredContext && ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
				logger::info(
					"[LightLimitFix] PreNG deferred context created flags={} context=0x{:X}",
					a_contextFlags,
					ToAddress(*a_deferredContext));
				TraceLightLimitFixContextDiagnostics("deferred", "observed", *a_deferredContext, nullptr, a_device);
				InstallLightLimitFixDrawContextDiagnostics(*a_deferredContext, "deferred", nullptr, a_device);
			}
			return result;
		}
#endif
	}

	void Install()
	{
		logger::info("[CommunityShaders] D3D11 observation hooks armed");
	}

	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		if (!a_device || installedDeviceHooks) {
			return;
		}

		*(uintptr_t*)&createVertexShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateVertexShaderHook, 12);
		*(uintptr_t*)&createPixelShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreatePixelShaderHook, 15);
		*(uintptr_t*)&createComputeShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateComputeShaderHook, 18);
#if defined(FALLOUT_PRE_NG)
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			*(uintptr_t*)&createDeferredContext = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateDeferredContextHook, 27);
		}
#endif
		installedDeviceHooks = true;

		logger::info("[CommunityShaders] D3D11 shader observation hooks installed");

#if defined(FALLOUT_PRE_NG)
		if (ShouldCapturePreNGDFLightVanillaDumpBytecode()) {
			logger::info(
				"[LightLimitFix] PreNG targeted vanilla shader bytecode capture active; set {}/{}=0 to hold it after the targeted dump run",
				kPreNGDFLightVanillaDumpEnv,
				kPreNGDFCompositeVanillaDumpEnv);
		}
		if (ShouldTracePreNGDFLightDrawState()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state capture active; set {}=0 after the short targeted run",
				kPreNGDFLightDrawStateEnv);
		}
		if (ShouldRunPreNGDFLightZeroAdditivePass()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight zero-additive pass active; set {}=0 after the short targeted run",
				kPreNGDFLightZeroAdditivePassEnv);
		}
		if (ShouldBindPreNGDFLightDrawStateStrictCB()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state strict-CB b3 bind active; set {}=0 after the short targeted run",
				kPreNGDFLightDrawStateStrictCBBindEnv);
		}
		if (ShouldBindPreNGDFLightDrawStateClusterSRVs()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state cluster SRV t35-t37 bind active; set {}=0 after the short targeted run",
				kPreNGDFLightDrawStateClusterSRVBindEnv);
		}
		if (ShouldRunPreNGDFLightDrawStateProof()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight draw-state proof budget resolved env={} samples={}",
				kPreNGDFLightDrawStateProofBudgetEnv,
				GetPreNGDFLightDrawStateProofBudget());
		}
		if (ShouldRunPreNGDFLightResourceNoOpPass()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight resource no-op pass active; set {}=0 after the short targeted run",
				kPreNGDFLightResourceNoOpPassEnv);
		}
		if (ShouldRunPreNGDFLightFullContractNoOpPass()) {
			logger::info(
				"[LightLimitFix] PreNG DFLight full contract no-op pass active; set {}=0 after the short targeted run",
				kPreNGDFLightFullContractNoOpPassEnv);
		}
		if (ShouldRunPreNGDFLightLLFAdditivePass()) {
			const auto drawLimit = GetPreNGDFLightLLFAdditivePassDrawBudget();
			const auto frameBudget = GetPreNGDFLightLLFAdditivePassFrameBudget();
			const auto scale1024 = GetPreNGDFLightLLFAdditiveScale1024();
			const auto maxLights = GetPreNGDFLightLLFAdditiveMaxLights();
			const bool persistent = ShouldPersistPreNGDFLightLLFAdditivePass();
			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive legacy proof active drawBudget={} frameBudget={} scale1024={} maxLights={} persistent={} legacyOverrideEnv={} budgetEnv={} frameBudgetEnv={} scaleEnv={} maxLightsEnv={} persistentEnv={}; set {}=0 after the targeted run",
				drawLimit,
				frameBudget,
				scale1024,
				maxLights,
				persistent ? "on" : "off",
				kPreNGDFLightLegacyAdditiveProofEnv,
				kPreNGDFLightLLFAdditiveBudgetEnv,
				kPreNGDFLightLLFAdditiveFrameBudgetEnv,
				kPreNGDFLightLLFAdditiveScale1024Env,
				kPreNGDFLightLLFAdditiveMaxLightsEnv,
				kPreNGDFLightLLFAdditivePersistentEnv,
				kPreNGDFLightLLFAdditivePassEnv);
		}
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			observedD3D11Device = a_device;
			winrt::com_ptr<ID3D11DeviceContext> context;
			a_device->GetImmediateContext(context.put());
			if (context) {
				observedImmediateContext = context.get();
				TraceLightLimitFixContextDiagnostics("immediate", "observed", context.get(), nullptr, a_device);
				InstallLightLimitFixDrawContextDiagnostics(context.get(), "immediate", nullptr, a_device);
			} else {
				logger::warn("[LightLimitFix] PreNG immediate context unavailable during draw diagnostics install");
			}
		} else {
			logger::info(
				"[LightLimitFix] PreNG support-only PS candidate diagnostics held; set {}=1 for broad shader-path evidence, {}=1 for narrow DFLight draw-state evidence, {}=1 for zero-output DFLight additive pass proof, {}=1 for DFLight draw-state strict-CB b3 proof, {}=1 for DFLight draw-state cluster SRV t35-t37 proof, {}=1 for DFLight LLF-resource no-op pass proof, {}=1 for DFLight vanilla+LLF full-contract no-op pass proof, or {}=1 plus {}=1 for the legacy DFLight LLF-only additive proof. The legacy additive proof is not the Skyrim-CS LLF implementation direction.",
				kTraceLLFPSEnv,
				kPreNGDFLightDrawStateEnv,
				kPreNGDFLightZeroAdditivePassEnv,
				kPreNGDFLightDrawStateStrictCBBindEnv,
				kPreNGDFLightDrawStateClusterSRVBindEnv,
				kPreNGDFLightResourceNoOpPassEnv,
				kPreNGDFLightFullContractNoOpPassEnv,
				kPreNGDFLightLegacyAdditiveProofEnv,
				kPreNGDFLightLLFAdditivePassEnv);
		}
#endif
	}

	void OnFrame()
	{
#if defined(FALLOUT_PRE_NG)
		AdvancePreNGDFLightLLFAdditivePassFrame();
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			if (!llfOnFrameLogged) {
				llfOnFrameLogged = true;
				logger::info("[LightLimitFix] PreNG Hooks::OnFrame reached; probing renderer context");
			}
			ProbeLightLimitFixRendererContext();
		}
#endif
	}
}
