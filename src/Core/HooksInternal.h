#pragma once

#include "Core/DebugSwitches.h"
#include "Core/ShaderCache.h"

#include <cstdint>
#include <optional>

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
