#pragma once

#include "Core/DebugSwitches.h"
#include "Core/ShaderCache.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

#include <d3d11.h>

namespace CommunityShaders::Hooks
{
	// ---- unguarded: used by Create*ShaderHook / OnD3D11DeviceCreated (all builds) ----
	using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
	inline CreatePixelShaderFn createPixelShader = nullptr;

#if defined(FALLOUT_PRE_NG)
	using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);

	// Env switch names shared between AdditivePasses.cpp and Hooks.cpp's
	// OnD3D11DeviceCreated startup logging.
	inline constexpr const char* kPreNGDFLightZeroAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_ZERO_ADD_PASS";
	inline constexpr const char* kPreNGDFLightResourceNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_RESOURCE_NOOP_PASS";
	inline constexpr const char* kPreNGDFLightFullContractNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_NOOP_PASS";
	inline constexpr const char* kPreNGDFLightLLFAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PASS";
	inline constexpr const char* kPreNGDFLightLegacyAdditiveProofEnv = "FO4CS_LLF_PRENG_DFLIGHT_LEGACY_ADDITIVE_PROOF";
	inline constexpr const char* kPreNGDFLightLLFAdditiveBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_BUDGET";
	inline constexpr const char* kPreNGDFLightLLFAdditivePersistentEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PERSISTENT";
	inline constexpr const char* kPreNGDFLightLLFAdditiveFrameBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_FRAME_BUDGET";
	inline constexpr const char* kPreNGDFLightLLFAdditiveScale1024Env = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_SCALE_1024";
	inline constexpr const char* kPreNGDFLightLLFAdditiveMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_MAX_LIGHTS";

	// Shared helpers.
	inline std::uintptr_t ToAddress(const void* a_pointer)
	{
		return reinterpret_cast<std::uintptr_t>(a_pointer);
	}

	inline std::uintptr_t GetContextVTablePointer(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return 0;
		}
		return *reinterpret_cast<std::uintptr_t*>(a_context);
	}

	inline bool ReadPreNGEnvironmentSwitch(const char* a_name)
	{
		return DebugSwitches::ReadSwitchEnabled(a_name);
	}

	inline std::optional<std::uint32_t> ReadPreNGEnvironmentUInt(const char* a_name)
	{
		const auto state = DebugSwitches::ReadUInt(a_name);
		return state.present && state.valid ? std::optional<std::uint32_t>{ state.value } : std::nullopt;
	}

	// DSP: defined in Hooks.cpp (moved out of the anonymous namespace).
	[[nodiscard]] std::optional<ShaderCache::ShaderMetadata> GetBoundPreNGDFLightDrawStatePixelShader(ID3D11DeviceContext* a_context);

	// D3D11DeviceHooks domain (Promotion Step 1): shared state + env switches + functions
	// defined in Hooks.cpp (moved out of the anonymous namespace).
	inline ID3D11Device* observedD3D11Device = nullptr;
	inline ID3D11DeviceContext* observedImmediateContext = nullptr;

	inline constexpr const char* kPreNGDFLightDrawStateEnv = "FO4CS_LLF_PRENG_DFLIGHT_DRAW_STATE";
	inline constexpr const char* kPreNGDFLightDrawStateStrictCBBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_STRICT_CB";
	inline constexpr const char* kPreNGDFLightDrawStateClusterSRVBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_CLUSTER_SRVS";
	inline constexpr const char* kPreNGDFLightDrawStateProofBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_DRAW_STATE_PROOF_BUDGET";
	inline constexpr const char* kTraceLLFPSEnv = "FO4CS_TRACE_LLF_PS";

	[[nodiscard]] bool ShouldEnableLightLimitFixPixelCandidateDiagnostics();
	bool ShouldTraceLLFPixelCandidates(const ShaderCache& a_cache);
	bool ShouldTrackPreNGDFLightDrawTargets();
	bool IsLightLimitFixPixelTrackedCandidate(const ShaderCache::ShaderMetadata& a_metadata);
	bool IsPreNGDFLightDrawStateTarget(const ShaderCache::ShaderMetadata& a_metadata);
	void TrackLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata);
	void TrackObservedPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata);
	void TraceLightLimitFixPixelCandidate(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata);
	void TrackPreNGDFLightDrawStatePixelShader(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata);
	void TraceLightLimitFixContextDiagnostics(const char* a_source, const char* a_phase, ID3D11DeviceContext* a_context, const void* a_rendererData, const void* a_rendererDevice);
	void InstallLightLimitFixDrawContextDiagnostics(ID3D11DeviceContext* a_context, const char* a_source, const void* a_rendererData, const void* a_rendererDevice);
	bool ShouldTracePreNGDFLightDrawState();
	bool ShouldBindPreNGDFLightDrawStateStrictCB();
	bool ShouldBindPreNGDFLightDrawStateClusterSRVs();
	bool ShouldRunPreNGDFLightDrawStateProof();
	std::uint32_t GetPreNGDFLightDrawStateProofBudget();

	// LLFPixelTracker domain (Promotion Step 1): shared lock + helpers + definitions
	// defined in Hooks.cpp (moved out of the anonymous namespace).
	inline std::mutex llfCandidateLock;

	inline bool HasTextureSlot(const ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_slot)
	{
		for (const auto slot : a_metadata.textureSlots) {
			if (slot == a_slot) {
				return true;
			}
		}

		return false;
	}

	inline bool HasTextureDimension(const ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_dimension, std::uint32_t a_slot)
	{
		for (const auto [dimension, slot] : a_metadata.textureDimensions) {
			if (dimension == a_dimension && slot == a_slot) {
				return true;
			}
		}

		return false;
	}

	bool HasCachedBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context);
	void TrackLightLimitFixBoundPixelShader(ID3D11DeviceContext*, ID3D11PixelShader*);
	void TraceLightLimitFixDrawHookHealth(const char* a_drawKind);
	void TraceLightLimitFixContextHookHealth(ID3D11DeviceContext*, const char* a_hookKind);
	void TraceLightLimitFixPixelShaderBinding(ID3D11DeviceContext*, ID3D11PixelShader*);
	void TraceLightLimitFixBoundPixelShaderInventory(ID3D11DeviceContext*, ID3D11PixelShader*);
	void TraceLightLimitFixBoundPixelShaderSurvey(ID3D11DeviceContext*, ID3D11PixelShader*);
	void TraceLightLimitFixStateContext(ID3D11DeviceContext*, const char* a_stateKind, std::string_view a_stateDetails);
	void TraceLightLimitFixDrawContext(ID3D11DeviceContext*, const char* a_drawKind, std::string_view a_drawCounts);

	// AdditivePasses domain: defined in AdditivePasses.cpp.
	bool ShouldRunPreNGDFLightZeroAdditivePass();
	bool ShouldRunPreNGDFLightResourceNoOpPass();
	bool ShouldRunPreNGDFLightFullContractNoOpPass();
	bool ShouldRunPreNGDFLightLLFAdditivePass();
	bool ShouldPersistPreNGDFLightLLFAdditivePass();
	std::uint32_t GetPreNGDFLightLLFAdditivePassDrawBudget();
	std::uint32_t GetPreNGDFLightLLFAdditivePassFrameBudget();
	std::uint32_t GetPreNGDFLightLLFAdditiveScale1024();
	std::uint32_t GetPreNGDFLightLLFAdditiveMaxLights();
	void AdvancePreNGDFLightLLFAdditivePassFrame();
	void RunPreNGDFLightZeroAdditivePass(ID3D11DeviceContext* a_context, DrawIndexedFn a_originalDrawIndexed, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation);
	void RunPreNGDFLightResourceNoOpPass(ID3D11DeviceContext* a_context, DrawIndexedFn a_originalDrawIndexed, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation);
	void RunPreNGDFLightFullContractNoOpPass(ID3D11DeviceContext* a_context, DrawIndexedFn a_originalDrawIndexed, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation);
	void RunPreNGDFLightLLFAdditivePass(ID3D11DeviceContext* a_context, DrawIndexedFn a_originalDrawIndexed, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation);
#endif
}
