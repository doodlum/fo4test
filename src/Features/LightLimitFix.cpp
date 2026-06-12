#include "Features/LightLimitFix.h"
#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <Windows.h>
#include <cstdint>
#include <string_view>
#include <RE/FO4Runtime.h>
#if defined(FALLOUT_PRE_NG)
#include "RE/Bethesda/IMenu.h"
#include "RE/Bethesda/UI.h"
#endif

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderCache.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSFadeNode.h"
#else
#include "RE/Bethesda/BSFadeNode.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#else
#include "RE/Bethesda/TESObjectREFRs.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESBoundAnimObjects.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/N/NiAVObject.h"
#include "RE/N/NiBound.h"
#include "RE/N/NiColor.h"
#else
#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiBound.h"
#include "RE/NetImmerse/NiColor.h"
#endif

#include "SimpleIni.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <optional>
#include <sstream>

namespace
{
	constexpr std::uint32_t kClusterMaxLights = 128;
	constexpr std::uint32_t kMaxLights = 1024;
	// Above this collected-light count the clustered prepass (per-frame 1024-cluster
	// cull over ~1000 lights) and its resource proof become a GPU/CPU sink that
	// collapses framerate. Such dense buckets only appear in fullscreen preview
	// scenes (e.g. ExamineMenu's legendary-weapon rig latches onto the world
	// ShadowSceneNode), where visible LLF is not needed. Skip the clustered
	// prepass for these frames. See .codex/docs/current-state.md (ExamineMenu FPS).
	constexpr std::uint32_t kPreNGClusterPrepassMaxLights = 512;
#if defined(FALLOUT_PRE_NG)
	constexpr std::uint64_t kPreNGStableFrame = 5;
	constexpr bool kPreNGEnableInternalPointLightHook = false;
	constexpr const char* kPreNGSetupGeometryHookOptInEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_HOOK";
	constexpr const char* kPreNGSetupGeometryStrictCBBindEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_BIND_STRICT_CB";
	constexpr const char* kPreNGSetupGeometryPersistStrictCBEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_PERSIST_STRICT_CB";
	constexpr const char* kPreNGSetupGeometryCallBudgetEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_CALL_BUDGET";
	constexpr const char* kPreNGSetupGeometryFrameBudgetEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_FRAME_BUDGET";
	constexpr const char* kPreNGPointLightHookOptInEnv = "FO4CS_LLF_PRENG_POINT_LIGHT_HOOK";
	constexpr const char* kPreNGStrictLightCBDiagnosticEnv = "FO4CS_LLF_PRENG_STRICT_CB_DIAG";
	constexpr const char* kPreNGStrictLightCBBindEnv = "FO4CS_LLF_PRENG_BIND_STRICT_CB";
	constexpr const char* kPreNGClusterSRVBindEnv = "FO4CS_LLF_PRENG_BIND_CLUSTER_SRVS";
	constexpr const char* kPreNGPrepassResourceBindEnv = "FO4CS_LLF_PRENG_PREPASS_BIND_RESOURCES";
	constexpr const char* kPreNGPersistentClusterPrepassEnv = "FO4CS_LLF_PRENG_PERSISTENT_CLUSTER_PREPASS";
	constexpr const char* kPreNGPersistentClusterPrepassRefreshIntervalEnv = "FO4CS_LLF_PRENG_PERSISTENT_CLUSTER_PREPASS_REFRESH_INTERVAL";
	constexpr const char* kPreNGClusterPrepassReuseEnv = "FO4CS_LLF_PRENG_CLUSTER_PREPASS_REUSE";
	constexpr const char* kPreNGShadowSceneFastReuseEnv = "FO4CS_LLF_PRENG_SHADOW_SCENE_FAST_REUSE";
	constexpr const char* kPreNGShadowSceneFastReuseRefreshIntervalEnv = "FO4CS_LLF_PRENG_SHADOW_SCENE_FAST_REUSE_REFRESH_INTERVAL";
	constexpr const char* kPreNGClusterPrepassProofFramesEnv = "FO4CS_LLF_PRENG_CLUSTER_PREPASS_PROOF_FRAMES";
	constexpr const char* kPreNGBSLightingResourcePrepassProofFramesEnv = "FO4CS_LLF_PRENG_BSLIGHTING_RESOURCE_PREPASS_FRAMES";
	constexpr const char* kPreNGDFLightDrawStateStrictCBBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_STRICT_CB";
	constexpr const char* kPreNGDFLightDrawStateClusterSRVBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_CLUSTER_SRVS";
	constexpr const char* kPreNGDFLightResourceNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_RESOURCE_NOOP_PASS";
	constexpr const char* kPreNGDFLightFullContractNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_NOOP_PASS";
	constexpr const char* kPreNGDFLightLLFAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PASS";
	constexpr const char* kPreNGDFLightLegacyAdditiveProofEnv = "FO4CS_LLF_PRENG_DFLIGHT_LEGACY_ADDITIVE_PROOF";
	constexpr const char* kPreNGDFLightLLFAdditivePersistentEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PERSISTENT";
	constexpr const char* kPreNGDFLightLLFAdditiveRefreshIntervalEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_REFRESH_INTERVAL";
	constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
	constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
	constexpr const char* kPreNGDFLightFullContractVisibleLLFEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF";
	constexpr const char* kPreNGDFCompositeResourceBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_RESOURCE_BIND";
	constexpr const char* kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
	constexpr const char* kPreNGDFCompositeVisibleLLFEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF";
	constexpr const char* kPreNGBSLightingResourceBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_RESOURCE_BIND";
	constexpr const char* kPreNGBSLightingSetupGeometryResourceBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_SETUP_GEOMETRY_RESOURCE_BIND";
	constexpr const char* kPreNGBSLightingContractCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONTRACT_COMPILE";
	constexpr const char* kPreNGBSLightingConsumerCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONSUMER_COMPILE";
	constexpr const char* kPreNGBSLightingDescriptorObserveEnv = "FO4CS_LLF_PRENG_BSLIGHTING_DESCRIPTOR_OBSERVE";
	constexpr const char* kPreNGBSLightingVanillaBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_VANILLA_BIND";
	constexpr const char* kPreNGBSLightingLLFBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND";
	constexpr const char* kPreNGShaderObjectMetadataEnv = "FO4CS_LLF_PRENG_SHADER_OBJECT_METADATA";
	constexpr const char* kPreNGTraceLLFPixelEnv = "FO4CS_TRACE_LLF_PS";
	constexpr const char* kPreNGDFLightContractCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_CONTRACT_COMPILE";
	constexpr const char* kPreNGDFLightCandidateCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_CANDIDATE_COMPILE";
	constexpr const char* kPreNGDFLightContractProbeSource = "LightLimitFix\\DFLightContractProbePS.hlsl";
	constexpr const char* kPreNGDFLightFullShadowedCandidateSource = "LightLimitFix\\DFLightFullShadowedPS.hlsl";
	constexpr std::uint32_t kPreNGSetupGeometryStrictCBProofMaxSamples = 16;
	constexpr std::uint32_t kPreNGMaxSetupGeometryCallBudget = 1000000;
	constexpr std::uint32_t kPreNGDefaultSetupGeometryFrameBudget = 2;
	constexpr std::uint32_t kPreNGMaxSetupGeometryFrameBudget = 64;
	constexpr std::uint32_t kPreNGDefaultClusterPrepassProofFrames = 8;
	constexpr std::uint32_t kPreNGMinClusterPrepassProofFrames = 1;
	constexpr std::uint32_t kPreNGMaxClusterPrepassProofFrames = 600;
	constexpr std::uint32_t kPreNGDefaultBSLightingResourcePrepassProofFrames = 120;
	constexpr std::uint32_t kPreNGMinBSLightingResourcePrepassProofFrames = 8;
	constexpr std::uint32_t kPreNGMaxBSLightingResourcePrepassProofFrames = 600;
	constexpr std::uint32_t kPreNGDefaultPersistentClusterPrepassRefreshInterval = 0;
	constexpr std::uint32_t kPreNGMinPersistentClusterPrepassRefreshInterval = 0;
	constexpr std::uint32_t kPreNGMaxPersistentClusterPrepassRefreshInterval = 600;
	constexpr std::uint32_t kPreNGDefaultShadowSceneFastReuseRefreshInterval = 8;
	constexpr std::uint32_t kPreNGMinShadowSceneFastReuseRefreshInterval = 1;
	constexpr std::uint32_t kPreNGMaxShadowSceneFastReuseRefreshInterval = 600;
	constexpr std::uint32_t kPreNGDefaultDFLightLLFAdditiveRefreshInterval = 0;
	constexpr std::uint32_t kPreNGMinDFLightLLFAdditiveRefreshInterval = 0;
	constexpr std::uint32_t kPreNGMaxDFLightLLFAdditiveRefreshInterval = 600;
	constexpr float kPreNGClusterBuildReuseTolerance = 1.0e-3f;
	std::atomic_bool s_preNGDFLightLLFConsumerDescriptorObserved = false;
	std::atomic_uint32_t s_preNGDFLightLLFConsumerDescriptorObservations = 0;
	std::atomic_bool s_preNGDFCompositeLLFConsumerDescriptorObserved = false;
	std::atomic_uint32_t s_preNGDFCompositeLLFConsumerDescriptorObservations = 0;
	std::atomic_bool s_preNGBSLightingLLFConsumerDescriptorObserved = false;
	std::atomic_uint32_t s_preNGBSLightingLLFConsumerDescriptorObservations = 0;
	std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastVertexDescriptor = 0;
	std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastPixelDescriptor = 0;
	std::atomic_bool s_preNGBSLightingLLFConsumerLastFound = false;
	std::atomic<std::uintptr_t> s_preNGBSLightingLLFConsumerLastVanillaPixelShader = 0;
	std::atomic_bool s_preNGBSLightingDeferredResourceProofComplete = false;
	constexpr std::uint64_t kPreNGBSLightingResourceProofMenuSettleFrames = 120;
	constexpr std::string_view kPreNGBSLightingResourceProofLockpickingMenu{ "LockpickingMenu" };
	// Fullscreen 3D preview menus that latch the LLF decode onto the world
	// ShadowSceneNode (1000+ lights), collapsing framerate via the per-frame
	// clustered prepass. Like LockpickingMenu, these need permanent suppression
	// of the clustered prepass / deferred b3-t35-t37 bind for the process: the
	// preview rig itself only needs its handful of vanilla lights, and visible
	// LLF is not wanted while a preview menu is up. See .codex/docs/current-state.md.
	constexpr std::array kPreNGBSLightingResourceProofBlockingMenus{
		kPreNGBSLightingResourceProofLockpickingMenu,
		std::string_view{ "ExamineMenu" },
		std::string_view{ "ExamineConfirmMenu" }
	};
	std::atomic_uint64_t s_preNGBSLightingResourceProofBypassUntilFrame = 0;
	std::atomic_uint32_t s_preNGBSLightingResourceProofBypassLogs = 0;
	std::atomic_bool s_preNGBSLightingResourceProofSuppressedByLockpicking = false;
	constexpr std::uint64_t kPreNGBSLightingSetupGeometryNoLightBypassFrames = 30;
	constexpr std::array kPreNGBSLightingSetupGeometryPreviewMenus{
		std::string_view{ "PipboyMenu" },
		std::string_view{ "TerminalMenu" },
		std::string_view{ "ExamineMenu" },
		std::string_view{ "ExamineConfirmMenu" },
		std::string_view{ "ContainerMenu" },
		std::string_view{ "BarterMenu" },
		std::string_view{ "PowerArmorModMenu" }
	};
	constexpr std::uint32_t kPreNGBSLightingSetupGeometryWorkshopPreviewReason =
		static_cast<std::uint32_t>(kPreNGBSLightingSetupGeometryPreviewMenus.size() + 1);
	constexpr std::uint64_t kPreNGBSLightingSetupGeometryPreviewCacheInvalidFrame =
		static_cast<std::uint64_t>(-1);
	std::atomic_uint64_t s_preNGBSLightingSetupGeometryNoLightNextProbeFrame = 0;
	std::atomic_uint64_t s_preNGBSLightingSetupGeometryBypassUntilFrame = 0;
	std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassLogs = 0;
	std::atomic_uint64_t s_preNGBSLightingSetupGeometryPreviewCacheFrame =
		kPreNGBSLightingSetupGeometryPreviewCacheInvalidFrame;
	std::atomic_uint32_t s_preNGBSLightingSetupGeometryPreviewCacheReason = 0;
	std::atomic_bool s_preNGPointLightHookInstalled = false;
	std::atomic_bool s_preNGPointLightHookPatchVerified = false;
	std::atomic_uint32_t s_preNGPointLightHookCallCount = 0;
	std::atomic_bool s_preNGBSLightingSetupGeometryHookInstalled = false;
	std::atomic_uint32_t s_preNGBSLightingSetupGeometryHookCallCount = 0;
	std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassCallCount = 0;
#endif

	std::string GetShaderPath()
	{
		return "LightLimitFix\\";
	}

#pragma warning(push)
#pragma warning(disable: 4324)
	struct NiLightView : RE::NiAVObject
	{
		RE::NiColor amb;
		RE::NiColor diff;
		RE::NiColor spec;
		float dimmer;
		alignas(16) RE::NiBound modelBound;
		void* rendererData;
	};
#pragma warning(pop)

	static_assert(sizeof(NiLightView) == 0x170);
	static_assert(offsetof(NiLightView, diff) == 0x12C);
	static_assert(offsetof(NiLightView, modelBound) == 0x150);

	bool LogResourceFailure(const char* a_name, HRESULT a_hr)
	{
		logger::error("[LightLimitFix] {} failed (hr=0x{:08X})",
		              a_name, static_cast<std::uint32_t>(a_hr));
		return false;
	}

	bool IsFiniteMatrix(const DirectX::XMFLOAT4X4& a_matrix)
	{
		const auto* values = reinterpret_cast<const float*>(&a_matrix);
		for (std::size_t i = 0; i < 16; ++i) {
			if (!std::isfinite(values[i])) {
				return false;
			}
		}
		return true;
	}
#if defined(FALLOUT_PRE_NG)
	bool PreNGClusterBuildInputsMatch(
		const LightLimitFix::ClusterBuildCacheState& a_cached,
		const LightLimitFix::LightBuildingCB& a_current)
	{
		if (std::fabs(a_cached.LightsNear - a_current.LightsNear) > kPreNGClusterBuildReuseTolerance ||
		    std::fabs(a_cached.LightsFar - a_current.LightsFar) > kPreNGClusterBuildReuseTolerance) {
			return false;
		}

		for (std::uint32_t i = 0; i < 4; ++i) {
			if (a_cached.ClusterSize[i] != a_current.ClusterSize[i]) {
				return false;
			}
		}

		// Projection jitter can change the inverse matrix every frame. For the
		// targeted DFLight refresh path, reuse built AABBs while near/far and
		// grid size are stable; SetupResources invalidates resource changes.
		return true;
	}

	constexpr std::uint64_t kPreNGFNVOffsetBasis = 14695981039346656037ull;
	constexpr std::uint64_t kPreNGFNVPrime = 1099511628211ull;

	void HashPreNGAppendBytes(std::uint64_t& a_hash, const void* a_data, std::size_t a_size)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(a_data);
		for (std::size_t i = 0; i < a_size; ++i) {
			a_hash ^= bytes[i];
			a_hash *= kPreNGFNVPrime;
		}
	}

	std::uint64_t HashPreNGBytes(const void* a_data, std::size_t a_size)
	{
		auto hash = kPreNGFNVOffsetBasis;
		HashPreNGAppendBytes(hash, a_data, a_size);
		return hash;
	}

	LightLimitFix::ClusterPayloadCacheState MakePreNGClusterPayloadCacheState(
		const std::vector<LightLimitFix::LightData>& a_lights,
		const LightLimitFix::StrictLightDataCB& a_strictLightData,
		std::uint32_t a_lightCount,
		std::uint32_t a_strictLightCount,
		const DirectX::XMFLOAT4X4& a_viewTransposed,
		float a_lightsNear,
		float a_lightsFar,
		const std::uint32_t (&a_clusterSize)[3])
	{
		LightLimitFix::ClusterPayloadCacheState state{};
		state.LightsNear = a_lightsNear;
		state.LightsFar = a_lightsFar;
		state.ClusterSize[0] = a_clusterSize[0];
		state.ClusterSize[1] = a_clusterSize[1];
		state.ClusterSize[2] = a_clusterSize[2];
		state.ClusterSize[3] = 0;
		state.LightCount = a_lightCount;
		state.StrictLightCount = a_strictLightCount;
		state.ShadowBitMask = a_strictLightData.ShadowBitMask;
		state.LightsHash = a_lights.empty() ?
			kPreNGFNVOffsetBasis :
			HashPreNGBytes(a_lights.data(), a_lights.size() * sizeof(LightLimitFix::LightData));
		state.StrictHash = HashPreNGBytes(&a_strictLightData, sizeof(a_strictLightData));
		state.ViewHash = HashPreNGBytes(&a_viewTransposed, sizeof(a_viewTransposed));
		return state;
	}

	bool PreNGClusterPayloadInputsMatch(
		const LightLimitFix::ClusterPayloadCacheState& a_cached,
		const LightLimitFix::ClusterPayloadCacheState& a_current)
	{
		if (std::fabs(a_cached.LightsNear - a_current.LightsNear) > kPreNGClusterBuildReuseTolerance ||
		    std::fabs(a_cached.LightsFar - a_current.LightsFar) > kPreNGClusterBuildReuseTolerance) {
			return false;
		}

		for (std::uint32_t i = 0; i < 4; ++i) {
			if (a_cached.ClusterSize[i] != a_current.ClusterSize[i]) {
				return false;
			}
		}

		return a_cached.LightCount == a_current.LightCount &&
		       a_cached.StrictLightCount == a_current.StrictLightCount &&
		       a_cached.ShadowBitMask == a_current.ShadowBitMask &&
		       a_cached.LightsHash == a_current.LightsHash &&
		       a_cached.StrictHash == a_current.StrictHash &&
		       a_cached.ViewHash == a_current.ViewHash;
	}

	namespace F4Runtime = RE::FO4Runtime;

	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		return F4Runtime::IsReadableAddress(a_address, a_size);
	}

	constexpr std::uint32_t kPreNGBSRenderPassSceneLightFirstIndex = F4Runtime::PreNG::BS_RENDER_PASS_SCENE_LIGHT_FIRST_INDEX;
	constexpr std::uint32_t kPreNGInvalidShadowLightMaskIndex = F4Runtime::PreNG::INVALID_SHADOW_LIGHT_MASK_INDEX;
	constexpr std::uint32_t kPreNGMaxShadowLightMaskBits = F4Runtime::PreNG::MAX_SHADOW_LIGHT_MASK_BITS;
	constexpr std::uint32_t kPreNGMaxShadowSceneActiveLights = 8192;
	constexpr std::uint32_t kPreNGMaxShadowSceneDecodeLights = kMaxLights;
	constexpr float kPreNGLightContributionThreshold = 1.0e-4f;
	constexpr float kPreNGLightRadiusThreshold = 1.0e-4f;

	template <class T>
	bool ReadPreNGValue(std::uintptr_t a_address, T& a_value)
	{
		return F4Runtime::ReadValue(a_address, a_value);
	}

	bool SamePreNGShadowSceneFastReuseKey(
		const LightLimitFix::ShadowSceneFastReuseKey& a_lhs,
		const LightLimitFix::ShadowSceneFastReuseKey& a_rhs)
	{
		return a_lhs.Node == a_rhs.Node &&
		       a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
		       a_lhs.CurrentIndex == a_rhs.CurrentIndex &&
		       a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
		       a_lhs.UsedFallback == a_rhs.UsedFallback &&
		       a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
		       a_lhs.ShadowEntries == a_rhs.ShadowEntries &&
		       a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
		       a_lhs.ActiveCount == a_rhs.ActiveCount &&
		       a_lhs.ShadowCount == a_rhs.ShadowCount &&
		       a_lhs.ExtraCount == a_rhs.ExtraCount &&
		       a_lhs.ActiveHash == a_rhs.ActiveHash &&
		       a_lhs.ShadowHash == a_rhs.ShadowHash &&
		       a_lhs.ExtraHash == a_rhs.ExtraHash;
	}

	// Structural (pointer/hash-independent) fast-reuse comparison. The per-light
	// wrapper pointers folded into ActiveHash/ShadowHash/ExtraHash churn every
	// frame even for a static light set (the engine reorders/reallocates the
	// wrapper array), so the strict SamePreNGShadowSceneFastReuseKey never
	// matches for dense scenes and the decode re-runs every frame (sub-1-FPS in
	// 1000+ light interiors). This weaker key matches when the same node exposes
	// the same bucket layout and light counts, which together with a hard
	// ReuseAge < refreshInterval gate bounds full re-decodes to once per
	// refresh interval regardless of pointer churn.
	bool SamePreNGShadowSceneFastReuseStructure(
		const LightLimitFix::ShadowSceneFastReuseKey& a_lhs,
		const LightLimitFix::ShadowSceneFastReuseKey& a_rhs)
	{
		return a_lhs.Node == a_rhs.Node &&
		       a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
		       a_lhs.CurrentIndex == a_rhs.CurrentIndex &&
		       a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
		       a_lhs.UsedFallback == a_rhs.UsedFallback &&
		       a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
		       a_lhs.ShadowEntries == a_rhs.ShadowEntries &&
		       a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
		       a_lhs.ActiveCount == a_rhs.ActiveCount &&
		       a_lhs.ShadowCount == a_rhs.ShadowCount &&
		       a_lhs.ExtraCount == a_rhs.ExtraCount;
	}

	bool ReadPreNGShadowSceneBucketHash(
		const F4Runtime::PreNGShadowSceneBucket& a_bucket,
		std::uint64_t& a_hash)
	{
		a_hash = kPreNGFNVOffsetBasis;
		HashPreNGAppendBytes(a_hash, &a_bucket.entries, sizeof(a_bucket.entries));
		HashPreNGAppendBytes(a_hash, &a_bucket.count, sizeof(a_bucket.count));
		for (std::uint32_t i = 0; i < a_bucket.count; ++i) {
			std::uintptr_t wrapperAddress = 0;
			const auto entryAddress = a_bucket.entries + (i * sizeof(std::uintptr_t));
			if (!ReadPreNGValue(entryAddress, wrapperAddress)) {
				return false;
			}
			HashPreNGAppendBytes(a_hash, &wrapperAddress, sizeof(wrapperAddress));
		}
		return true;
	}

	bool MakePreNGShadowSceneFastReuseKey(
		const F4Runtime::PreNGShadowSceneNodeRef& a_nodeRef,
		const F4Runtime::PreNGShadowSceneBuckets& a_buckets,
		LightLimitFix::ShadowSceneFastReuseKey& a_key)
	{
		LightLimitFix::ShadowSceneFastReuseKey key{};
		key.Node = a_nodeRef.node;
		key.SelectedIndex = a_nodeRef.selectedIndex;
		key.CurrentIndex = a_nodeRef.currentIndex;
		key.CurrentIndexRead = a_nodeRef.currentIndexRead;
		key.UsedFallback = a_nodeRef.usedFallback;
		key.ActiveEntries = a_buckets.active.entries;
		key.ShadowEntries = a_buckets.shadow.entries;
		key.ExtraEntries = a_buckets.extra.entries;
		key.ActiveCount = a_buckets.active.count;
		key.ShadowCount = a_buckets.shadow.count;
		key.ExtraCount = a_buckets.extra.count;
		if (!ReadPreNGShadowSceneBucketHash(a_buckets.active, key.ActiveHash) ||
		    !ReadPreNGShadowSceneBucketHash(a_buckets.shadow, key.ShadowHash) ||
		    !ReadPreNGShadowSceneBucketHash(a_buckets.extra, key.ExtraHash)) {
			return false;
		}

		a_key = key;
		return true;
	}

	using PreNGPixelShaderEntryState = F4Runtime::PreNGShaderEntryState;

	PreNGPixelShaderEntryState ReadPreNGCurrentPixelShaderEntryState()
	{
		return F4Runtime::ReadPreNGPixelShaderEntryState();
	}

	bool HasPreNGConstantBufferSlot(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_slot)
	{
		return a_slot < a_metadata.constantBufferSizes.size() &&
		       a_metadata.constantBufferSizes[a_slot] != 0;
	}

	bool HasPreNGTextureSlot(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_slot)
	{
		return std::find(a_metadata.textureSlots.begin(), a_metadata.textureSlots.end(), a_slot) != a_metadata.textureSlots.end();
	}

	std::uint32_t GetPreNGTextureSampleCount(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_slot)
	{
		return a_slot < a_metadata.textureSampleCounts.size() ? a_metadata.textureSampleCounts[a_slot] : 0;
	}

	std::string FormatPreNGShaderBufferSlots(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata)
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

	std::string FormatPreNGShaderTextureSlots(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata)
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

	std::string FormatPreNGShaderTextureSampleCounts(const CommunityShaders::ShaderCache::ShaderMetadata& a_metadata)
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

	struct PreNGShaderSlotEvidence
	{
		bool hasMetadata = false;
		bool declaresCB3 = false;
		bool declaresT35 = false;
		bool declaresT36 = false;
		bool declaresT37 = false;
		std::uint32_t samplesT35 = 0;
		std::uint32_t samplesT36 = 0;
		std::uint32_t samplesT37 = 0;
	};

	PreNGShaderSlotEvidence GetPreNGShaderSlotEvidence(const std::optional<CommunityShaders::ShaderCache::ShaderMetadata>& a_metadata)
	{
		PreNGShaderSlotEvidence result{};
		if (!a_metadata) {
			return result;
		}

		result.hasMetadata = true;
		result.declaresCB3 = HasPreNGConstantBufferSlot(*a_metadata, 3);
		result.declaresT35 = HasPreNGTextureSlot(*a_metadata, 35);
		result.declaresT36 = HasPreNGTextureSlot(*a_metadata, 36);
		result.declaresT37 = HasPreNGTextureSlot(*a_metadata, 37);
		result.samplesT35 = GetPreNGTextureSampleCount(*a_metadata, 35);
		result.samplesT36 = GetPreNGTextureSampleCount(*a_metadata, 36);
		result.samplesT37 = GetPreNGTextureSampleCount(*a_metadata, 37);
		return result;
	}

	bool HasPreNGFullShadowedDFLightVanillaContract(const std::optional<CommunityShaders::ShaderCache::ShaderMetadata>& a_metadata)
	{
		return a_metadata &&
		       a_metadata->constantBufferSizes[2] == 448 &&
		       a_metadata->constantBufferSizes[12] == 496 &&
		       HasPreNGTextureSlot(*a_metadata, 0) &&
		       HasPreNGTextureSlot(*a_metadata, 1) &&
		       HasPreNGTextureSlot(*a_metadata, 2) &&
		       HasPreNGTextureSlot(*a_metadata, 3) &&
		       HasPreNGTextureSlot(*a_metadata, 5) &&
		       GetPreNGTextureSampleCount(*a_metadata, 0) == 1 &&
		       GetPreNGTextureSampleCount(*a_metadata, 1) == 1 &&
		       GetPreNGTextureSampleCount(*a_metadata, 2) == 1 &&
		       GetPreNGTextureSampleCount(*a_metadata, 3) == 1 &&
		       GetPreNGTextureSampleCount(*a_metadata, 5) == 6;
	}

	std::string FormatPreNGShaderMetadata(const std::optional<CommunityShaders::ShaderCache::ShaderMetadata>& a_metadata)
	{
		if (!a_metadata) {
			return "missing";
		}

		return std::format(
			"uid={} asm=0x{:08X} hash=0x{:08X} size={} buffers={} textures={} textureSamples={}",
			a_metadata->uid,
			a_metadata->asmHash,
			a_metadata->hash,
			a_metadata->size,
			FormatPreNGShaderBufferSlots(*a_metadata),
			FormatPreNGShaderTextureSlots(*a_metadata),
			FormatPreNGShaderTextureSampleCounts(*a_metadata));
	}

	using PreNGShadowSceneNodeRef = F4Runtime::PreNGShadowSceneNodeRef;

	PreNGShadowSceneNodeRef GetPreNGWorldShadowSceneNode()
	{
		return F4Runtime::GetPreNGWorldShadowSceneNode();
	}

	enum class PreNGLightDecodeResult
	{
		Decoded,
		MissingWrapperData,
		InvalidNiLightData,
		NonContributingLightData
	};

	PreNGLightDecodeResult DecodePreNGBSLightWrapper(
		std::uintptr_t a_wrapperAddress,
		LightLimitFix::LightData& a_data,
		std::uintptr_t& a_niLightAddress,
		bool& a_shadowMaskUnreadable,
		bool& a_shadowMaskInvalid,
		std::uint32_t& a_shadowMaskBit)
	{
		a_data = {};
		a_niLightAddress = 0;
		a_shadowMaskUnreadable = false;
		a_shadowMaskInvalid = false;
		a_shadowMaskBit = 0;

		float wrapperFade = 1.0f;
		const F4Runtime::PreNGLightWrapperView wrapperView{ a_wrapperAddress };
		if (a_wrapperAddress == 0 ||
			!wrapperView.ReadFade(wrapperFade) ||
			!wrapperView.ReadNiLight(a_niLightAddress) ||
			a_niLightAddress == 0 ||
			!std::isfinite(wrapperFade)) {
			return PreNGLightDecodeResult::MissingWrapperData;
		}

		F4Runtime::PreNGNiLightData niLight{};
		const F4Runtime::PreNGNiLightView niLightView{ a_niLightAddress };
		if (!niLightView.Read(niLight) ||
			!std::isfinite(niLight.diffuse[0]) ||
			!std::isfinite(niLight.diffuse[1]) ||
			!std::isfinite(niLight.diffuse[2]) ||
			!std::isfinite(niLight.radius) ||
			!std::isfinite(niLight.dimmer) ||
			!std::isfinite(niLight.position[0]) ||
			!std::isfinite(niLight.position[1]) ||
			!std::isfinite(niLight.position[2]) ||
			niLight.radius <= 0.0f) {
			return PreNGLightDecodeResult::InvalidNiLightData;
		}

		const float fade = niLight.dimmer * wrapperFade;
		const float contribution = (niLight.diffuse[0] + niLight.diffuse[1] + niLight.diffuse[2]) * fade;
		if (niLight.radius <= kPreNGLightRadiusThreshold || contribution <= kPreNGLightContributionThreshold) {
			return PreNGLightDecodeResult::NonContributingLightData;
		}

		a_data.color.x = niLight.diffuse[0];
		a_data.color.y = niLight.diffuse[1];
		a_data.color.z = niLight.diffuse[2];
		a_data.fade = fade;
		a_data.radius = niLight.radius;
		a_data.invRadius = a_data.radius > 0.0f ? 1.0f / a_data.radius : 0.0f;
		a_data.positionWS[0].data.x = niLight.position[0];
		a_data.positionWS[0].data.y = niLight.position[1];
		a_data.positionWS[0].data.z = niLight.position[2];
		a_data.lightFlags = static_cast<std::uint32_t>(LightLimitFix::LightFlags::Initialised);

		std::uint32_t shadowMaskIndex = kPreNGInvalidShadowLightMaskIndex;
		if (wrapperView.ReadShadowMaskIndex(shadowMaskIndex)) {
			if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex && shadowMaskIndex < kPreNGMaxShadowLightMaskBits) {
				a_data.lightFlags |= static_cast<std::uint32_t>(LightLimitFix::LightFlags::Shadow);
				a_data.shadowLightIndex = shadowMaskIndex;
				a_shadowMaskBit = (1u << shadowMaskIndex);
			} else if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex) {
				a_shadowMaskInvalid = true;
			}
		} else {
			a_shadowMaskUnreadable = true;
		}

		return PreNGLightDecodeResult::Decoded;
	}

	struct PreNGCallPatchState
	{
		bool readable = false;
		std::uint8_t opcode = 0;
		std::int32_t rel32 = 0;
		std::uintptr_t callTarget = 0;
	};

	PreNGCallPatchState ReadPreNGCallPatch(std::uintptr_t a_call)
	{
		PreNGCallPatchState state{};
		if (!IsReadableMemory(a_call, 5)) {
			return state;
		}

		const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_call);
		state.readable = true;
		state.opcode = bytes[0];
		std::memcpy(&state.rel32, bytes + 1, sizeof(state.rel32));
		state.callTarget = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(a_call + 5) + state.rel32);
		return state;
	}

	std::uintptr_t ResolvePreNGAbsoluteJumpTarget(std::uintptr_t a_address)
	{
		std::uint8_t bytes[14]{};
		if (!IsReadableMemory(a_address, sizeof(bytes))) {
			return 0;
		}

		std::memcpy(bytes, reinterpret_cast<const void*>(a_address), sizeof(bytes));
		if (bytes[0] != 0xFF || bytes[1] != 0x25) {
			return 0;
		}

		std::int32_t disp = 0;
		std::memcpy(&disp, bytes + 2, sizeof(disp));
		const auto slot = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(a_address + 6) + disp);

		std::uintptr_t target = 0;
		if (!ReadPreNGValue(slot, target)) {
			return 0;
		}

		return target;
	}


	bool ValidatePreNGPointLightCallsite()
	{
		const auto imageBase = F4Runtime::ModuleBase();
		const auto runtimeSetup = F4Runtime::PreNG::BS_LIGHTING_SHADER_SETUP_GEOMETRY.address();
		const auto runtimeCall = F4Runtime::PreNG::POINT_LIGHT_CALL.address();
		const auto runtimeCallContext = runtimeCall - 8;
		const auto runtimeTarget = F4Runtime::PreNG::POINT_LIGHT_TARGET.address();
		const auto runtimeVTable = F4Runtime::PreNG::BS_LIGHTING_SHADER_VTABLE.address();
		const auto runtimeVFuncEntry = F4Runtime::PreNG::BS_LIGHTING_SHADER_VFUNC_7.address();

		const bool vfuncReadable = IsReadableMemory(runtimeVFuncEntry, sizeof(std::uintptr_t));
		const bool callReadable = IsReadableMemory(runtimeCallContext, F4Runtime::PreNG::POINT_LIGHT_CALL_CONTEXT.size());
		if (!vfuncReadable || !callReadable) {
			logger::warn(
				"[LightLimitFix] PreNG point-light callsite validation skipped: unreadable memory base=0x{:X} vfuncReadable={} callReadable={} vfunc[7]=0x{:X} callContext=0x{:X}",
				imageBase, vfuncReadable, callReadable, runtimeVFuncEntry, runtimeCallContext);
			return false;
		}

		const auto observedVFunc = *reinterpret_cast<const std::uintptr_t*>(runtimeVFuncEntry);
		const auto* callBytes = reinterpret_cast<const std::uint8_t*>(runtimeCall);
		std::int32_t callRel = 0;
		std::memcpy(&callRel, callBytes + 1, sizeof(callRel));
		const auto observedTarget = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(runtimeCall + 5) + callRel);
		const bool contextMatches = std::memcmp(
			reinterpret_cast<const void*>(runtimeCallContext),
			F4Runtime::PreNG::POINT_LIGHT_CALL_CONTEXT.data(),
			F4Runtime::PreNG::POINT_LIGHT_CALL_CONTEXT.size()) == 0;

		if (observedVFunc == runtimeSetup && callBytes[0] == 0xE8 && observedTarget == runtimeTarget && contextMatches) {
			logger::info(
				"[LightLimitFix] PreNG point-light callsite validated base=0x{:X} setup=0x{:X} call=0x{:X} target=0x{:X} vtable=0x{:X} vfunc[7]=0x{:X}->0x{:X}",
				imageBase, runtimeSetup, runtimeCall, observedTarget, runtimeVTable, runtimeVFuncEntry, observedVFunc);
			return true;
		}

		logger::warn(
			"[LightLimitFix] PreNG point-light callsite mismatch base=0x{:X} vfunc[7]=0x{:X}->0x{:X} expectedSetup=0x{:X} call=0x{:X} opcode=0x{:02X} rel32=0x{:08X} observedTarget=0x{:X} expectedTarget=0x{:X} contextMatch={}",
			imageBase,
			runtimeVFuncEntry,
			observedVFunc,
			runtimeSetup,
			runtimeCall,
			static_cast<std::uint32_t>(callBytes[0]),
			static_cast<std::uint32_t>(callRel),
			observedTarget,
			runtimeTarget,
			contextMatches);
		return false;
	}

	bool IsTruthyEnvironmentValue(const char* a_value)
	{
		return std::strcmp(a_value, "1") == 0 ||
		       std::strcmp(a_value, "true") == 0 ||
		       std::strcmp(a_value, "TRUE") == 0 ||
		       std::strcmp(a_value, "on") == 0 ||
		       std::strcmp(a_value, "ON") == 0;
	}

	enum class EnvironmentSwitchSource
	{
		kNone,
		kProcess,
		kUserRegistry,
		kMachineRegistry
	};

	struct EnvironmentSwitchState
	{
		bool enabled = false;
		EnvironmentSwitchSource source = EnvironmentSwitchSource::kNone;
	};

	struct EnvironmentUIntState
	{
		std::uint32_t value = 0;
		EnvironmentSwitchSource source = EnvironmentSwitchSource::kNone;
		bool present = false;
		bool valid = false;
	};

	const char* EnvironmentSwitchSourceName(EnvironmentSwitchSource a_source)
	{
		switch (a_source) {
		case EnvironmentSwitchSource::kProcess:
			return "process";
		case EnvironmentSwitchSource::kUserRegistry:
			return "user-reg";
		case EnvironmentSwitchSource::kMachineRegistry:
			return "machine-reg";
		default:
			return "none";
		}
	}

	bool ReadRegistryEnvironmentValue(
		HKEY a_root,
		const char* a_subKey,
		const char* a_name,
		char (&a_value)[16])
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

	EnvironmentSwitchState ReadEnvironmentSwitch(const char* a_name)
	{
		char value[16]{};
		if (ReadRegistryEnvironmentValue(HKEY_CURRENT_USER, "Environment", a_name, value)) {
			return {
				IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kUserRegistry
			};
		}

		if (ReadRegistryEnvironmentValue(
				HKEY_LOCAL_MACHINE,
				"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				a_name,
				value)) {
			return {
				IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kMachineRegistry
			};
		}

		SetLastError(ERROR_SUCCESS);
		const auto length = GetEnvironmentVariableA(
			a_name,
			value,
			static_cast<DWORD>(sizeof(value)));
		if (length > 0) {
			return {
				length < sizeof(value) && IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kProcess
			};
		}
		if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			return { false, EnvironmentSwitchSource::kProcess };
		}

		return {};
	}

	bool ParseEnvironmentUIntValue(const char* a_value, std::uint32_t& a_result)
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

	EnvironmentUIntState MakeEnvironmentUIntState(
		const char* a_value,
		EnvironmentSwitchSource a_source,
		bool a_available)
	{
		EnvironmentUIntState state{};
		state.source = a_source;
		state.present = true;
		if (!a_available) {
			return state;
		}

		state.valid = ParseEnvironmentUIntValue(a_value, state.value);
		return state;
	}

	EnvironmentUIntState ReadEnvironmentUInt(const char* a_name)
	{
		char value[16]{};
		if (ReadRegistryEnvironmentValue(HKEY_CURRENT_USER, "Environment", a_name, value)) {
			return MakeEnvironmentUIntState(value, EnvironmentSwitchSource::kUserRegistry, true);
		}

		if (ReadRegistryEnvironmentValue(
				HKEY_LOCAL_MACHINE,
				"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				a_name,
				value)) {
			return MakeEnvironmentUIntState(value, EnvironmentSwitchSource::kMachineRegistry, true);
		}

		SetLastError(ERROR_SUCCESS);
		const auto length = GetEnvironmentVariableA(
			a_name,
			value,
			static_cast<DWORD>(sizeof(value)));
		if (length > 0) {
			return MakeEnvironmentUIntState(
				value,
				EnvironmentSwitchSource::kProcess,
				length < sizeof(value));
		}
		if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			return MakeEnvironmentUIntState("", EnvironmentSwitchSource::kProcess, false);
		}

		return {};
	}

	std::uint32_t GetPreNGClusterPrepassProofFrameBudget()
	{
		static const std::uint32_t budget = [] {
			const auto state = ReadEnvironmentUInt(kPreNGClusterPrepassProofFramesEnv);
			auto resolved = kPreNGDefaultClusterPrepassProofFrames;
			auto clamped = false;
			if (state.present && state.valid) {
				const auto requested = state.value;
				resolved = std::clamp(
					requested,
					kPreNGMinClusterPrepassProofFrames,
					kPreNGMaxClusterPrepassProofFrames);
				clamped = resolved != requested;
			}

			logger::info(
				"[LightLimitFix] PreNG clustered Prepass proof frame budget resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{}",
				kPreNGClusterPrepassProofFramesEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultClusterPrepassProofFrames,
				kPreNGMinClusterPrepassProofFrames,
				kPreNGMaxClusterPrepassProofFrames);
			return resolved;
		}();
		return budget;
	}

	std::uint32_t GetPreNGBSLightingResourcePrepassProofFrameBudget()
	{
		static const std::uint32_t budget = [] {
			const auto state = ReadEnvironmentUInt(kPreNGBSLightingResourcePrepassProofFramesEnv);
			auto resolved = kPreNGDefaultBSLightingResourcePrepassProofFrames;
			auto clamped = false;
			if (state.present && state.valid) {
				const auto requested = state.value;
				resolved = std::clamp(
					requested,
					kPreNGMinBSLightingResourcePrepassProofFrames,
					kPreNGMaxBSLightingResourcePrepassProofFrames);
				clamped = resolved != requested;
			}

			logger::info(
				"[LightLimitFix] PreNG BSLighting resource Prepass proof frame budget resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{}",
				kPreNGBSLightingResourcePrepassProofFramesEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultBSLightingResourcePrepassProofFrames,
				kPreNGMinBSLightingResourcePrepassProofFrames,
				kPreNGMaxBSLightingResourcePrepassProofFrames);
			return resolved;
		}();
		return budget;
	}

	std::uint32_t GetPreNGDFLightLLFAdditiveRefreshInterval()
	{
		static const std::uint32_t interval = [] {
			const auto state = ReadEnvironmentUInt(kPreNGDFLightLLFAdditiveRefreshIntervalEnv);
			auto resolved = kPreNGDefaultDFLightLLFAdditiveRefreshInterval;
			auto clamped = false;
			if (state.present && state.valid) {
				const auto requested = state.value;
				resolved = std::clamp(
					requested,
					kPreNGMinDFLightLLFAdditiveRefreshInterval,
					kPreNGMaxDFLightLLFAdditiveRefreshInterval);
				clamped = resolved != requested;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive Prepass refresh interval resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{}",
				kPreNGDFLightLLFAdditiveRefreshIntervalEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultDFLightLLFAdditiveRefreshInterval,
				kPreNGMinDFLightLLFAdditiveRefreshInterval,
				kPreNGMaxDFLightLLFAdditiveRefreshInterval);
			return resolved;
		}();
		return interval;
	}

	std::optional<std::uint32_t> GetPreNGSetupGeometryCallBudget()
	{
		static const std::optional<std::uint32_t> budget = []() -> std::optional<std::uint32_t> {
			const auto state = ReadEnvironmentUInt(kPreNGSetupGeometryCallBudgetEnv);
			if (!state.present) {
				return std::nullopt;
			}
			if (!state.valid) {
				logger::warn(
					"[LightLimitFix] PreNG SetupGeometry call budget ignored {} source={} present={} valid=false",
					kPreNGSetupGeometryCallBudgetEnv,
					EnvironmentSwitchSourceName(state.source),
					state.present);
				return std::nullopt;
			}

			const auto resolved = std::min(state.value, kPreNGMaxSetupGeometryCallBudget);
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry call budget resolved {}={} source={} requested={} max={}",
				kPreNGSetupGeometryCallBudgetEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.value,
				kPreNGMaxSetupGeometryCallBudget);
			return resolved;
		}();
		return budget;
	}

	std::uint32_t GetPreNGSetupGeometryFrameBudget()
	{
		static const std::uint32_t budget = [] {
			const auto state = ReadEnvironmentUInt(kPreNGSetupGeometryFrameBudgetEnv);
			if (!state.present) {
				logger::info(
					"[LightLimitFix] PreNG SetupGeometry frame budget default {}={} max={} source=default",
					kPreNGSetupGeometryFrameBudgetEnv,
					kPreNGDefaultSetupGeometryFrameBudget,
					kPreNGMaxSetupGeometryFrameBudget);
				return kPreNGDefaultSetupGeometryFrameBudget;
			}
			if (!state.valid) {
				logger::warn(
					"[LightLimitFix] PreNG SetupGeometry frame budget invalid; using default {}={} source={}",
					kPreNGSetupGeometryFrameBudgetEnv,
					kPreNGDefaultSetupGeometryFrameBudget,
					EnvironmentSwitchSourceName(state.source));
				return kPreNGDefaultSetupGeometryFrameBudget;
			}
			const auto resolved = std::min(state.value, kPreNGMaxSetupGeometryFrameBudget);
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry frame budget resolved {}={} source={} requested={} max={}",
				kPreNGSetupGeometryFrameBudgetEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.value,
				kPreNGMaxSetupGeometryFrameBudget);
			return resolved;
		}();
		return budget;
	}

	bool TryReservePreNGSetupGeometryCall()
	{
		const auto budget = GetPreNGSetupGeometryCallBudget();
		if (!budget) {
			return true;
		}

		static std::atomic_uint32_t setupGeometryBudgetedCalls = 0;
		static std::atomic_bool setupGeometryBudgetLogged = false;
		const auto callIndex = setupGeometryBudgetedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
		if (callIndex <= *budget) {
			return true;
		}

		if (!setupGeometryBudgetLogged.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry call budget reached; holding scene-light decode/upload after calls={} budget={} env={}",
				callIndex - 1,
				*budget,
				kPreNGSetupGeometryCallBudgetEnv);
		}
		return false;
	}

	bool TryReservePreNGSetupGeometryFrameSample()
	{
		const auto budget = GetPreNGSetupGeometryFrameBudget();
		if (budget == 0) {
			return false;
		}

		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		static std::uint64_t sampledFrame = static_cast<std::uint64_t>(-1);
		static std::uint32_t sampledThisFrame = 0;
		if (sampledFrame != frame) {
			sampledFrame = frame;
			sampledThisFrame = 0;
		}

		++sampledThisFrame;
		if (sampledThisFrame <= budget) {
			return true;
		}

		static std::atomic_uint32_t skippedSamples = 0;
		const auto skipped = ++skippedSamples;
		if (skipped <= 8 || skipped % 4096 == 0) {
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry frame budget held samples={} frame={} accepted={} budget={}",
				skipped,
				frame,
				budget,
				budget);
		}
		return false;
	}

	bool TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame()
	{
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		auto nextFrame = s_preNGBSLightingSetupGeometryNoLightNextProbeFrame.load(std::memory_order_relaxed);
		while (frame >= nextFrame) {
			if (s_preNGBSLightingSetupGeometryNoLightNextProbeFrame.compare_exchange_weak(
					nextFrame,
					frame + 1,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
				return true;
			}
		}
		return false;
	}

	void ExtendPreNGBSLightingSetupGeometryBypassWindow()
	{
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (!runtime) {
			return;
		}

		const auto frame = runtime->GetFrameCount();
		const auto newBypassUntil = frame + kPreNGBSLightingSetupGeometryNoLightBypassFrames;
		auto bypassUntil = s_preNGBSLightingSetupGeometryBypassUntilFrame.load(std::memory_order_relaxed);
		while (bypassUntil < newBypassUntil) {
			if (s_preNGBSLightingSetupGeometryBypassUntilFrame.compare_exchange_weak(
					bypassUntil,
					newBypassUntil,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
				return;
			}
		}
	}

	std::string_view DetectPreNGBSLightingResourceProofMenuBlock()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return {};
		}

		for (const auto menu : kPreNGBSLightingResourceProofBlockingMenus) {
			if (ui->GetMenuOpen(menu.data())) {
				return menu;
			}
		}

		return {};
	}

	bool ShouldDeferPreNGBSLightingResourceProofForMenu()
	{
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		const auto menuBlock = DetectPreNGBSLightingResourceProofMenuBlock();

		auto logDefer = [&](const char* a_reason, std::uint64_t a_until) {
			const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
			if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0) {
				logger::info(
					"[LightLimitFix] PreNG BSLighting resource proof deferred for UI menu/settle deferrals={} frame={} until={} reason={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
					deferralIndex,
					frame,
					a_until,
					a_reason,
					kPreNGBSLightingResourceProofMenuSettleFrames);
			}
		};

		if (!menuBlock.empty()) {
			if (!s_preNGBSLightingResourceProofSuppressedByLockpicking.exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG BSLighting resource proof suppressed after preview-menu descriptor path frame={} menu={}; resource-only proof already validated, skipping delayed clustered Prepass and deferred b3/t35-t37 bind for this process",
					frame,
					menuBlock.data());
			}
			if (s_preNGBSLightingResourceProofSuppressedByLockpicking.load(std::memory_order_relaxed)) {
				return true;
			}

			const auto newBypassUntil = frame + kPreNGBSLightingResourceProofMenuSettleFrames;
			auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
			while (bypassUntil < newBypassUntil) {
				if (s_preNGBSLightingResourceProofBypassUntilFrame.compare_exchange_weak(
						bypassUntil,
						newBypassUntil,
						std::memory_order_relaxed,
						std::memory_order_relaxed)) {
					break;
				}
			}
			logDefer(menuBlock.data(), newBypassUntil);
			return true;
		}

		if (runtime) {
			const auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
			if (frame < bypassUntil) {
				logDefer("post-BSLighting-resource-settle", bypassUntil);
				return true;
			}
		}

		return false;
	}

	void ExtendPreNGBSLightingResourceProofDescriptorSettle()
	{
		if (s_preNGBSLightingResourceProofSuppressedByLockpicking.load(std::memory_order_relaxed)) {
			return;
		}

		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (!runtime) {
			return;
		}

		const auto frame = runtime->GetFrameCount();
		const auto newBypassUntil = frame + kPreNGBSLightingResourceProofMenuSettleFrames;
		auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
		while (bypassUntil < newBypassUntil) {
			if (s_preNGBSLightingResourceProofBypassUntilFrame.compare_exchange_weak(
					bypassUntil,
					newBypassUntil,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
				break;
			}
		}

		const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
		if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting resource proof deferred after descriptor burst deferrals={} frame={} until={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
				deferralIndex,
				frame,
				newBypassUntil,
				kPreNGBSLightingResourceProofMenuSettleFrames);
		}
	}

	bool IsTruthyEnvironmentSwitch(const char* a_name)
	{
		return ReadEnvironmentSwitch(a_name).enabled;
	}

	void LogPreNGDiagnosticEnvironmentSnapshot()
	{
		static bool logged = false;
		if (logged) {
			return;
		}
		logged = true;

		const auto setupGeometryHookState = ReadEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv);
		const auto setupGeometryBindCBState = ReadEnvironmentSwitch(kPreNGSetupGeometryStrictCBBindEnv);
		const auto setupGeometryPersistCBState = ReadEnvironmentSwitch(kPreNGSetupGeometryPersistStrictCBEnv);
		const auto hookState = ReadEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
		const auto strictCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
		const auto bindCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
		const auto bindClusterSRVState = ReadEnvironmentSwitch(kPreNGClusterSRVBindEnv);
		const auto persistentClusterPrepassState = ReadEnvironmentSwitch(kPreNGPersistentClusterPrepassEnv);
		const auto dflightDrawStateStrictCBBindState = ReadEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
		const auto dflightDrawStateClusterSRVBindState = ReadEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
		const auto dflightResourceNoOpPassState = ReadEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
		const auto dflightFullContractNoOpPassState = ReadEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
		const auto dflightLLFAdditivePassState = ReadEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
		const auto dflightLegacyAdditiveProofState = ReadEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
		const auto dflightLLFAdditivePersistentState = ReadEnvironmentSwitch(kPreNGDFLightLLFAdditivePersistentEnv);
		const auto dfCompositeResourceBindState = ReadEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv);
		const auto dfCompositeSafeBindState = ReadEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
		const auto dfCompositeVisibleLLFState = ReadEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
		const auto bsLightingResourceBindState = ReadEnvironmentSwitch(kPreNGBSLightingResourceBindEnv);
		const auto bsLightingSetupGeometryResourceBindState = ReadEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
		const auto bsLightingContractCompileState = ReadEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
		const auto bsLightingConsumerCompileState = ReadEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
		const auto bsLightingDescriptorObserveState = ReadEnvironmentSwitch(kPreNGBSLightingDescriptorObserveEnv);
		const auto bsLightingVanillaBindState = ReadEnvironmentSwitch(kPreNGBSLightingVanillaBindEnv);
		const auto bsLightingLLFBindState = ReadEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
		const auto shaderObjectMetadataState = ReadEnvironmentSwitch(kPreNGShaderObjectMetadataEnv);
		const auto tracePSState = ReadEnvironmentSwitch(kPreNGTraceLLFPixelEnv);
		const auto dflightContractCompileState = ReadEnvironmentSwitch(kPreNGDFLightContractCompileEnv);
		const auto dflightCandidateCompileState = ReadEnvironmentSwitch(kPreNGDFLightCandidateCompileEnv);

		logger::info(
			"[LightLimitFix] PreNG diagnostic env snapshot {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} sources setupGeometryHook={} setupGeometryBindCB={} setupGeometryPersistCB={} hook={} strictCB={} bindCB={} bindSRV={} persistentPrepass={} dflightDrawStateStrictCB={} dflightDrawStateClusterSRV={} dflightResourceNoOp={} dflightFullContractNoOp={} dflightLLFAdditive={} dflightLegacyAdditiveProof={} dflightLLFAdditivePersistent={} dfCompositeResourceBind={} dfCompositeSafeBind={} dfCompositeVisibleLLF={} bsLightingResourceBind={} bsLightingSetupGeometryResourceBind={} shaderObjectMetadata={} tracePS={} dflightContractCompile={} dflightCandidateCompile={}",
			kPreNGSetupGeometryHookOptInEnv,
			setupGeometryHookState.enabled ? "on" : "off",
			kPreNGSetupGeometryStrictCBBindEnv,
			setupGeometryBindCBState.enabled ? "on" : "off",
			kPreNGSetupGeometryPersistStrictCBEnv,
			setupGeometryPersistCBState.enabled ? "on" : "off",
			kPreNGPointLightHookOptInEnv,
			hookState.enabled ? "on" : "off",
			kPreNGStrictLightCBDiagnosticEnv,
			strictCBState.enabled ? "on" : "off",
			kPreNGStrictLightCBBindEnv,
			bindCBState.enabled ? "on" : "off",
			kPreNGClusterSRVBindEnv,
			bindClusterSRVState.enabled ? "on" : "off",
			kPreNGPersistentClusterPrepassEnv,
			persistentClusterPrepassState.enabled ? "on" : "off",
			kPreNGDFLightDrawStateStrictCBBindEnv,
			dflightDrawStateStrictCBBindState.enabled ? "on" : "off",
			kPreNGDFLightDrawStateClusterSRVBindEnv,
			dflightDrawStateClusterSRVBindState.enabled ? "on" : "off",
			kPreNGDFLightResourceNoOpPassEnv,
			dflightResourceNoOpPassState.enabled ? "on" : "off",
			kPreNGDFLightFullContractNoOpPassEnv,
			dflightFullContractNoOpPassState.enabled ? "on" : "off",
			kPreNGDFLightLLFAdditivePassEnv,
			dflightLLFAdditivePassState.enabled ? "on" : "off",
			kPreNGDFLightLegacyAdditiveProofEnv,
			dflightLegacyAdditiveProofState.enabled ? "on" : "off",
			kPreNGDFLightLLFAdditivePersistentEnv,
			dflightLLFAdditivePersistentState.enabled ? "on" : "off",
			kPreNGDFCompositeResourceBindEnv,
			dfCompositeResourceBindState.enabled ? "on" : "off",
			kPreNGDFCompositeSafeBindEnv,
			dfCompositeSafeBindState.enabled ? "on" : "off",
			kPreNGDFCompositeVisibleLLFEnv,
			dfCompositeVisibleLLFState.enabled ? "on" : "off",
			kPreNGBSLightingResourceBindEnv,
			bsLightingResourceBindState.enabled ? "on" : "off",
			kPreNGBSLightingSetupGeometryResourceBindEnv,
			bsLightingSetupGeometryResourceBindState.enabled ? "on" : "off",
			kPreNGShaderObjectMetadataEnv,
			shaderObjectMetadataState.enabled ? "on" : "off",
			kPreNGTraceLLFPixelEnv,
			tracePSState.enabled ? "on" : "off",
			kPreNGDFLightContractCompileEnv,
			dflightContractCompileState.enabled ? "on" : "off",
			kPreNGDFLightCandidateCompileEnv,
			dflightCandidateCompileState.enabled ? "on" : "off",
			EnvironmentSwitchSourceName(setupGeometryHookState.source),
			EnvironmentSwitchSourceName(setupGeometryBindCBState.source),
			EnvironmentSwitchSourceName(setupGeometryPersistCBState.source),
			EnvironmentSwitchSourceName(hookState.source),
			EnvironmentSwitchSourceName(strictCBState.source),
			EnvironmentSwitchSourceName(bindCBState.source),
			EnvironmentSwitchSourceName(bindClusterSRVState.source),
			EnvironmentSwitchSourceName(persistentClusterPrepassState.source),
			EnvironmentSwitchSourceName(dflightDrawStateStrictCBBindState.source),
			EnvironmentSwitchSourceName(dflightDrawStateClusterSRVBindState.source),
			EnvironmentSwitchSourceName(dflightResourceNoOpPassState.source),
			EnvironmentSwitchSourceName(dflightFullContractNoOpPassState.source),
			EnvironmentSwitchSourceName(dflightLLFAdditivePassState.source),
			EnvironmentSwitchSourceName(dflightLegacyAdditiveProofState.source),
			EnvironmentSwitchSourceName(dflightLLFAdditivePersistentState.source),
			EnvironmentSwitchSourceName(dfCompositeResourceBindState.source),
			EnvironmentSwitchSourceName(dfCompositeSafeBindState.source),
			EnvironmentSwitchSourceName(dfCompositeVisibleLLFState.source),
			EnvironmentSwitchSourceName(bsLightingResourceBindState.source),
			EnvironmentSwitchSourceName(bsLightingSetupGeometryResourceBindState.source),
			EnvironmentSwitchSourceName(shaderObjectMetadataState.source),
			EnvironmentSwitchSourceName(tracePSState.source),
			EnvironmentSwitchSourceName(dflightContractCompileState.source),
			EnvironmentSwitchSourceName(dflightCandidateCompileState.source));

		logger::info(
			"[LightLimitFix] PreNG BSLighting proof env snapshot {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} sources bsLightingContractCompile={} bsLightingConsumerCompile={} bsLightingDescriptorObserve={} bsLightingResourceBind={} bindStrictCB={} bindClusterSRV={} bsLightingVanillaBind={} bsLightingLLFBind={}",
			kPreNGBSLightingContractCompileEnv,
			bsLightingContractCompileState.enabled ? "on" : "off",
			kPreNGBSLightingConsumerCompileEnv,
			bsLightingConsumerCompileState.enabled ? "on" : "off",
			kPreNGBSLightingDescriptorObserveEnv,
			bsLightingDescriptorObserveState.enabled ? "on" : "off",
			kPreNGBSLightingResourceBindEnv,
			bsLightingResourceBindState.enabled ? "on" : "off",
			kPreNGStrictLightCBBindEnv,
			bindCBState.enabled ? "on" : "off",
			kPreNGClusterSRVBindEnv,
			bindClusterSRVState.enabled ? "on" : "off",
			kPreNGBSLightingVanillaBindEnv,
			bsLightingVanillaBindState.enabled ? "on" : "off",
			kPreNGBSLightingLLFBindEnv,
			bsLightingLLFBindState.enabled ? "on" : "off",
			EnvironmentSwitchSourceName(bsLightingContractCompileState.source),
			EnvironmentSwitchSourceName(bsLightingConsumerCompileState.source),
			EnvironmentSwitchSourceName(bsLightingDescriptorObserveState.source),
			EnvironmentSwitchSourceName(bsLightingResourceBindState.source),
			EnvironmentSwitchSourceName(bindCBState.source),
			EnvironmentSwitchSourceName(bindClusterSRVState.source),
			EnvironmentSwitchSourceName(bsLightingVanillaBindState.source),
			EnvironmentSwitchSourceName(bsLightingLLFBindState.source));
	}

	bool ShouldInstallPreNGInternalPointLightHook()
	{
		if constexpr (kPreNGEnableInternalPointLightHook) {
			return true;
		}

		return IsTruthyEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
	}

	bool ShouldUpdatePreNGStrictLightCB()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
		return enabled;
	}

	bool ShouldBindPreNGStrictLightCB()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGSetupGeometryStrictLightCB()
	{
		static const bool enabled =
			IsTruthyEnvironmentSwitch(kPreNGSetupGeometryStrictCBBindEnv) &&
			ShouldBindPreNGStrictLightCB();
		return enabled;
	}

	bool ShouldPersistPreNGSetupGeometryStrictLightCB()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGSetupGeometryPersistStrictCBEnv);
		return enabled;
	}

	bool ShouldBindPreNGClusterSRVs()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGClusterSRVBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGPrepassResources()
	{
		static const bool enabled = [] {
			const auto state = ReadEnvironmentSwitch(kPreNGPrepassResourceBindEnv);
			const bool resolved = state.source == EnvironmentSwitchSource::kNone || state.enabled;
			logger::info(
				"[LightLimitFix] PreNG Prepass resource bind resolved {}={} source={} default=on",
				kPreNGPrepassResourceBindEnv,
				resolved ? "on" : "off",
				EnvironmentSwitchSourceName(state.source));
			return resolved;
		}();
		return enabled;
	}

	bool ShouldBindPreNGDFLightDrawStateStrictLightCB()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightDrawStateClusterSRVs()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
		return enabled;
	}

	bool ShouldUsePreNGSetupGeometryStrictLightCBProof()
	{
		static const bool enabled =
			IsTruthyEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv) &&
			(ShouldUpdatePreNGStrictLightCB() || ShouldBindPreNGStrictLightCB());
		return enabled;
	}

	bool ShouldPersistPreNGClusterPrepass()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGPersistentClusterPrepassEnv);
		return enabled;
	}

	std::uint32_t GetPreNGPersistentClusterPrepassRefreshInterval()
	{
		static const std::uint32_t interval = [] {
			const auto state = ReadEnvironmentUInt(kPreNGPersistentClusterPrepassRefreshIntervalEnv);
			auto resolved = kPreNGDefaultPersistentClusterPrepassRefreshInterval;
			auto clamped = false;
			if (state.present && state.valid) {
				resolved = state.value;
				if (resolved < kPreNGMinPersistentClusterPrepassRefreshInterval) {
					resolved = kPreNGMinPersistentClusterPrepassRefreshInterval;
					clamped = true;
				} else if (resolved > kPreNGMaxPersistentClusterPrepassRefreshInterval) {
					resolved = kPreNGMaxPersistentClusterPrepassRefreshInterval;
					clamped = true;
				}
			}

			logger::info(
				"[LightLimitFix] PreNG persistent clustered Prepass refresh interval resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{} note=0-holds-warmed-payload",
				kPreNGPersistentClusterPrepassRefreshIntervalEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultPersistentClusterPrepassRefreshInterval,
				kPreNGMinPersistentClusterPrepassRefreshInterval,
				kPreNGMaxPersistentClusterPrepassRefreshInterval);
			return resolved;
		}();
		return interval;
	}

	bool ShouldReusePreNGClusterPrepassPayload()
	{
		static const bool enabled = [] {
			const auto state = ReadEnvironmentSwitch(kPreNGClusterPrepassReuseEnv);
			const bool resolved = state.source == EnvironmentSwitchSource::kNone || state.enabled;
			logger::info(
				"[LightLimitFix] PreNG clustered Prepass payload reuse resolved {}={} source={} default=on",
				kPreNGClusterPrepassReuseEnv,
				resolved ? "on" : "off",
				EnvironmentSwitchSourceName(state.source));
			return resolved;
		}();
		return enabled;
	}

	bool ShouldReusePreNGShadowSceneFastReuse()
	{
		static const bool enabled = [] {
			const auto state = ReadEnvironmentSwitch(kPreNGShadowSceneFastReuseEnv);
			const bool resolved = state.source == EnvironmentSwitchSource::kNone || state.enabled;
			logger::info(
				"[LightLimitFix] PreNG ShadowScene fast reuse resolved {}={} source={} default=on",
				kPreNGShadowSceneFastReuseEnv,
				resolved ? "on" : "off",
				EnvironmentSwitchSourceName(state.source));
			return resolved;
		}();
		return enabled;
	}

	std::uint32_t GetPreNGShadowSceneFastReuseRefreshInterval()
	{
		static const std::uint32_t interval = [] {
			const auto state = ReadEnvironmentUInt(kPreNGShadowSceneFastReuseRefreshIntervalEnv);
			auto resolved = kPreNGDefaultShadowSceneFastReuseRefreshInterval;
			auto clamped = false;
			if (state.present && state.valid) {
				resolved = state.value;
				if (resolved < kPreNGMinShadowSceneFastReuseRefreshInterval) {
					resolved = kPreNGMinShadowSceneFastReuseRefreshInterval;
					clamped = true;
				} else if (resolved > kPreNGMaxShadowSceneFastReuseRefreshInterval) {
					resolved = kPreNGMaxShadowSceneFastReuseRefreshInterval;
					clamped = true;
				}
			}

			logger::info(
				"[LightLimitFix] PreNG ShadowScene fast reuse refresh interval resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{}",
				kPreNGShadowSceneFastReuseRefreshIntervalEnv,
				resolved,
				EnvironmentSwitchSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultShadowSceneFastReuseRefreshInterval,
				kPreNGMinShadowSceneFastReuseRefreshInterval,
				kPreNGMaxShadowSceneFastReuseRefreshInterval);
			return resolved;
		}();
		return interval;
	}

	bool ShouldRunPreNGDFLightResourceNoOpPass()
	{
		return IsTruthyEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
	}

	bool ShouldRunPreNGDFLightFullContractNoOpPass()
	{
		return IsTruthyEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
	}

	bool ShouldRunPreNGDFLightLLFAdditivePass()
	{
		static const bool enabled = [] {
			const bool requested = IsTruthyEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
			const bool legacyProofAllowed = IsTruthyEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
			if (requested && !legacyProofAllowed) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight LLF additive pass held; this legacy proof path is not the Skyrim-CS LLF direction. Keep it off for normal development, or set {}=1 only to reproduce the old additive proof.",
					kPreNGDFLightLegacyAdditiveProofEnv);
			} else if (requested) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight LLF additive legacy proof active; this is not the final Skyrim-CS route and should not be used for performance or visual validation.");
			}
			return requested && legacyProofAllowed;
		}();
		return enabled;
	}

	bool ShouldRunPreNGDFLightFullContractVisibleLLF()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightFullContractVisibleLLFEnv);
		return enabled;
	}

	bool ShouldRunPreNGDFCompositeVisibleLLF()
	{
		static const bool enabled =
			IsTruthyEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv) &&
			IsTruthyEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
		return enabled;
	}

	bool ShouldUsePreNGDFLightDescriptorDemandResources()
	{
		static const bool enabled = [] {
			const bool requested = IsTruthyEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
			const bool unsafeOverride = IsTruthyEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
			return requested && unsafeOverride;
		}();
		return enabled;
	}

	bool ShouldUsePreNGDFCompositeDescriptorDemandResources()
	{
		static const bool enabled =
			IsTruthyEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv) ||
			ShouldRunPreNGDFCompositeVisibleLLF();
		return enabled;
	}

	bool ShouldUsePreNGBSLightingDescriptorDemandResources()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingResourceBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGBSLightingSetupGeometryResources()
	{
		static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
		return enabled;
	}

	void LogPreNGHookReachabilityWatchdog(std::uint64_t a_frame)
	{
		if (a_frame < 600) {
			return;
		}

		const bool pointRequested = ShouldInstallPreNGInternalPointLightHook();
		const bool setupResourceRequested = ShouldBindPreNGBSLightingSetupGeometryResources();
		if (!pointRequested && !setupResourceRequested) {
			return;
		}

		static std::atomic_bool logged = false;
		if (logged.exchange(true, std::memory_order_relaxed)) {
			return;
		}

		logger::info(
			"[LightLimitFix] PreNG hook reachability watchdog frame={} pointRequested={} pointInstalled={} pointPatchVerified={} pointCalls={} setupResourceRequested={} setupInstalled={} setupCalls={} setupBypassCalls={}; zero-call hooks mean this run has not exercised the verified BSLighting/point-light route yet, so visible LLF remains held",
			a_frame,
			pointRequested,
			s_preNGPointLightHookInstalled.load(std::memory_order_acquire),
			s_preNGPointLightHookPatchVerified.load(std::memory_order_acquire),
			s_preNGPointLightHookCallCount.load(std::memory_order_relaxed),
			setupResourceRequested,
			s_preNGBSLightingSetupGeometryHookInstalled.load(std::memory_order_acquire),
			s_preNGBSLightingSetupGeometryHookCallCount.load(std::memory_order_relaxed),
			s_preNGBSLightingSetupGeometryBypassCallCount.load(std::memory_order_relaxed));
	}

	bool ShouldHoldPreNGDFLightPreparedState()
	{
		return ShouldRunPreNGDFLightResourceNoOpPass() ||
		       ShouldRunPreNGDFLightFullContractNoOpPass() ||
		       ShouldBindPreNGDFLightDrawStateClusterSRVs() ||
		       ShouldRunPreNGDFLightLLFAdditivePass();
	}

	bool ShouldUsePreNGPersistentClusterPrepassConsumer()
	{
		const bool visibleFullContractConsumer =
			ShouldRunPreNGDFLightFullContractVisibleLLF() &&
			s_preNGDFLightLLFConsumerDescriptorObserved.load(std::memory_order_relaxed) &&
			(ShouldBindPreNGStrictLightCB() || ShouldBindPreNGClusterSRVs());
		const bool dfCompositeVisibleConsumer =
			ShouldRunPreNGDFCompositeVisibleLLF() &&
			s_preNGDFCompositeLLFConsumerDescriptorObserved.load(std::memory_order_relaxed) &&
			(ShouldBindPreNGStrictLightCB() || ShouldBindPreNGClusterSRVs());

		return ShouldBindPreNGDFLightDrawStateClusterSRVs() ||
		       ShouldRunPreNGDFLightLLFAdditivePass() ||
		       visibleFullContractConsumer ||
		       dfCompositeVisibleConsumer;
	}

	bool ShouldRunPreNGClusterPrepassProof()
	{
		const bool dflightLLFAdditiveRequested = ShouldRunPreNGDFLightLLFAdditivePass();
		const bool prepassResourceBindRequested = ShouldBindPreNGPrepassResources();
		const bool strictCBProofRequested =
			prepassResourceBindRequested &&
			(ShouldUpdatePreNGStrictLightCB() || ShouldBindPreNGStrictLightCB()) &&
			!ShouldUsePreNGSetupGeometryStrictLightCBProof();
		const bool prepassClusterSRVProofRequested =
			prepassResourceBindRequested &&
			ShouldBindPreNGClusterSRVs();
		const bool fullContractDescriptorObserved =
			s_preNGDFLightLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
		const bool dfCompositeDescriptorObserved =
			s_preNGDFCompositeLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
		const bool bsLightingDescriptorObserved =
			s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
		const bool descriptorResourceSubGateRequested =
			ShouldBindPreNGStrictLightCB() ||
			ShouldBindPreNGClusterSRVs();
		const bool bsLightingResourceProofGateRequested =
			ShouldUsePreNGBSLightingDescriptorDemandResources() &&
			bsLightingDescriptorObserved &&
			descriptorResourceSubGateRequested;
		const bool bsLightingResourceProofSuppressed =
			s_preNGBSLightingResourceProofSuppressedByLockpicking.load(std::memory_order_relaxed);
		if (bsLightingResourceProofGateRequested && bsLightingResourceProofSuppressed) {
			static std::atomic_bool loggedLockpickingSuppression = false;
			if (!loggedLockpickingSuppression.exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG clustered Prepass held for BSLighting resource proof because preview-menu (Lockpicking/Examine) suppression is active; descriptorObserved={} strictOrClusterGate={} env={}; delayed 1024-light proof work is skipped",
					bsLightingDescriptorObserved,
					descriptorResourceSubGateRequested,
					kPreNGBSLightingResourceBindEnv);
			}
		}
		const bool bsLightingResourceProofRequested =
			bsLightingResourceProofGateRequested &&
			!bsLightingResourceProofSuppressed;
		const bool bsLightingResourceProofMenuDeferred =
			bsLightingResourceProofRequested &&
			ShouldDeferPreNGBSLightingResourceProofForMenu();
		const bool descriptorDemandRequested =
			(ShouldUsePreNGDFLightDescriptorDemandResources() ||
			 fullContractDescriptorObserved ||
			 (ShouldUsePreNGDFCompositeDescriptorDemandResources() && dfCompositeDescriptorObserved) ||
				 (bsLightingResourceProofRequested && !bsLightingResourceProofMenuDeferred)) &&
			descriptorResourceSubGateRequested;
		const bool proofRequested =
			strictCBProofRequested ||
			prepassClusterSRVProofRequested ||
			descriptorDemandRequested ||
			ShouldBindPreNGDFLightDrawStateClusterSRVs() ||
			ShouldRunPreNGDFLightResourceNoOpPass() ||
			ShouldRunPreNGDFLightFullContractNoOpPass() ||
			dflightLLFAdditiveRequested;
		if (!proofRequested) {
			static bool loggedHeld = false;
			if (!loggedHeld) {
				logger::info(
					"[LightLimitFix] PreNG clustered Prepass held; strict CB proof is served by SetupGeometry when {}=1. {} and {} are resource sub-gates and no longer start Prepass without a Prepass bind or descriptor consumer; enable {}, {}, {}, {}, {}, or {} for Prepass/DFLight/DFComposite contract proof work, or {}=1 plus a BSLighting descriptor observation for BSLighting resource proof work. {}=1 only extends an on-demand DFLight persistent proof and no longer starts Prepass by itself. The old DFLight additive path requires {}=1 plus {}=1 and is not the Skyrim-CS LLF direction.",
					kPreNGSetupGeometryHookOptInEnv,
					kPreNGStrictLightCBBindEnv,
					kPreNGClusterSRVBindEnv,
					kPreNGPrepassResourceBindEnv,
					kPreNGDFLightDrawStateClusterSRVBindEnv,
					kPreNGDFLightResourceNoOpPassEnv,
					kPreNGDFLightFullContractNoOpPassEnv,
					kPreNGDFLightFullShadowedDescriptorConsumerEnv,
					kPreNGDFCompositeResourceBindEnv,
					kPreNGBSLightingResourceBindEnv,
					kPreNGPersistentClusterPrepassEnv,
					kPreNGDFLightLegacyAdditiveProofEnv,
					kPreNGDFLightLLFAdditivePassEnv);
				loggedHeld = true;
			}
			return false;
		}

		const bool persistentProofRequested = ShouldUsePreNGPersistentClusterPrepassConsumer();
		if (ShouldPersistPreNGClusterPrepass()) {
			if (persistentProofRequested) {
				return true;
			}
			static bool loggedPersistentFinite = false;
			if (!loggedPersistentFinite) {
				logger::info(
					"[LightLimitFix] PreNG persistent clustered Prepass requested but no on-demand persistent LLF consumer is active; Prepass-owned b3/t35-t37 resource proof and finite DFLight no-op proofs remain finite to avoid cached clustered payload affecting non-visible passes after their draw budget ends. Enable {} or {} for DFLight persistent proof work; {}, {}, and {} stay finite proof gates.",
					kPreNGDFLightDrawStateClusterSRVBindEnv,
					kPreNGDFLightLLFAdditivePassEnv,
					kPreNGDFLightResourceNoOpPassEnv,
					kPreNGDFLightFullContractNoOpPassEnv,
					kPreNGPrepassResourceBindEnv);
				loggedPersistentFinite = true;
			}
		}

		static std::atomic_uint32_t proofFrameCount = 0;
		const auto frameBudget = GetPreNGClusterPrepassProofFrameBudget();
		const bool bsLightingPayloadPending =
			bsLightingResourceProofRequested &&
			!bsLightingResourceProofMenuDeferred &&
			(!globals::features::lightLimitFix.loaded ||
			 !globals::features::lightLimitFix.HasPreNGBSLightingDescriptorConsumerData());
		const auto activeFrameBudget = bsLightingPayloadPending ?
			std::max(frameBudget, GetPreNGBSLightingResourcePrepassProofFrameBudget()) :
			frameBudget;
		auto current = proofFrameCount.load(std::memory_order_relaxed);
		while (current < activeFrameBudget) {
			if (proofFrameCount.compare_exchange_weak(
					current,
					current + 1,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
				if (current == 0) {
					logger::info(
						"[LightLimitFix] PreNG clustered Prepass proof window active; limiting compute/bind proof to {} frames unless {}=1 with an on-demand persistent consumer",
						frameBudget,
						kPreNGPersistentClusterPrepassEnv);
				}
				if (bsLightingPayloadPending && current == frameBudget) {
					static bool loggedBSLightingPayloadWait = false;
					if (!loggedBSLightingPayloadWait) {
						logger::info(
							"[LightLimitFix] PreNG clustered Prepass proof window extended for BSLighting resource payload; baseFrames={} extendedFrames={} env={} descriptorObserved={} payloadReady=false",
							frameBudget,
							activeFrameBudget,
							kPreNGBSLightingResourcePrepassProofFramesEnv,
							bsLightingDescriptorObserved);
						loggedBSLightingPayloadWait = true;
					}
				}
				return true;
			}
		}

		const auto refreshInterval = dflightLLFAdditiveRequested ? GetPreNGDFLightLLFAdditiveRefreshInterval() : 0;
		if (refreshInterval > 0) {
			static std::atomic_uint32_t postProofFrameCount = 0;
			static std::atomic_uint32_t refreshCount = 0;
			const auto postProofFrame = postProofFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if (postProofFrame % refreshInterval == 0) {
				const auto refreshIndex = refreshCount.fetch_add(1, std::memory_order_relaxed) + 1;
				if (refreshIndex <= 8 || refreshIndex % 64 == 0) {
					logger::info(
						"[LightLimitFix] PreNG clustered Prepass DFLight LLF additive refresh active refreshes={} postProofFrames={} interval={} proofFrames={} env={}",
						refreshIndex,
						postProofFrame,
						refreshInterval,
						frameBudget,
						kPreNGDFLightLLFAdditiveRefreshIntervalEnv);
				}
				return true;
			}
		}

		static bool loggedComplete = false;
		if (!loggedComplete) {
			logger::info(
				"[LightLimitFix] PreNG clustered Prepass proof window complete; holding per-frame light collection, cluster compute, b3, and t35-t37 after {} frames; baseFrames={} bsLightingPayloadPending={} legacyAdditiveRefreshInterval={} env={}; {}=1 requires an on-demand persistent consumer",
				activeFrameBudget,
				frameBudget,
				bsLightingPayloadPending,
				refreshInterval,
				kPreNGDFLightLLFAdditiveRefreshIntervalEnv,
				kPreNGPersistentClusterPrepassEnv);
			loggedComplete = true;
		}
		return false;
	}

	bool ShouldCompilePreNGDFLightContractProbe()
	{
		return IsTruthyEnvironmentSwitch(kPreNGDFLightContractCompileEnv);
	}

	bool ShouldCompilePreNGDFLightFullShadowedCandidate()
	{
		return IsTruthyEnvironmentSwitch(kPreNGDFLightCandidateCompileEnv);
	}

	void TryBindPreNGBSLightingDeferredDescriptorResources(LightLimitFix& a_feature)
	{
		if (s_preNGBSLightingResourceProofSuppressedByLockpicking.load(std::memory_order_relaxed) ||
			!ShouldUsePreNGBSLightingDescriptorDemandResources() ||
			!s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed) ||
			s_preNGBSLightingDeferredResourceProofComplete.load(std::memory_order_relaxed) ||
			!a_feature.HasPreNGBSLightingDescriptorConsumerData()) {
			return;
		}

		if (ShouldDeferPreNGBSLightingResourceProofForMenu()) {
			return;
		}

		const auto vertexDescriptor =
			s_preNGBSLightingLLFConsumerLastVertexDescriptor.load(std::memory_order_relaxed);
		const auto pixelDescriptor =
			s_preNGBSLightingLLFConsumerLastPixelDescriptor.load(std::memory_order_relaxed);
		const auto vanillaFound =
			s_preNGBSLightingLLFConsumerLastFound.load(std::memory_order_relaxed);
		const auto vanillaPixelShader =
			s_preNGBSLightingLLFConsumerLastVanillaPixelShader.load(std::memory_order_relaxed);

		const auto resourceState = a_feature.BindPreNGBSLightingDescriptorResourcesToPixelShader();

		static std::atomic_uint32_t deferredBindCount = 0;
		const auto bindIndex = ++deferredBindCount;
		if (bindIndex <= 8 || bindIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting deferred descriptor resources attempted binds={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X} strictCB={} clusterSRVs={} lights={} strict={} shadowMask=0x{:08X}; audit records whether current PS still matches the observed BSLighting PS",
				bindIndex,
				vertexDescriptor,
				pixelDescriptor,
				vanillaFound,
				vanillaPixelShader,
				resourceState.strictCBBound,
				resourceState.clusterSRVsBound,
				resourceState.lightCount,
				resourceState.strictLightCount,
				resourceState.shadowBitMask);
			a_feature.TracePreNGActiveLightingBindings(
				"descriptor-bslighting-resource-bind-deferred-prepass",
				static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE),
				vertexDescriptor,
				pixelDescriptor,
				vanillaFound,
				vanillaPixelShader);
		}

		if (resourceState.strictCBBound &&
			resourceState.clusterSRVsBound &&
			!s_preNGBSLightingDeferredResourceProofComplete.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting deferred resource-only proof reached b3/t35-t37 completion after clustered payload upload; future deferred proof binds are held until a visible-safe consumer is implemented");
		}
	}

	void RunPreNGDFLightCompileOnlyDiagnostic(
		const char* a_label,
		const char* a_envName,
		const char* a_source,
		bool a_enabled,
		std::atomic_bool& a_attempted,
		bool& a_loggedHeld)
	{
		if (!a_enabled) {
			if (!a_loggedHeld) {
				logger::info(
					"[LightLimitFix] PreNG DFLight {} compile held; set {}=1 to compile {} in-game; shader replacement and binding remain held",
					a_label,
					a_envName,
					a_source);
				a_loggedHeld = true;
			}
			return;
		}

		if (a_attempted.exchange(true)) {
			return;
		}

		auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
			a_source,
			"ps_5_0",
			nullptr,
			"main");
		if (!compiled) {
			logger::warn(
				"[LightLimitFix] PreNG DFLight {} compile failed source={} replacement=held bind=held",
				a_label,
				a_source);
			return;
		}

		auto* shaderCache = CommunityShaders::ShaderCache::GetSingleton();
		auto metadata = shaderCache ?
			shaderCache->GetMetadataForBytecode(
				CommunityShaders::ShaderStage::Pixel,
				compiled->data(),
				compiled->size()) :
			std::nullopt;
		const auto evidence = GetPreNGShaderSlotEvidence(metadata);

		const auto cb2Bytes = metadata ? metadata->constantBufferSizes[2] : 0;
		const auto cb12Bytes = metadata ? metadata->constantBufferSizes[12] : 0;
		const auto cb3Bytes = metadata ? metadata->constantBufferSizes[3] : 0;
		const auto t0Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 0) : 0;
		const auto t1Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 1) : 0;
		const auto t2Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 2) : 0;
		const auto t3Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 3) : 0;
		const auto t5Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 5) : 0;

		const bool fullShadowedVanilla = HasPreNGFullShadowedDFLightVanillaContract(metadata);
		const bool llfContract =
			evidence.hasMetadata &&
			evidence.declaresCB3 &&
			evidence.declaresT35 &&
			evidence.declaresT36 &&
			evidence.declaresT37 &&
			evidence.samplesT35 > 0 &&
			evidence.samplesT36 > 0 &&
			evidence.samplesT37 > 0 &&
			cb3Bytes > 0;

		logger::info(
			"[LightLimitFix] PreNG DFLight {} compile result source={} bytecode={} metadata={} buffers={} textures={} samples={} replacement=held bind=held",
			a_label,
			a_source,
			compiled->size(),
			FormatPreNGShaderMetadata(metadata),
			metadata ? FormatPreNGShaderBufferSlots(*metadata) : "none",
			metadata ? FormatPreNGShaderTextureSlots(*metadata) : "none",
			metadata ? FormatPreNGShaderTextureSampleCounts(*metadata) : "none");

		logger::info(
			"[LightLimitFix] PreNG DFLight {} evidence vanillaFullShadowed={} cb2Bytes={} cb12Bytes={} slots(t0={},t1={},t2={},t3={},t5={}) samples(t0={},t1={},t2={},t3={},t5={}) llfComplete={} cb3={} cb3Bytes={} t35={} t36={} t37={} loads(t35={},t36={},t37={}) replacement=held bind=held",
			a_label,
			fullShadowedVanilla,
			cb2Bytes,
			cb12Bytes,
			metadata && HasPreNGTextureSlot(*metadata, 0),
			metadata && HasPreNGTextureSlot(*metadata, 1),
			metadata && HasPreNGTextureSlot(*metadata, 2),
			metadata && HasPreNGTextureSlot(*metadata, 3),
			metadata && HasPreNGTextureSlot(*metadata, 5),
			t0Samples,
			t1Samples,
			t2Samples,
			t3Samples,
			t5Samples,
			llfContract,
			evidence.declaresCB3,
			cb3Bytes,
			evidence.declaresT35,
			evidence.declaresT36,
			evidence.declaresT37,
			evidence.samplesT35,
			evidence.samplesT36,
			evidence.samplesT37);
	}

	void RunPreNGDFLightContractProbeCompileDiagnostic()
	{
		static std::atomic_bool attempted = false;
		static bool loggedHeld = false;
		RunPreNGDFLightCompileOnlyDiagnostic(
			"contract probe",
			kPreNGDFLightContractCompileEnv,
			kPreNGDFLightContractProbeSource,
			ShouldCompilePreNGDFLightContractProbe(),
			attempted,
			loggedHeld);
	}

	void RunPreNGDFLightFullShadowedCandidateCompileDiagnostic()
	{
		static std::atomic_bool attempted = false;
		static bool loggedHeld = false;
		RunPreNGDFLightCompileOnlyDiagnostic(
			"full-shadowed candidate",
			kPreNGDFLightCandidateCompileEnv,
			kPreNGDFLightFullShadowedCandidateSource,
			ShouldCompilePreNGDFLightFullShadowedCandidate(),
			attempted,
			loggedHeld);
	}

	struct PreNGPointLightSetupCall
	{
		static std::int64_t thunk(
			std::uintptr_t a_pixelShader,
			RE::BSRenderPass* a_pass,
			DirectX::XMMATRIX* a_transform,
			std::int32_t a_lightCount,
			std::int32_t a_shadowArg,
			float a_worldScale,
			std::int32_t a_unknown)
		{
			const auto callIndex = s_preNGPointLightHookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
			std::uint32_t collected = 0;
			std::uint32_t strict = 0;
			std::uint32_t strictShadowBitMask = 0;
			bool strictCBUploaded = false;
			bool strictCBBound = false;
			bool clusterSRVsBound = false;
			std::uint32_t requestedLightCount = 0;
			LightLimitFix* self = nullptr;
			if (globals::features::lightLimitFix.loaded && a_lightCount > 0) {
				self = &globals::features::lightLimitFix;
				requestedLightCount = static_cast<std::uint32_t>(a_lightCount);
				collected = self->CollectLightsFromPreNGSceneLights(
					a_pass,
					requestedLightCount,
					a_shadowArg > 0 ? static_cast<std::uint32_t>(a_shadowArg) : 0);
				strict = self->currentStrictLightCount;
				strictShadowBitMask = self->strictLightDataTemp.ShadowBitMask;
			}

			const auto result = func(a_pixelShader, a_pass, a_transform, a_lightCount, a_shadowArg, a_worldScale, a_unknown);
			const auto currentPixelShader = ReadPreNGCurrentPixelShaderEntryState();

			if (self) {
				strictCBUploaded = self->UploadPreNGStrictLightDataDiagnostic();
				strictCBBound = self->BindPreNGStrictLightDataCBToPixelShader(a_pass, requestedLightCount, strictCBUploaded);
				clusterSRVsBound = self->BindPreNGClusterSRVsToPixelShader(a_pass, requestedLightCount, strictCBBound);
				strict = self->currentStrictLightCount;
				strictShadowBitMask = self->strictLightDataTemp.ShadowBitMask;
			}

			const bool logThisCall = callIndex <= 8 || callIndex % 512 == 0;
			if (self && logThisCall) {
				self->TracePreNGActiveLightingBindings(
					"point-light-hook",
					-1,
					currentPixelShader.id,
					currentPixelShader.id,
					currentPixelShader.d3dObject != 0,
					currentPixelShader.d3dObject);
			}

			if (logThisCall) {
				logger::info(
					"[LightLimitFix] PreNG internal point-light hook reached calls={} constantGroup=0x{:X} pass=0x{:X} requested={} collected={} strict={} strictCB={} b3={} t35t37={} bindOrder=post-vanilla shadowArg={} strictShadowMask=0x{:08X} worldScale={:.3f} unknown={} currentPSEntry=0x{:X} currentPSD3D=0x{:X} currentPSId=0x{:X} currentPSEntryReadable={} currentPSSlots(88={},89={},94={},96={})",
					callIndex,
					a_pixelShader,
					reinterpret_cast<std::uintptr_t>(a_pass),
					a_lightCount,
					collected,
					strict,
					strictCBUploaded ? "uploaded" : "held",
					strictCBBound ? "bound" : "held",
					clusterSRVsBound ? "bound" : "held",
					a_shadowArg,
					strictShadowBitMask,
					a_worldScale,
					a_unknown,
					currentPixelShader.entry,
					currentPixelShader.d3dObject,
					currentPixelShader.id,
					currentPixelShader.entryReadable,
					static_cast<std::uint32_t>(currentPixelShader.slot88),
					static_cast<std::uint32_t>(currentPixelShader.slot89),
					static_cast<std::uint32_t>(currentPixelShader.slot94),
					static_cast<std::uint32_t>(currentPixelShader.slot96));
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	bool VerifyPreNGPointLightHookPatch(std::uintptr_t a_runtimeCall)
	{
		const auto patch = ReadPreNGCallPatch(a_runtimeCall);
		const auto branchTarget = patch.readable ? ResolvePreNGAbsoluteJumpTarget(patch.callTarget) : 0;
		const auto thunkTarget = reinterpret_cast<std::uintptr_t>(&PreNGPointLightSetupCall::thunk);
		const auto originalTarget = F4Runtime::PreNG::POINT_LIGHT_TARGET.address();
		const bool directToThunk = patch.callTarget == thunkTarget;
		const bool branchToThunk = branchTarget == thunkTarget;
		const bool verified = patch.readable && patch.opcode == 0xE8 && (directToThunk || branchToThunk);

		if (verified) {
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook patch verified call=0x{:X} callTarget=0x{:X} branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
				a_runtimeCall,
				patch.callTarget,
				branchTarget,
				thunkTarget,
				originalTarget,
				static_cast<std::uint32_t>(patch.rel32));
		} else {
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook patch verification failed call=0x{:X} readable={} opcode=0x{:02X} callTarget=0x{:X} branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
				a_runtimeCall,
				patch.readable,
				static_cast<std::uint32_t>(patch.opcode),
				patch.callTarget,
				branchTarget,
				thunkTarget,
				originalTarget,
				static_cast<std::uint32_t>(patch.rel32));
		}

		return verified;
	}


	enum class PreNGPointLightHookState
	{
		Failed,
		Prepared,
		Installed,
		InstalledUnverified
	};

	const char* PreNGPointLightHookStateName(PreNGPointLightHookState a_state)
	{
		switch (a_state) {
		case PreNGPointLightHookState::Prepared:
			return "prepared";
		case PreNGPointLightHookState::Installed:
			return "installed";
		case PreNGPointLightHookState::InstalledUnverified:
			return "installed-unverified";
		case PreNGPointLightHookState::Failed:
		default:
			return "failed";
		}
	}

	bool CanInstallPreNGSetupGeometryHooks(PreNGPointLightHookState a_pointLightHookState)
	{
		return a_pointLightHookState == PreNGPointLightHookState::Prepared ||
		       a_pointLightHookState == PreNGPointLightHookState::Installed;
	}

	PreNGPointLightHookState PreparePreNGPointLightHook()
	{
		if (!ValidatePreNGPointLightCallsite()) {
			s_preNGPointLightHookInstalled.store(false, std::memory_order_release);
			s_preNGPointLightHookPatchVerified.store(false, std::memory_order_release);
			logger::warn("[LightLimitFix] PreNG internal point-light hook not prepared; callsite validation failed");
			return PreNGPointLightHookState::Failed;
		}

		const auto runtimeCall = F4Runtime::PreNG::POINT_LIGHT_CALL.address();
		if (ShouldInstallPreNGInternalPointLightHook()) {
			stl::write_thunk_call<PreNGPointLightSetupCall>(runtimeCall);
			const bool patchVerified = VerifyPreNGPointLightHookPatch(runtimeCall);
			s_preNGPointLightHookInstalled.store(true, std::memory_order_release);
			s_preNGPointLightHookPatchVerified.store(patchVerified, std::memory_order_release);
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook installed at call=0x{:X}; diagnostic opt-in is active; patchVerified={}",
				runtimeCall,
				patchVerified);
			return patchVerified ? PreNGPointLightHookState::Installed : PreNGPointLightHookState::InstalledUnverified;
		}

		s_preNGPointLightHookInstalled.store(false, std::memory_order_release);
		s_preNGPointLightHookPatchVerified.store(false, std::memory_order_release);

		logger::info(
			"[LightLimitFix] PreNG internal point-light hook prepared at call=0x{:X}; install gate is off (set {}=1 for diagnostic activation)",
			runtimeCall,
			kPreNGPointLightHookOptInEnv);
		return PreNGPointLightHookState::Prepared;
	}
#endif
}

void LightLimitFix::LoadSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	const auto path = GetSettingsPath();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		ini.LoadFile(path.string().c_str());
	}

	settings.EnableLightsVisualisation = ini.GetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	settings.LightsVisualisationMode = static_cast<std::uint32_t>(
		ini.GetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode)));
}

void LightLimitFix::SaveSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	ini.SetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	ini.SetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode));

	const auto path = GetSettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	ini.SaveFile(path.string().c_str());
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

void LightLimitFix::DrawSettings()
{
	if (ImGui::CollapsingHeader("Light Limit Fix")) {
		int changed = 0;
		changed |= ImGui::Checkbox("Lights Visualisation", &settings.EnableLightsVisualisation) ? 1 : 0;

		const char* modes[] = { "Clusters", "Lights", "Both" };
		int mode = static_cast<int>(settings.LightsVisualisationMode);
		if (ImGui::Combo("Visualisation Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
			settings.LightsVisualisationMode = static_cast<std::uint32_t>(std::clamp(mode, 0, IM_ARRAYSIZE(modes) - 1));
			changed = 1;
		}

		ImGui::Text("Lights: %u", currentLightCount);
		ImGui::Text("Strict lights: %u", currentStrictLightCount);
		ImGui::Text("Clusters: %ux%ux%u", clusterSize[0], clusterSize[1], clusterSize[2]);

		if (changed) {
			SaveSettings();
		}
	}
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.CameraNear = CameraNear;
	perFrame.CameraFar = CameraFar;
	perFrame.ClusterSize[0] = clusterSize[0];
	perFrame.ClusterSize[1] = clusterSize[1];
	perFrame.ClusterSize[2] = clusterSize[2];
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
	if (!device) {
		logger::warn("[LightLimitFix] SetupResources: D3D11 device not available");
		return;
	}
#if defined(FALLOUT_PRE_NG)
	clusterBuildCacheValid = false;
	clusterBuildCache = {};
	clusterPayloadCacheValid = false;
	clusterPayloadCache = {};
	shadowSceneFastReuseValid = false;
	shadowSceneFastReuse = {};
#endif

	// com_ptr auto-releases previous resources on reassignment — no manual ClearShaderCache needed

	auto shaderPath = GetShaderPath();
#if defined(FALLOUT_PRE_NG)
	RunPreNGDFLightContractProbeCompileDiagnostic();
	RunPreNGDFLightFullShadowedCandidateCompileDiagnostic();
#endif

	auto compileOrLoad = [&](const char* a_name, winrt::com_ptr<ID3D11ComputeShader>& a_out) {
		auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
			shaderPath + a_name);
		if (!compiled) {
			logger::warn("[LightLimitFix] Failed to compile: {}{}", shaderPath, a_name);
			return false;
		}
		const auto hr = device->CreateComputeShader(compiled->data(), compiled->size(), nullptr, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	if (!compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS) ||
	    !compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS)) {
		logger::warn("[LightLimitFix] GPU resources pending - compute shaders not available");
		return;
	}

	auto createBuffer = [&](const char* a_name, const D3D11_BUFFER_DESC& a_desc,
	                        winrt::com_ptr<ID3D11Buffer>& a_out) {
		const auto hr = device->CreateBuffer(&a_desc, nullptr, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	auto createSRV = [&](const char* a_name, ID3D11Resource* a_resource,
	                     const D3D11_SHADER_RESOURCE_VIEW_DESC& a_desc,
	                     winrt::com_ptr<ID3D11ShaderResourceView>& a_out) {
		const auto hr = device->CreateShaderResourceView(a_resource, &a_desc, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	auto createUAV = [&](const char* a_name, ID3D11Resource* a_resource,
	                     const D3D11_UNORDERED_ACCESS_VIEW_DESC& a_desc,
	                     winrt::com_ptr<ID3D11UnorderedAccessView>& a_out) {
		const auto hr = device->CreateUnorderedAccessView(a_resource, &a_desc, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	// Constant buffers
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightBuildingCB);
		if (!createBuffer("CreateBuffer(lightBuildingCB)", desc, lightBuildingCB)) return;
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightCullingCB);
		if (!createBuffer("CreateBuffer(lightCullingCB)", desc, lightCullingCB)) return;
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(StrictLightDataCB);
		if (!createBuffer("CreateBuffer(strictLightDataCB)", desc, strictLightDataCB)) {
			logger::warn("[LightLimitFix] Strict light diagnostic CB unavailable; continuing without it");
		}
	}

	// Lights structured buffer
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightData);
		desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
		if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = kMaxLights;
		if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffer.get(), srvDesc, lightsSRV)) return;
	}

	// Clusters structured buffer
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(ClusterAABB);
		desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(ClusterAABB));
		if (!createBuffer("CreateBuffer(clustersBuffer)", desc, clustersBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		if (!createSRV("CreateShaderResourceView(clustersSRV)", clustersBuffer.get(), srvDesc, clustersSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		if (!createUAV("CreateUnorderedAccessView(clustersUAV)", clustersBuffer.get(), uavDesc, clustersUAV)) return;
	}

	// Light index counter
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = sizeof(std::uint32_t);
		if (!createBuffer("CreateBuffer(lightIndexCounterBuffer)", desc, lightIndexCounterBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = 1;
		if (!createSRV("CreateShaderResourceView(lightIndexCounterSRV)", lightIndexCounterBuffer.get(), srvDesc, lightIndexCounterSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = 1;
		if (!createUAV("CreateUnorderedAccessView(lightIndexCounterUAV)", lightIndexCounterBuffer.get(), uavDesc, lightIndexCounterUAV)) return;
	}

	// Light index list
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = static_cast<UINT>(clusterCount * kClusterMaxLights * sizeof(std::uint32_t));
		if (!createBuffer("CreateBuffer(lightIndexListBuffer)", desc, lightIndexListBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		if (!createSRV("CreateShaderResourceView(lightIndexListSRV)", lightIndexListBuffer.get(), srvDesc, lightIndexListSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		if (!createUAV("CreateUnorderedAccessView(lightIndexListUAV)", lightIndexListBuffer.get(), uavDesc, lightIndexListUAV)) return;
	}

	// Light grid
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightGrid);
		desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(LightGrid));
		if (!createBuffer("CreateBuffer(lightGridBuffer)", desc, lightGridBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		if (!createSRV("CreateShaderResourceView(lightGridSRV)", lightGridBuffer.get(), srvDesc, lightGridSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		if (!createUAV("CreateUnorderedAccessView(lightGridUAV)", lightGridBuffer.get(), uavDesc, lightGridUAV)) return;
	}

	if (!HasResources()) {
		logger::error("[LightLimitFix] GPU resource creation finished with incomplete resources");
		return;
	}

	logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
	             clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_AE)
	auto* setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
	if (setting) {
		setting->SetInt(0x7FFFFFFF);
		logger::info("[LightLimitFix] Unlocked magic light limit");
	}
#endif
}

void LightLimitFix::PostPostLoad()
{
#if defined(FALLOUT_PRE_NG)
	LogPreNGDiagnosticEnvironmentSnapshot();
	const auto pointLightHookState = PreparePreNGPointLightHook();
	const auto setupGeometryHookState = ReadEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv);
	const auto bsLightingSetupGeometryResourceBindState = ReadEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
	const bool setupGeometryRequested =
		setupGeometryHookState.enabled ||
		bsLightingSetupGeometryResourceBindState.enabled;
	const bool setupGeometryAllowed = CanInstallPreNGSetupGeometryHooks(pointLightHookState);
	if (setupGeometryRequested && setupGeometryAllowed) {
		Hooks::Install(false);
		logger::info(
			"[LightLimitFix] PreNG BSLightingShader SetupGeometry hook installed setupGeometryHookEnv={} setupGeometryHook={} source={} bsLightingSetupGeometryResourceBindEnv={} bsLightingResourceHook={} source={} pointLightState={}; scene-light decoder active with frame budget {}, shader binding still uses explicit b3/t35-t37 gates",
			kPreNGSetupGeometryHookOptInEnv,
			setupGeometryHookState.enabled ? "on" : "off",
			EnvironmentSwitchSourceName(setupGeometryHookState.source),
			kPreNGBSLightingSetupGeometryResourceBindEnv,
			bsLightingSetupGeometryResourceBindState.enabled ? "on" : "off",
			EnvironmentSwitchSourceName(bsLightingSetupGeometryResourceBindState.source),
			PreNGPointLightHookStateName(pointLightHookState),
			GetPreNGSetupGeometryFrameBudget());
		return;
	}

	if (setupGeometryRequested && !setupGeometryAllowed) {
		logger::warn(
			"[LightLimitFix] PreNG SetupGeometry hooks held despite {}=1 source={} pointLightState={}; callsite evidence is not trusted",
			kPreNGSetupGeometryHookOptInEnv,
			EnvironmentSwitchSourceName(setupGeometryHookState.source),
			PreNGPointLightHookStateName(pointLightHookState));
	} else {
		switch (pointLightHookState) {
		case PreNGPointLightHookState::Installed:
			logger::warn(
				"[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook diagnostic active and patch verified; set {}=1 for controlled SetupGeometry evidence",
				kPreNGSetupGeometryHookOptInEnv);
			break;
		case PreNGPointLightHookState::InstalledUnverified:
			logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook patch unverified; diagnostic evidence is not trusted");
			break;
		case PreNGPointLightHookState::Prepared:
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook remains gated; set {}=1 for controlled SetupGeometry evidence",
				kPreNGSetupGeometryHookOptInEnv);
			break;
		case PreNGPointLightHookState::Failed:
			logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook not prepared");
			break;
		}
	}
	return;
#else
	Hooks::Install();
#endif
}

void LightLimitFix::Prepass()
{
	const auto frameNumber = ++diagFrameCounter;

#if defined(FALLOUT_PRE_NG)
	auto* runtime = CommunityShaders::Runtime::GetSingleton();
	if (!runtime) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: runtime unavailable");
		}
		return;
	}
	if (runtime->GetFrameCount() < kPreNGStableFrame) {
		if (frameNumber == 1) {
			logger::info("[LightLimitFix] PreNG Prepass waiting for stable frame gate ({})", kPreNGStableFrame);
		}
		return;
	}
	LogPreNGHookReachabilityWatchdog(runtime->GetFrameCount());
#endif

	if (!HasResources()) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: GPU resources are incomplete");
		}
		return;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: renderer data unavailable");
		}
		return;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: D3D11 context unavailable");
		}
		return;
	}

	auto clearComputeBindings = [&] {
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[3]{};
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
		ID3D11Buffer* nullCB = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	};
	auto clearPixelClusterSRVs = [&] {
		ID3D11ShaderResourceView* nullSRVs[3]{};
		context->PSSetShaderResources(35, ARRAYSIZE(nullSRVs), nullSRVs);
	};
	auto clearPixelLLFBindings = [&] {
		clearPixelClusterSRVs();
		ID3D11Buffer* nullCB = nullptr;
		context->PSSetConstantBuffers(3, 1, &nullCB);
	};

#if defined(FALLOUT_PRE_NG)
	const bool persistentPrepassActive =
		ShouldPersistPreNGClusterPrepass() &&
		ShouldUsePreNGPersistentClusterPrepassConsumer();
	if (!ShouldRunPreNGClusterPrepassProof()) {
		clearPixelLLFBindings();
		seenLights.clear();
		seenThisPass.clear();
		seenCBHashes.clear();
		frameLights.clear();
		if (ShouldHoldPreNGDFLightPreparedState() && currentLightCount > 0) {
			static std::atomic_uint32_t holdCount = 0;
			const auto holdIndex = ++holdCount;
			if (holdIndex <= 8 || holdIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG clustered Prepass prepared state retained for DFLight proof pass holds={} lights={} strict={} shadowMask=0x{:08X}",
					holdIndex,
					currentLightCount,
					currentStrictLightCount,
					strictLightDataTemp.ShadowBitMask);
			}
		} else {
			currentLightCount = 0;
			currentStrictLightCount = 0;
			strictLightDataTemp = {};
			clusterPayloadCacheValid = false;
			clusterPayloadCache = {};
			shadowSceneFastReuseValid = false;
			shadowSceneFastReuse = {};
		}
		return;
	}
#endif

#if defined(FALLOUT_PRE_NG)
	if (persistentPrepassActive &&
		ShouldReusePreNGClusterPrepassPayload() &&
		clusterPayloadCacheValid &&
		currentLightCount > 0) {
		const auto refreshInterval = GetPreNGPersistentClusterPrepassRefreshInterval();
		static std::atomic_uint32_t persistentPrepassFrameCount = 0;
		const auto persistentFrame = persistentPrepassFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (refreshInterval == 0 || (refreshInterval > 1 && persistentFrame % refreshInterval != 0)) {
			seenLights.clear();
			seenThisPass.clear();
			seenCBHashes.clear();
			frameLights.clear();
			currentLightCount = clusterPayloadCache.LightCount;
			currentStrictLightCount = clusterPayloadCache.StrictLightCount;
			strictLightDataTemp.NumStrictLights = clusterPayloadCache.StrictLightCount;
			strictLightDataTemp.ShadowBitMask = clusterPayloadCache.ShadowBitMask;

			clearPixelLLFBindings();
			if (ShouldBindPreNGPrepassResources()) {
				static bool loggedPersistentPrepassPixelBindingHold = false;
				if (!loggedPersistentPrepassPixelBindingHold) {
					logger::info(
						"[LightLimitFix] PreNG persistent clustered Prepass pixel bindings held; cached b3/t35-t37 payload remains available, but PS slots are cleared until an on-demand visible consumer binds them");
					loggedPersistentPrepassPixelBindingHold = true;
				}
			}

			static std::atomic_uint32_t throttledReuseCount = 0;
			const auto reuseIndex = ++throttledReuseCount;
			if (reuseIndex <= 8 || reuseIndex % 128 == 0) {
				logger::info(
					"[LightLimitFix] PreNG persistent clustered Prepass CPU-throttled reuse reuses={} frame={} persistentFrame={} refreshInterval={} lights={} strict={} clusters={} shadowMask=0x{:08X} strictCBUploaded={}",
					reuseIndex,
					frameNumber,
					persistentFrame,
					refreshInterval,
					currentLightCount,
					currentStrictLightCount,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					strictLightDataTemp.ShadowBitMask,
					clusterPayloadCache.StrictCBUploaded);
			}
			return;
		}
	}
#endif

	const auto& gState = RE::BSGraphics::State::GetSingleton();
	const auto& camView = gState.cameraState.camViewData;

	DirectX::XMFLOAT4X4 projInvTransposed;
	{
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.projMat));
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
	}
	if (!IsFiniteMatrix(projInvTransposed)) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: camera projection matrix is not invertible");
		}
		return;
	}

	DirectX::XMFLOAT4X4 viewTransposed;
	{
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
	}
	if (!IsFiniteMatrix(viewTransposed)) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: camera view matrix is invalid");
		}
		return;
	}

#if defined(FALLOUT_PRE_NG)
	std::vector<LightData> preNGSceneLightFallback;
	preNGSceneLightFallback.swap(frameLights);
	const auto preNGSceneLightFallbackStrictData = strictLightDataTemp;
	const auto preNGSceneLightFallbackStrictCount = currentStrictLightCount;

	seenLights.clear();
	seenThisPass.clear();

	if (CollectLightsFromPreNGShadowScene() == 0) {
		if (!preNGSceneLightFallback.empty()) {
			frameLights.swap(preNGSceneLightFallback);
			strictLightDataTemp = preNGSceneLightFallbackStrictData;
			currentStrictLightCount = preNGSceneLightFallbackStrictCount;

			static std::atomic_uint32_t fallbackUseCount = 0;
			const auto fallbackIndex = ++fallbackUseCount;
			if (fallbackIndex <= 8 || fallbackIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG scene-light fallback feeds clustered prepass uses={} lights={} strict={} shadowMask=0x{:08X}",
					fallbackIndex,
					static_cast<std::uint32_t>(frameLights.size()),
					currentStrictLightCount,
					strictLightDataTemp.ShadowBitMask);
			}
		} else {
			CollectLightsFromScene();
		}
	}
#else
	if (!seenLights.empty()) {
		CollectLightsFromBSLight();
	} else {
		CollectLightsFromScene();
	}
#endif

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());
#if defined(FALLOUT_PRE_NG)
	// Dense-bucket guard: skip the clustered prepass entirely when the decoded
	// light set is huge (fullscreen preview scenes like ExamineMenu latch onto
	// the world ShadowSceneNode with ~1000+ lights). The per-frame 1024-cluster
	// cull over that many lights collapses framerate, and visible LLF is not
	// needed for those frames. Mirrors the clearPixelLLFBindings/clear pattern of
	// the proof-window-complete path so no stale clustered SRVs/CB linger.
	if (currentLightCount > kPreNGClusterPrepassMaxLights) {
		clearPixelLLFBindings();
		seenLights.clear();
		seenThisPass.clear();
		seenCBHashes.clear();
		frameLights.clear();
		clusterPayloadCacheValid = false;
		static std::atomic_uint32_t denseSkipCount = 0;
		const auto skipIndex = ++denseSkipCount;
		if (skipIndex <= 8 || skipIndex % 128 == 0) {
			logger::info(
				"[LightLimitFix] PreNG clustered Prepass skipped for dense light bucket skips={} frame={} lights={} threshold={}; per-frame 1024-cluster cull over this many lights collapses framerate (e.g. ExamineMenu world-node latch), visible LLF not needed here",
				skipIndex,
				frameNumber,
				currentLightCount,
				kPreNGClusterPrepassMaxLights);
		}
		currentLightCount = 0;
		return;
	}

	const auto preNGClusterPayloadCurrent = MakePreNGClusterPayloadCacheState(
		frameLights,
		strictLightDataTemp,
		currentLightCount,
		currentStrictLightCount,
		viewTransposed,
		CameraNear,
		CameraFar,
		clusterSize);
	const bool preNGClusterPayloadReused =
		ShouldReusePreNGClusterPrepassPayload() &&
		clusterPayloadCacheValid &&
		clusterBuildCacheValid &&
		PreNGClusterPayloadInputsMatch(clusterPayloadCache, preNGClusterPayloadCurrent);
	if (preNGClusterPayloadReused) {
		static std::atomic_uint32_t payloadReuseCount = 0;
		const auto reuseIndex = ++payloadReuseCount;
		if (reuseIndex <= 8 || reuseIndex % 128 == 0) {
			logger::info(
				"[LightLimitFix] PreNG clustered Prepass reused LLF payload reuses={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X} lightsHash=0x{:016X} viewHash=0x{:016X}",
				reuseIndex,
				frameNumber,
				currentLightCount,
				currentStrictLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask,
				preNGClusterPayloadCurrent.LightsHash,
				preNGClusterPayloadCurrent.ViewHash);
		}
	}
#endif

	if (frameNumber % 300 == 0) {
		logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}",
		             frameNumber, currentLightCount,
		             clusterSize[0], clusterSize[1], clusterSize[2],
		             CameraNear, CameraFar);
	}

	seenLights.clear();
	seenCBHashes.clear();

#if defined(FALLOUT_PRE_NG)
	if (!preNGClusterPayloadReused)
#endif
	{
		clearPixelClusterSRVs();

		if (currentLightCount > 0) {
			const auto lightUploadBytes = static_cast<UINT>(currentLightCount * sizeof(LightData));
			D3D11_BOX lightUploadBox{};
			lightUploadBox.right = lightUploadBytes;
			lightUploadBox.bottom = 1;
			lightUploadBox.back = 1;
			context->UpdateSubresource(lightsBuffer.get(), 0, &lightUploadBox, frameLights.data(), lightUploadBytes, 0);
		}

#if defined(FALLOUT_PRE_NG)
		if (currentLightCount > 0) {
			static std::atomic_uint32_t nonZeroClusterUploadCount = 0;
			const auto uploadIndex = ++nonZeroClusterUploadCount;
			if (uploadIndex <= 8 || uploadIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG clustered prepass uploaded uploads={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
					uploadIndex,
					frameNumber,
					currentLightCount,
					currentStrictLightCount,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					strictLightDataTemp.ShadowBitMask);
			}
		}
#endif

		const UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounterUAV.get(), counterReset);

		LightBuildingCB buildingCBData{};
		buildingCBData.LightsNear = CameraNear;
		buildingCBData.LightsFar = CameraFar;
		buildingCBData.pad0[0] = buildingCBData.pad0[1] = 0;
		buildingCBData.ClusterSize[0] = clusterSize[0];
		buildingCBData.ClusterSize[1] = clusterSize[1];
		buildingCBData.ClusterSize[2] = clusterSize[2];
		buildingCBData.ClusterSize[3] = 0;
		std::memcpy(&buildingCBData.CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));

		bool rebuildClusterAABBs = true;
#if defined(FALLOUT_PRE_NG)
		rebuildClusterAABBs =
			!clusterBuildCacheValid ||
			!PreNGClusterBuildInputsMatch(clusterBuildCache, buildingCBData);
#endif

		if (rebuildClusterAABBs) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			const auto hr = context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			if (FAILED(hr)) {
				LogResourceFailure("Map(lightBuildingCB)", hr);
				clearComputeBindings();
				return;
			}
			std::memcpy(mapped.pData, &buildingCBData, sizeof(buildingCBData));
			context->Unmap(lightBuildingCB.get(), 0);

			context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
			ID3D11Buffer* cbPtr = lightBuildingCB.get();
			context->CSSetConstantBuffers(0, 1, &cbPtr);
			ID3D11UnorderedAccessView* buildingUAVs[] = { clustersUAV.get() };
			context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
			context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
#if defined(FALLOUT_PRE_NG)
			clusterBuildCache.LightsNear = buildingCBData.LightsNear;
			clusterBuildCache.LightsFar = buildingCBData.LightsFar;
			for (std::uint32_t i = 0; i < 4; ++i) {
				clusterBuildCache.ClusterSize[i] = buildingCBData.ClusterSize[i];
			}
			clusterBuildCacheValid = true;

			static std::atomic_uint32_t clusterBuildRebuildCount = 0;
			const auto rebuildIndex = ++clusterBuildRebuildCount;
			if (rebuildIndex <= 8 || rebuildIndex % 128 == 0) {
				logger::info(
					"[LightLimitFix] PreNG clustered Prepass rebuilt cluster AABBs rebuilds={} frame={} clusters={} near={:.3f} far={:.1f}",
					rebuildIndex,
					frameNumber,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					CameraNear,
					CameraFar);
			}
#endif
			clearComputeBindings();
		}
#if defined(FALLOUT_PRE_NG)
		else {
			static std::atomic_uint32_t clusterBuildReuseCount = 0;
			const auto reuseIndex = ++clusterBuildReuseCount;
			if (reuseIndex <= 8 || reuseIndex % 128 == 0) {
				logger::info(
					"[LightLimitFix] PreNG clustered Prepass reused cluster AABBs reuses={} frame={} clusters={} stableKey=near/far/cluster-size tolerance={}",
					reuseIndex,
					frameNumber,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					kPreNGClusterBuildReuseTolerance);
			}
		}
#endif

		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			const auto hr = context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			if (FAILED(hr)) {
				LogResourceFailure("Map(lightCullingCB)", hr);
				clearComputeBindings();
				return;
			}
			auto* cb = static_cast<LightCullingCB*>(mapped.pData);
			cb->LightCount = currentLightCount;
			cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
			cb->ClusterSize[0] = clusterSize[0];
			cb->ClusterSize[1] = clusterSize[1];
			cb->ClusterSize[2] = clusterSize[2];
			cb->ClusterSize[3] = 0;
			std::memcpy(&cb->CameraView, &viewTransposed, sizeof(viewTransposed));
			context->Unmap(lightCullingCB.get(), 0);

			ID3D11ShaderResourceView* cullingSRVs[] = { clustersSRV.get(), lightsSRV.get() };
			context->CSSetShaderResources(0, 2, cullingSRVs);

			ID3D11UnorderedAccessView* cullingUAVs[] = { lightIndexCounterUAV.get(), lightIndexListUAV.get(), lightGridUAV.get() };
			context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

			context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
			ID3D11Buffer* cullCBPtr = lightCullingCB.get();
			context->CSSetConstantBuffers(0, 1, &cullCBPtr);

			context->Dispatch(
				(clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
				(clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
				(clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
		}

		clearComputeBindings();

#if defined(FALLOUT_PRE_NG)
		clusterPayloadCache = preNGClusterPayloadCurrent;
		clusterPayloadCache.StrictCBUploaded = false;
		clusterPayloadCacheValid = true;
#endif
	}
	frameLights.clear();

#if defined(FALLOUT_PRE_NG)
	if (ShouldBindPreNGPrepassResources()) {
		bool prepassStrictCBUploaded =
			preNGClusterPayloadReused &&
			clusterPayloadCacheValid &&
			clusterPayloadCache.StrictCBUploaded;
		if (!prepassStrictCBUploaded && ShouldBindPreNGStrictLightCB()) {
			prepassStrictCBUploaded = persistentPrepassActive ?
				UpdatePreNGStrictLightDataCB(context) :
				UploadPreNGStrictLightDataDiagnostic();
		}
		const bool prepassStrictCBBound = persistentPrepassActive ?
			false :
			BindPreNGStrictLightDataCBToPixelShader(nullptr, currentLightCount, prepassStrictCBUploaded);
		if ((prepassStrictCBBound || prepassStrictCBUploaded) && clusterPayloadCacheValid) {
			clusterPayloadCache.StrictCBUploaded = true;
		}
		if (prepassStrictCBBound) {
			static std::atomic_uint32_t prepassStrictCBBindCount = 0;
			const auto prepassStrictCBBindIndex = ++prepassStrictCBBindCount;
			if (prepassStrictCBBindIndex <= 8 || prepassStrictCBBindIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG strict-light CB bound to PS b3 from Prepass (Skyrim parity) binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X} uploadedBeforeBind={}",
					prepassStrictCBBindIndex,
					frameNumber,
					currentLightCount,
					currentStrictLightCount,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					strictLightDataTemp.ShadowBitMask,
					prepassStrictCBUploaded);
			}
		}
		if (ShouldBindPreNGClusterSRVs() && !persistentPrepassActive) {
			ID3D11ShaderResourceView* views[3]{
				lightsSRV.get(),
				lightIndexListSRV.get(),
				lightGridSRV.get()
			};
			context->PSSetShaderResources(35, ARRAYSIZE(views), views);

			static std::atomic_uint32_t prepassBindCount = 0;
			const auto prepassBindIndex = ++prepassBindCount;
			if (prepassBindIndex <= 8 || prepassBindIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 from Prepass (Skyrim parity) binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
					prepassBindIndex,
					frameNumber,
					currentLightCount,
					currentStrictLightCount,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					strictLightDataTemp.ShadowBitMask);
			}

			if (currentLightCount > 0) {
				static std::atomic_uint32_t nonZeroPrepassBindCount = 0;
				const auto nonZeroPrepassBindIndex = ++nonZeroPrepassBindCount;
				if (nonZeroPrepassBindIndex <= 8 || nonZeroPrepassBindIndex % 512 == 0) {
					logger::info(
						"[LightLimitFix] PreNG cluster SRVs Prepass nonzero bind proof nonzeroBinds={} binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
						nonZeroPrepassBindIndex,
						prepassBindIndex,
						frameNumber,
						currentLightCount,
						currentStrictLightCount,
						clusterSize[0] * clusterSize[1] * clusterSize[2],
						strictLightDataTemp.ShadowBitMask);
				}
			}
		} else if (persistentPrepassActive) {
			clearPixelLLFBindings();
			static bool loggedPersistentPrepassPixelBindingHold = false;
			if (!loggedPersistentPrepassPixelBindingHold) {
				logger::info(
					"[LightLimitFix] PreNG persistent clustered Prepass pixel bindings held after upload; cached b3/t35-t37 payload remains available, but PS slots are cleared until an on-demand visible consumer binds them");
				loggedPersistentPrepassPixelBindingHold = true;
			}
		} else {
			static bool loggedPreNGBindingHold = false;
			if (!loggedPreNGBindingHold) {
				logger::info("[LightLimitFix] PreNG compute Prepass active; Skyrim-style PS t35-t37 Prepass binding is gated by FO4CS_LLF_PRENG_BIND_CLUSTER_SRVS, strict CB b3 Prepass binding is gated by FO4CS_LLF_PRENG_BIND_STRICT_CB");
				loggedPreNGBindingHold = true;
			}
		}
	} else {
		static bool loggedPreNGPrepassResourceBindHold = false;
		if (!loggedPreNGPrepassResourceBindHold) {
			logger::info(
				"[LightLimitFix] PreNG Prepass resource binding held by {}; descriptor-owned DFlight/DFComposite/BSLighting consumers bind b3/t35-t37 on demand",
				kPreNGPrepassResourceBindEnv);
			loggedPreNGPrepassResourceBindHold = true;
		}
		TryBindPreNGBSLightingDeferredDescriptorResources(*this);
	}
#else
	if (frameNumber >= 3 && currentLightCount > 0) {
		ID3D11ShaderResourceView* views[3]{
			lightsSRV.get(),
			lightIndexListSRV.get(),
			lightGridSRV.get()
		};
		context->PSSetShaderResources(35, ARRAYSIZE(views), views);

		if (frameNumber % 300 == 0) {
			logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)",
			             currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
		}
	}
#endif
}

bool LightLimitFix::HasResources() const
{
	return clusterBuildingCS &&
	       clusterCullingCS &&
	       lightBuildingCB &&
	       lightCullingCB &&
	       lightsBuffer &&
	       lightsSRV &&
	       clustersBuffer &&
	       clustersSRV &&
	       clustersUAV &&
	       lightIndexCounterBuffer &&
	       lightIndexCounterSRV &&
	       lightIndexCounterUAV &&
	       lightIndexListBuffer &&
	       lightIndexListSRV &&
	       lightIndexListUAV &&
	       lightGridBuffer &&
	       lightGridSRV &&
	       lightGridUAV;
}

#if defined(FALLOUT_PRE_NG)
bool LightLimitFix::HasPreNGDFLightDescriptorConsumerData() const
{
	return HasResources() &&
	       strictLightDataCB &&
	       currentLightCount > 0 &&
	       currentStrictLightCount > 0;
}

bool LightLimitFix::HasPreNGDFCompositeDescriptorConsumerData() const
{
	return HasResources() &&
	       strictLightDataCB &&
	       currentLightCount > 0;
}

bool LightLimitFix::HasPreNGBSLightingDescriptorConsumerData() const
{
	return HasResources() &&
	       strictLightDataCB &&
	       currentLightCount > 0;
}

void LightLimitFix::NotifyPreNGDFLightLLFConsumerDescriptorObserved(
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_found,
	std::uintptr_t a_pixelShader)
{
	s_preNGDFLightLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);

	const auto observation = ++s_preNGDFLightLLFConsumerDescriptorObservations;
	if (observation <= 8 || observation % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFLight LLF consumer descriptor observed observations={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} ownedPS=0x{:X}; clustered Prepass demand can start on the next frame while resource sub-gates remain enabled",
			observation,
			a_vertexDescriptor,
			a_pixelDescriptor,
			a_found,
			a_pixelShader);
	}
}

bool LightLimitFix::HasPreNGDFLightLLFConsumerDescriptorObserved() const
{
	return s_preNGDFLightLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}

void LightLimitFix::NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_found,
	std::uintptr_t a_vanillaPixelShader,
	std::uintptr_t a_ownedPixelShader)
{
	s_preNGDFCompositeLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);

	const auto observation = ++s_preNGDFCompositeLLFConsumerDescriptorObservations;
	if (observation <= 8 || observation % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFComposite LLF consumer descriptor observed observations={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X} ownedPS=0x{:X}; clustered Prepass demand can start on the next frame while resource sub-gates remain enabled",
			observation,
			a_vertexDescriptor,
			a_pixelDescriptor,
			a_found,
			a_vanillaPixelShader,
			a_ownedPixelShader);
	}
}

bool LightLimitFix::HasPreNGDFCompositeLLFConsumerDescriptorObserved() const
{
	return s_preNGDFCompositeLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}

void LightLimitFix::NotifyPreNGBSLightingLLFConsumerDescriptorObserved(
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_found,
	std::uintptr_t a_vanillaPixelShader)
{
	s_preNGBSLightingLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);
	s_preNGBSLightingLLFConsumerLastVertexDescriptor.store(a_vertexDescriptor, std::memory_order_relaxed);
	s_preNGBSLightingLLFConsumerLastPixelDescriptor.store(a_pixelDescriptor, std::memory_order_relaxed);
	s_preNGBSLightingLLFConsumerLastFound.store(a_found, std::memory_order_relaxed);
	s_preNGBSLightingLLFConsumerLastVanillaPixelShader.store(a_vanillaPixelShader, std::memory_order_relaxed);
	ShouldDeferPreNGBSLightingResourceProofForMenu();
	ExtendPreNGBSLightingResourceProofDescriptorSettle();

	const auto observation = ++s_preNGBSLightingLLFConsumerDescriptorObservations;
	if (observation <= 8 || observation % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG BSLighting LLF resource descriptor observed observations={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X}; finite clustered Prepass demand can start after BSLighting descriptor/menu settle while resource sub-gates remain enabled, unless the descriptor path is suppressed after LockpickingMenu",
			observation,
			a_vertexDescriptor,
			a_pixelDescriptor,
			a_found,
			a_vanillaPixelShader);
	}
}

bool LightLimitFix::HasPreNGBSLightingLLFConsumerDescriptorObserved() const
{
	return s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}
#endif

void LightLimitFix::Reset()
{
	if (!HasResources()) return;

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) return;

	ID3D11ShaderResourceView* nullViews[3]{};
	context->PSSetShaderResources(35, 3, nullViews);
}

void LightLimitFix::CollectLightsFromPass(RE::BSRenderPass* a_pass)
{
	if (!a_pass) return;

	auto* shaderProp = *reinterpret_cast<RE::BSShaderProperty**>(reinterpret_cast<std::uintptr_t>(a_pass) + 0x10);
	if (!shaderProp) return;

	auto* fadeNode = *reinterpret_cast<RE::BSFadeNode**>(reinterpret_cast<std::uintptr_t>(shaderProp) + 0x48);
	if (!fadeNode) return;

	auto* lightData = reinterpret_cast<RE::BSShaderPropertyLightData*>(reinterpret_cast<std::uintptr_t>(fadeNode) + 0x140);
	if (lightData->lightList.empty()) return;

	for (auto* light : lightData->lightList) {
		if (!light || seenLights.contains(light)) continue;
		seenLights.insert(light);
		seenThisPass.push_back(light);
	}
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::CollectLightsFromPreNGSceneLights(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, std::uint32_t a_shadowArg)
{
	strictLightDataTemp = {};
	strictLightDataTemp.RoomIndex = -1;
	currentStrictLightCount = 0;

	if (!a_pass) {
		return 0;
	}

	const auto passAddress = reinterpret_cast<std::uintptr_t>(a_pass);
	const F4Runtime::PreNGBSRenderPassView passView{ a_pass };

	std::uintptr_t sceneLightsAddress = 0;
	std::uint8_t rawLightCount = 0;
	if (!passView.ReadSceneLights(sceneLightsAddress) ||
		!passView.ReadRawLightCount(rawLightCount) ||
		sceneLightsAddress == 0 ||
		rawLightCount <= kPreNGBSRenderPassSceneLightFirstIndex) {
		return 0;
	}

	// FO4 vanilla passes (pass->numLights - 1) into the point-light writer and
	// reads physical sceneLights entries starting at index 1.
	auto availableLightCount = static_cast<std::uint32_t>(rawLightCount - kPreNGBSRenderPassSceneLightFirstIndex);
	if (a_requestedLightCount < availableLightCount) {
		availableLightCount = a_requestedLightCount;
	}

	std::uint32_t collected = 0;
	std::uint32_t strictWriteCount = 0;
	std::uint32_t missingEntryCount = 0;
	std::uint32_t missingWrapperDataCount = 0;
	std::uint32_t invalidNiLightDataCount = 0;
	std::uint32_t inactiveLightDataCount = 0;
	std::uint32_t duplicateLightCount = 0;
	std::uint32_t unreadableShadowMaskCount = 0;
	std::uint32_t invalidShadowMaskCount = 0;

	for (std::uint32_t i = 0; i < availableLightCount && (frameLights.size() < kMaxLights || strictWriteCount < kMaxStrictLights); ++i) {
		std::uintptr_t wrapperAddress = 0;
		if (!passView.ReadSceneLightWrapper(i, wrapperAddress) || wrapperAddress == 0) {
			++missingEntryCount;
			continue;
		}

		LightData data{};
		std::uintptr_t niLightAddress = 0;
		bool shadowMaskUnreadable = false;
		bool shadowMaskInvalid = false;
		std::uint32_t shadowMaskBit = 0;
		const auto decodeResult = DecodePreNGBSLightWrapper(
			wrapperAddress,
			data,
			niLightAddress,
			shadowMaskUnreadable,
			shadowMaskInvalid,
			shadowMaskBit);
		if (decodeResult == PreNGLightDecodeResult::MissingWrapperData) {
			++missingWrapperDataCount;
			continue;
		}
		if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData) {
			++invalidNiLightDataCount;
			continue;
		}
		if (decodeResult == PreNGLightDecodeResult::NonContributingLightData) {
			++inactiveLightDataCount;
			continue;
		}

		if (shadowMaskBit != 0) {
			strictLightDataTemp.ShadowBitMask |= shadowMaskBit;
		}
		if (shadowMaskUnreadable) {
			++unreadableShadowMaskCount;
		} else if (shadowMaskInvalid) {
			++invalidShadowMaskCount;
		}

		if (strictWriteCount < kMaxStrictLights) {
			strictLightDataTemp.StrictLights[strictWriteCount++] = data;
		}

		auto* lightKey = reinterpret_cast<RE::BSLight*>(niLightAddress);
		if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey)) {
			++duplicateLightCount;
			continue;
		}

		seenLights.insert(lightKey);
		seenThisPass.push_back(lightKey);
		frameLights.push_back(data);
		++collected;
	}

	strictLightDataTemp.NumStrictLights = strictWriteCount;
	currentStrictLightCount = strictWriteCount;

	static std::atomic_uint32_t decodeDiagCount = 0;
	if (availableLightCount > 0 && (collected > 0 || missingEntryCount > 0 || missingWrapperDataCount > 0 || invalidNiLightDataCount > 0 || inactiveLightDataCount > 0)) {
		const auto diagIndex = ++decodeDiagCount;
		if (diagIndex <= 8 || diagIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG scene-light decode pass=0x{:X} table=0x{:X} raw={} requested={} available={} collected={} strict={} shadowArg={} strictShadowMask=0x{:08X} skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
				passAddress,
				sceneLightsAddress,
				static_cast<std::uint32_t>(rawLightCount),
				a_requestedLightCount,
				availableLightCount,
				collected,
				strictWriteCount,
				a_shadowArg,
				strictLightDataTemp.ShadowBitMask,
				missingEntryCount,
				missingWrapperDataCount,
				invalidNiLightDataCount,
				inactiveLightDataCount,
				duplicateLightCount,
				unreadableShadowMaskCount,
				invalidShadowMaskCount);
		}
	}

	return collected;
}

std::uint32_t LightLimitFix::CollectLightsFromPreNGShadowScene()
{
	strictLightDataTemp = {};
	strictLightDataTemp.RoomIndex = -1;
	currentStrictLightCount = 0;

	std::uintptr_t activeLightsAddress = 0;
	std::uintptr_t activeShadowLightsAddress = 0;
	std::uintptr_t activeExtraLightsAddress = 0;
	std::uint32_t activeLightCount = 0;
	std::uint32_t activeShadowLightCount = 0;
	std::uint32_t activeExtraLightCount = 0;
	const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
	const auto shadowSceneNode = shadowSceneNodeRef.node;

	auto logFailure = [&](const char* a_reason) {
		shadowSceneFastReuseValid = false;
		shadowSceneFastReuse = {};
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG shadow-scene light decode held failures={} reason={} node=0x{:X} selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}) shadow=(ptr=0x{:X}, count={}) extra=(ptr=0x{:X}, count={})",
				failureIndex,
				a_reason,
				shadowSceneNode,
				static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
				static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex),
				shadowSceneNodeRef.currentIndexRead,
				shadowSceneNodeRef.usedFallback,
				activeLightsAddress,
				activeLightCount,
				activeShadowLightsAddress,
				activeShadowLightCount,
				activeExtraLightsAddress,
				activeExtraLightCount);
		}
	};

	if (shadowSceneNode == 0) {
		logFailure("world-shadow-scene-node-unavailable");
		return 0;
	}

	F4Runtime::PreNGShadowSceneBuckets buckets{};
	const F4Runtime::PreNGShadowSceneNodeView shadowSceneView{ shadowSceneNode };
	if (!shadowSceneView.ReadBuckets(buckets)) {
		logFailure("arrays-unreadable");
		return 0;
	}
	activeLightsAddress = buckets.active.entries;
	activeLightCount = buckets.active.count;
	activeShadowLightsAddress = buckets.shadow.entries;
	activeShadowLightCount = buckets.shadow.count;
	activeExtraLightsAddress = buckets.extra.entries;
	activeExtraLightCount = buckets.extra.count;

	if (activeLightCount > kPreNGMaxShadowSceneActiveLights ||
		activeShadowLightCount > kPreNGMaxShadowSceneActiveLights ||
		activeExtraLightCount > kPreNGMaxShadowSceneActiveLights) {
		logFailure("count-out-of-range");
		return 0;
	}

	if ((activeLightCount > 0 && activeLightsAddress == 0) ||
		(activeShadowLightCount > 0 && activeShadowLightsAddress == 0) ||
		(activeExtraLightCount > 0 && activeExtraLightsAddress == 0)) {
		logFailure("array-null");
		return 0;
	}

	const auto decodeActiveLightCount = std::min(activeLightCount, kPreNGMaxShadowSceneDecodeLights);
	const auto decodeActiveShadowLightCount = std::min(activeShadowLightCount, kPreNGMaxShadowSceneDecodeLights);
	const auto decodeActiveExtraLightCount = std::min(activeExtraLightCount, kPreNGMaxShadowSceneDecodeLights);
	const auto totalBucketCount =
		static_cast<std::uint64_t>(activeLightCount) +
		static_cast<std::uint64_t>(activeShadowLightCount) +
		static_cast<std::uint64_t>(activeExtraLightCount);
	const bool shadowSceneDecodeTruncated =
		activeLightCount != decodeActiveLightCount ||
		activeShadowLightCount != decodeActiveShadowLightCount ||
		activeExtraLightCount != decodeActiveExtraLightCount ||
		totalBucketCount > kPreNGMaxShadowSceneDecodeLights;
	if (shadowSceneDecodeTruncated) {
		static std::atomic_uint32_t truncationCount = 0;
		const auto truncationIndex = ++truncationCount;
		if (truncationIndex <= 8 || truncationIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG shadow-scene decode truncating oversized buckets truncations={} active={}=>{} shadow={}=>{} extra={}=>{} total={} decodeCapacity={} hardMax={}; LLF payload remains bounded",
				truncationIndex,
				activeLightCount,
				decodeActiveLightCount,
				activeShadowLightCount,
				decodeActiveShadowLightCount,
				activeExtraLightCount,
				decodeActiveExtraLightCount,
				totalBucketCount,
				kPreNGMaxShadowSceneDecodeLights,
				kPreNGMaxShadowSceneActiveLights);
		}
	}

	ShadowSceneFastReuseKey fastReuseKey{};
	const bool fastReuseEnabled = ShouldReusePreNGShadowSceneFastReuse();
	const auto fastReuseRefreshInterval = fastReuseEnabled ? GetPreNGShadowSceneFastReuseRefreshInterval() : 0;
	bool fastReuseKeyReady = false;
	if (fastReuseEnabled) {
		fastReuseKeyReady = MakePreNGShadowSceneFastReuseKey(shadowSceneNodeRef, buckets, fastReuseKey);
		if (!fastReuseKeyReady) {
			shadowSceneFastReuseValid = false;
			shadowSceneFastReuse = {};
		} else if (shadowSceneFastReuseValid &&
		           SamePreNGShadowSceneFastReuseStructure(shadowSceneFastReuse.Key, fastReuseKey) &&
		           shadowSceneFastReuse.ReuseAge < fastReuseRefreshInterval) {
			frameLights = shadowSceneFastReuse.Lights;
			strictLightDataTemp = shadowSceneFastReuse.StrictData;
			currentStrictLightCount = shadowSceneFastReuse.StrictLightCount;
			++shadowSceneFastReuse.ReuseAge;

			static std::atomic_uint32_t fastReuseCount = 0;
			const auto reuseIndex = ++fastReuseCount;
			if (reuseIndex <= 8 || reuseIndex % 128 == 0) {
				logger::info(
					"[LightLimitFix] PreNG ShadowScene fast-reused decoded lights reuses={} age={} refreshInterval={} node=0x{:X} active=(ptr=0x{:X}, count={}, hash=0x{:016X}) shadow=(ptr=0x{:X}, count={}, hash=0x{:016X}) extra=(ptr=0x{:X}, count={}, hash=0x{:016X}) lights={} strict={} shadowMask=0x{:08X} lightsHash=0x{:016X}",
					reuseIndex,
					shadowSceneFastReuse.ReuseAge,
					fastReuseRefreshInterval,
					shadowSceneFastReuse.Key.Node,
					shadowSceneFastReuse.Key.ActiveEntries,
					shadowSceneFastReuse.Key.ActiveCount,
					shadowSceneFastReuse.Key.ActiveHash,
					shadowSceneFastReuse.Key.ShadowEntries,
					shadowSceneFastReuse.Key.ShadowCount,
					shadowSceneFastReuse.Key.ShadowHash,
					shadowSceneFastReuse.Key.ExtraEntries,
					shadowSceneFastReuse.Key.ExtraCount,
					shadowSceneFastReuse.Key.ExtraHash,
					shadowSceneFastReuse.LightCount,
					shadowSceneFastReuse.StrictLightCount,
					strictLightDataTemp.ShadowBitMask,
					shadowSceneFastReuse.LightsHash);
			}
			return shadowSceneFastReuse.LightCount;
		}
	}

	std::uint32_t collected = 0;
	std::uint32_t strictWriteCount = 0;
	std::uint32_t missingEntryCount = 0;
	std::uint32_t missingWrapperDataCount = 0;
	std::uint32_t invalidNiLightDataCount = 0;
	std::uint32_t inactiveLightDataCount = 0;
	std::uint32_t duplicateLightCount = 0;
	std::uint32_t unreadableShadowMaskCount = 0;
	std::uint32_t invalidShadowMaskCount = 0;

	auto decodeArray = [&](std::uintptr_t a_arrayAddress, std::uint32_t a_count) {
		for (std::uint32_t i = 0; i < a_count && (frameLights.size() < kMaxLights || strictWriteCount < kMaxStrictLights); ++i) {
			std::uintptr_t wrapperAddress = 0;
			const auto entryAddress = a_arrayAddress + (i * sizeof(std::uintptr_t));
			if (!ReadPreNGValue(entryAddress, wrapperAddress) || wrapperAddress == 0) {
				++missingEntryCount;
				continue;
			}

			LightData data{};
			std::uintptr_t niLightAddress = 0;
			bool shadowMaskUnreadable = false;
			bool shadowMaskInvalid = false;
			std::uint32_t shadowMaskBit = 0;
			const auto decodeResult = DecodePreNGBSLightWrapper(
				wrapperAddress,
				data,
				niLightAddress,
				shadowMaskUnreadable,
				shadowMaskInvalid,
				shadowMaskBit);
			if (decodeResult == PreNGLightDecodeResult::MissingWrapperData) {
				++missingWrapperDataCount;
				continue;
			}
			if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData) {
				++invalidNiLightDataCount;
				continue;
			}
			if (decodeResult == PreNGLightDecodeResult::NonContributingLightData) {
				++inactiveLightDataCount;
				continue;
			}

			if (shadowMaskBit != 0) {
				strictLightDataTemp.ShadowBitMask |= shadowMaskBit;
			}
			if (shadowMaskUnreadable) {
				++unreadableShadowMaskCount;
			} else if (shadowMaskInvalid) {
				++invalidShadowMaskCount;
			}

			if (strictWriteCount < kMaxStrictLights) {
				strictLightDataTemp.StrictLights[strictWriteCount++] = data;
			}

			auto* lightKey = reinterpret_cast<RE::BSLight*>(niLightAddress);
			if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey)) {
				++duplicateLightCount;
				continue;
			}

			seenLights.insert(lightKey);
			seenThisPass.push_back(lightKey);
			frameLights.push_back(data);
			++collected;
		}
	};

	decodeArray(activeLightsAddress, decodeActiveLightCount);
	decodeArray(activeShadowLightsAddress, decodeActiveShadowLightCount);
	decodeArray(activeExtraLightsAddress, decodeActiveExtraLightCount);

	strictLightDataTemp.NumStrictLights = strictWriteCount;
	currentStrictLightCount = strictWriteCount;

	static std::atomic_uint32_t decodeDiagCount = 0;
	const auto diagIndex = ++decodeDiagCount;
	if (diagIndex <= 8 || diagIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG shadow-scene light decode node=0x{:X} selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}, decode={}) shadow=(ptr=0x{:X}, count={}, decode={}) extra=(ptr=0x{:X}, count={}, decode={}) truncated={} collected={} strict={} shadowMask=0x{:08X} skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
			shadowSceneNode,
			static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
			static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex),
			shadowSceneNodeRef.currentIndexRead,
			shadowSceneNodeRef.usedFallback,
			activeLightsAddress,
			activeLightCount,
			decodeActiveLightCount,
			activeShadowLightsAddress,
			activeShadowLightCount,
			decodeActiveShadowLightCount,
			activeExtraLightsAddress,
			activeExtraLightCount,
			decodeActiveExtraLightCount,
			shadowSceneDecodeTruncated,
			collected,
			strictWriteCount,
			strictLightDataTemp.ShadowBitMask,
			missingEntryCount,
			missingWrapperDataCount,
			invalidNiLightDataCount,
			inactiveLightDataCount,
			duplicateLightCount,
			unreadableShadowMaskCount,
			invalidShadowMaskCount);
	}

	if (fastReuseEnabled) {
		const auto cachedLightCount = static_cast<std::uint32_t>(frameLights.size());
		if (fastReuseKeyReady && cachedLightCount > 0) {
			const auto lightsHash = frameLights.empty() ?
				kPreNGFNVOffsetBasis :
				HashPreNGBytes(frameLights.data(), frameLights.size() * sizeof(LightData));
			const auto strictHash = HashPreNGBytes(&strictLightDataTemp, sizeof(strictLightDataTemp));
			const bool stableWithPrevious =
				shadowSceneFastReuseValid &&
				SamePreNGShadowSceneFastReuseKey(shadowSceneFastReuse.Key, fastReuseKey) &&
				shadowSceneFastReuse.LightCount == cachedLightCount &&
				shadowSceneFastReuse.StrictLightCount == currentStrictLightCount &&
				shadowSceneFastReuse.LightsHash == lightsHash &&
				shadowSceneFastReuse.StrictHash == strictHash;

			auto stableDecodeCount = stableWithPrevious ? shadowSceneFastReuse.StableDecodeCount : 0u;
			if (stableDecodeCount != 0xFFFFFFFFu) {
				++stableDecodeCount;
			}

			shadowSceneFastReuse.Key = fastReuseKey;
			shadowSceneFastReuse.Lights = frameLights;
			shadowSceneFastReuse.StrictData = strictLightDataTemp;
			shadowSceneFastReuse.LightCount = cachedLightCount;
			shadowSceneFastReuse.StrictLightCount = currentStrictLightCount;
			shadowSceneFastReuse.LightsHash = lightsHash;
			shadowSceneFastReuse.StrictHash = strictHash;
			shadowSceneFastReuse.StableDecodeCount = stableDecodeCount;
			shadowSceneFastReuse.ReuseAge = 0;
			shadowSceneFastReuseValid = true;

			static std::atomic_uint32_t fastReuseCaptureCount = 0;
			const auto captureIndex = ++fastReuseCaptureCount;
			if (captureIndex <= 8 || captureIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG ShadowScene fast-reuse captured decode captures={} stableDecodes={} refreshInterval={} node=0x{:X} lights={} strict={} shadowMask=0x{:08X} lightsHash=0x{:016X} strictHash=0x{:016X}",
					captureIndex,
					shadowSceneFastReuse.StableDecodeCount,
					fastReuseRefreshInterval,
					shadowSceneFastReuse.Key.Node,
					shadowSceneFastReuse.LightCount,
					shadowSceneFastReuse.StrictLightCount,
					strictLightDataTemp.ShadowBitMask,
					shadowSceneFastReuse.LightsHash,
					shadowSceneFastReuse.StrictHash);
			}
		} else {
			shadowSceneFastReuseValid = false;
			shadowSceneFastReuse = {};
		}
	}

	return collected;
}

bool LightLimitFix::UpdatePreNGStrictLightDataCB()
{
	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		return false;
	}

	return UpdatePreNGStrictLightDataCB(context);
}

bool LightLimitFix::UpdatePreNGStrictLightDataCB(ID3D11DeviceContext* a_context)
{
	static bool loggedMissing = false;
	if (!strictLightDataCB) {
		if (!loggedMissing) {
			logger::warn("[LightLimitFix] PreNG strict-light CB update requested but resource is unavailable");
			loggedMissing = true;
		}
		return false;
	}
	if (!a_context) {
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};
	const auto hr = a_context->Map(strictLightDataCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) {
		LogResourceFailure("Map(strictLightDataCB)", hr);
		return false;
	}

	std::memcpy(mapped.pData, &strictLightDataTemp, sizeof(strictLightDataTemp));
	a_context->Unmap(strictLightDataCB.get(), 0);
	return true;
}

bool LightLimitFix::UploadPreNGStrictLightDataDiagnostic()
{
	if (!ShouldUpdatePreNGStrictLightCB()) {
		return false;
	}

	return UpdatePreNGStrictLightDataCB();
}

bool LightLimitFix::BindPreNGStrictLightDataCBToPixelShader(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, bool a_bufferAlreadyUploaded)
{
	if (!ShouldBindPreNGStrictLightCB()) {
		return false;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG strict-light CB b3 bind held failures={} reason={} pass=0x{:X} requested={} strict={} shadowMask=0x{:08X} uploadedBeforeBind={}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				strictLightDataTemp.ShadowBitMask,
				a_bufferAlreadyUploaded);
		}
	};

	if (!strictLightDataCB) {
		logBindFailure("missing-strict-cb");
		return false;
	}

	if (!a_bufferAlreadyUploaded && !UpdatePreNGStrictLightDataCB()) {
		logBindFailure("upload-failed");
		return false;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		logBindFailure("renderer-data-unavailable");
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		logBindFailure("context-unavailable");
		return false;
	}

	ID3D11Buffer* buffer = strictLightDataCB.get();
	context->PSSetConstantBuffers(3, 1, &buffer);

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG strict-light CB bound to PS b3 binds={} pass=0x{:X} requested={} strict={} shadowMask=0x{:08X} uploadedBeforeBind={}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_pass),
			a_requestedLightCount,
			currentStrictLightCount,
			strictLightDataTemp.ShadowBitMask,
			a_bufferAlreadyUploaded);
	}

	return true;
}

bool LightLimitFix::BindPreNGClusterSRVsToPixelShader(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, bool a_strictCBBound)
{
	if (!ShouldBindPreNGClusterSRVs()) {
		return false;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG cluster SRV t35-t37 bind held failures={} reason={} pass=0x{:X} requested={} strict={} clusterLights={} shadowMask=0x{:08X} strictCBBound={}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				currentLightCount,
				strictLightDataTemp.ShadowBitMask,
				a_strictCBBound);
		}
	};

	if (!a_strictCBBound) {
		logBindFailure("strict-cb-not-bound");
		return false;
	}

	if (currentLightCount == 0) {
		logBindFailure("cluster-prepass-not-ready");
		return false;
	}

	if (!HasResources()) {
		logBindFailure("gpu-resources-incomplete");
		return false;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		logBindFailure("renderer-data-unavailable");
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		logBindFailure("context-unavailable");
		return false;
	}

	ID3D11ShaderResourceView* views[3]{
		lightsSRV.get(),
		lightIndexListSRV.get(),
		lightGridSRV.get()
	};
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 binds={} pass=0x{:X} requested={} strict={} clusterLights={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_pass),
			a_requestedLightCount,
			currentStrictLightCount,
			currentLightCount,
			clusterSize[0] * clusterSize[1] * clusterSize[2],
			strictLightDataTemp.ShadowBitMask,
			a_strictCBBound);
	}

	if (currentLightCount > 0) {
		static std::atomic_uint32_t nonZeroBindCount = 0;
		const auto nonZeroBindIndex = ++nonZeroBindCount;
		if (nonZeroBindIndex <= 8 || nonZeroBindIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG cluster SRVs nonzero bind proof nonzeroBinds={} binds={} pass=0x{:X} requested={} strict={} clusterLights={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
				nonZeroBindIndex,
				bindIndex,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				currentLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask,
				a_strictCBBound);
		}
	}

	return true;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDescriptorResourcesToPixelShader(const char* a_sourceName)
{
	PreNGDFLightResourceBindingState state{};
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;
	const char* sourceName = a_sourceName ? a_sourceName : "descriptor";

	const bool strictCBAlreadyUploaded =
		clusterPayloadCacheValid &&
		clusterPayloadCache.StrictCBUploaded;
	state.strictCBBound = BindPreNGStrictLightDataCBToPixelShader(nullptr, currentLightCount, strictCBAlreadyUploaded);
	if (state.strictCBBound && clusterPayloadCacheValid) {
		clusterPayloadCache.StrictCBUploaded = true;
	}
	state.clusterSRVsBound = BindPreNGClusterSRVsToPixelShader(nullptr, currentLightCount, state.strictCBBound);

	static std::atomic_uint32_t descriptorResourceBindCount = 0;
	const auto bindIndex = ++descriptorResourceBindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG {} resources bound binds={} lights={} strict={} shadowMask=0x{:08X} strictCB={} clusterSRVs={} strictCBUploadedBeforeBind={}",
			sourceName,
			bindIndex,
			state.lightCount,
			state.strictLightCount,
			state.shadowBitMask,
			state.strictCBBound,
			state.clusterSRVsBound,
			strictCBAlreadyUploaded);
	}

	return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDescriptorResourcesToPixelShader()
{
	return BindPreNGDescriptorResourcesToPixelShader("DFLight descriptor");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFCompositeDescriptorResourcesToPixelShader()
{
	return BindPreNGDescriptorResourcesToPixelShader("DFComposite descriptor");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGBSLightingDescriptorResourcesToPixelShader()
{
	return BindPreNGDescriptorResourcesToPixelShader("BSLighting descriptor");
}

#if defined(FALLOUT_PRE_NG)
	const char* GetPreNGBSLightingSetupGeometryPreviewReasonName(std::uint32_t a_reason)
	{
		if (a_reason > 0 && a_reason <= kPreNGBSLightingSetupGeometryPreviewMenus.size()) {
			return kPreNGBSLightingSetupGeometryPreviewMenus[a_reason - 1].data();
		}

		if (a_reason == kPreNGBSLightingSetupGeometryWorkshopPreviewReason) {
			return "WorkshopMenu3D";
		}

		return "none";
	}

	std::uint32_t DetectPreNGBSLightingSetupGeometryPreviewReason()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return 0;
		}

		for (std::uint32_t i = 0; i < kPreNGBSLightingSetupGeometryPreviewMenus.size(); ++i) {
			if (ui->GetMenuOpen(kPreNGBSLightingSetupGeometryPreviewMenus[i].data())) {
				return i + 1;
			}
		}

		const auto workshopMenu = ui->GetMenu<RE::WorkshopMenu>();
		if (workshopMenu.get() && workshopMenu->OnStack()) {
			const bool hasWorkshop3DPreview =
				workshopMenu->displayGeometry.get() ||
				!workshopMenu->displayItemModels.empty() ||
				workshopMenu->inv3DModelManager.itemBase ||
				workshopMenu->inv3DModelManager.tempRef;
			if (hasWorkshop3DPreview) {
				return kPreNGBSLightingSetupGeometryWorkshopPreviewReason;
			}
		}

		return 0;
	}

	std::uint32_t GetCachedPreNGBSLightingSetupGeometryPreviewReason()
	{
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (!runtime) {
			return DetectPreNGBSLightingSetupGeometryPreviewReason();
		}

		const auto frame = runtime->GetFrameCount();
		const auto cachedFrame = s_preNGBSLightingSetupGeometryPreviewCacheFrame.load(std::memory_order_acquire);
		if (cachedFrame == frame) {
			return s_preNGBSLightingSetupGeometryPreviewCacheReason.load(std::memory_order_acquire);
		}

		const auto reason = DetectPreNGBSLightingSetupGeometryPreviewReason();
		s_preNGBSLightingSetupGeometryPreviewCacheReason.store(reason, std::memory_order_release);
		s_preNGBSLightingSetupGeometryPreviewCacheFrame.store(frame, std::memory_order_release);
		return reason;
	}
#endif

bool LightLimitFix::ShouldProcessPreNGBSLightingSetupGeometryProof() const
{
	if (ShouldBindPreNGSetupGeometryStrictLightCB() ||
		ShouldPersistPreNGSetupGeometryStrictLightCB() ||
		ShouldUpdatePreNGStrictLightCB()) {
		return true;
	}

	if (!ShouldBindPreNGBSLightingSetupGeometryResources()) {
		return true;
	}

	auto* runtime = CommunityShaders::Runtime::GetSingleton();
	if (!runtime) {
		return true;
	}

	const auto frame = runtime->GetFrameCount();
	const auto bypassUntil = s_preNGBSLightingSetupGeometryBypassUntilFrame.load(std::memory_order_relaxed);
	if (frame < bypassUntil) {
		const auto bypassIndex = ++s_preNGBSLightingSetupGeometryBypassLogs;
		if (bypassIndex <= 8 || (bypassIndex & (bypassIndex - 1)) == 0) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting SetupGeometry proof bypass active bypasses={} frame={} until={}",
				bypassIndex,
				frame,
				bypassUntil);
		}
		return false;
	}

	return true;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGBSLightingSetupGeometryResources(RE::BSRenderPass* a_pass)
{
	PreNGDFLightResourceBindingState state{};
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!ShouldBindPreNGBSLightingSetupGeometryResources()) {
		return state;
	}

	static std::atomic_bool setupGeometryResourceProofComplete = false;
	if (setupGeometryResourceProofComplete.load(std::memory_order_relaxed)) {
		return state;
	}

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;
	if (currentLightCount == 0) {
		currentStrictLightCount = 0;
		strictLightDataTemp.NumStrictLights = 0;
		strictLightDataTemp.ShadowBitMask = 0;
		state.strictLightCount = 0;
		state.shadowBitMask = 0;
		ExtendPreNGBSLightingSetupGeometryBypassWindow();
		if (TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame()) {
			static std::atomic_uint32_t noLightFrameCount = 0;
			const auto noLightFrameIndex = ++noLightFrameCount;
			if (noLightFrameIndex <= 8 || (noLightFrameIndex & (noLightFrameIndex - 1)) == 0) {
				logger::info(
					"[LightLimitFix] PreNG BSLighting SetupGeometry resource proof held for no-light frame frames={} pass=0x{:X}; descriptor/resource demand remains idle until scene lights are collected",
					noLightFrameIndex,
					reinterpret_cast<std::uintptr_t>(a_pass));
			}
		}
		return state;
	}

	const auto currentPixelShader = ReadPreNGCurrentPixelShaderEntryState();
	NotifyPreNGBSLightingLLFConsumerDescriptorObserved(
		0,
		currentPixelShader.id,
		currentPixelShader.d3dObject != 0,
		currentPixelShader.d3dObject);

	if (!HasPreNGBSLightingDescriptorConsumerData()) {
		static std::atomic_uint32_t pendingCount = 0;
		const auto pendingIndex = ++pendingCount;
		if (pendingIndex <= 8 || pendingIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting SetupGeometry resource bind pending payload pending={} pass=0x{:X} currentPS=0x{:X} currentPSId=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
				pendingIndex,
				reinterpret_cast<std::uintptr_t>(a_pass),
				currentPixelShader.d3dObject,
				currentPixelShader.id,
				currentLightCount,
				currentStrictLightCount,
				strictLightDataTemp.ShadowBitMask);
		}
		return state;
	}

	state = BindPreNGDescriptorResourcesToPixelShader("BSLighting SetupGeometry");

	static std::atomic_uint32_t auditCount = 0;
	const auto auditIndex = ++auditCount;
	if (auditIndex <= 8 || auditIndex % 512 == 0) {
		TracePreNGActiveLightingBindings(
			"bslighting-setup-geometry-resource-bind",
			static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE),
			0,
			currentPixelShader.id,
			currentPixelShader.d3dObject != 0,
			currentPixelShader.d3dObject);
	}

	if (state.strictCBBound && state.clusterSRVsBound &&
		!setupGeometryResourceProofComplete.exchange(true, std::memory_order_relaxed)) {
		logger::info(
			"[LightLimitFix] PreNG BSLighting SetupGeometry resource-only proof reached b3/t35-t37 completion on current BSLighting path; future SetupGeometry resource binds are held until a visible-safe consumer is implemented");
	}

	return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDrawStateStrictLightCB(ID3D11DeviceContext* a_context)
{
	PreNGDFLightResourceBindingState state{};
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!ShouldBindPreNGDFLightDrawStateStrictLightCB()) {
		return state;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG DFLight draw-state strict-light CB b3 bind held failures={} reason={} context=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_context),
				state.lightCount,
				state.strictLightCount,
				state.shadowBitMask);
		}
	};

	if (!a_context) {
		logBindFailure("context-unavailable");
		return state;
	}
	if (!strictLightDataCB) {
		logBindFailure("missing-strict-cb");
		return state;
	}
	if (!UpdatePreNGStrictLightDataCB(a_context)) {
		logBindFailure("strict-cb-upload-failed");
		return state;
	}

	ID3D11Buffer* strictCB = strictLightDataCB.get();
	a_context->PSSetConstantBuffers(3, 1, &strictCB);
	state.strictCBBound = true;

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state strict-light CB bound to PS b3 binds={} context=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_context),
			state.lightCount,
			state.strictLightCount,
			state.shadowBitMask);
	}

	return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDrawStateClusterSRVs(
	ID3D11DeviceContext* a_context,
	bool a_strictCBBound)
{
	PreNGDFLightResourceBindingState state{};
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;
	state.strictCBBound = a_strictCBBound;

	if (!ShouldBindPreNGDFLightDrawStateClusterSRVs()) {
		return state;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG DFLight draw-state cluster SRV t35-t37 bind held failures={} reason={} context=0x{:X} lights={} strict={} shadowMask=0x{:08X} strictCBBound={}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_context),
				state.lightCount,
				state.strictLightCount,
				state.shadowBitMask,
				state.strictCBBound);
		}
	};

	if (!a_context) {
		logBindFailure("context-unavailable");
		return state;
	}
	if (!strictLightDataCB) {
		logBindFailure("missing-strict-cb");
		return state;
	}
	if (!state.strictCBBound) {
		ID3D11Buffer* currentStrictCB = nullptr;
		a_context->PSGetConstantBuffers(3, 1, &currentStrictCB);
		state.strictCBBound = currentStrictCB == strictLightDataCB.get();
		if (currentStrictCB) {
			currentStrictCB->Release();
		}
	}
	if (!state.strictCBBound) {
		logBindFailure("strict-cb-not-bound");
		return state;
	}
	if (currentLightCount == 0) {
		logBindFailure("cluster-prepass-not-ready");
		return state;
	}
	if (!HasResources()) {
		logBindFailure("gpu-resources-incomplete");
		return state;
	}
	if (!lightsSRV || !lightIndexListSRV || !lightGridSRV) {
		logBindFailure("missing-cluster-srvs");
		return state;
	}

	ID3D11ShaderResourceView* views[3]{
		lightsSRV.get(),
		lightIndexListSRV.get(),
		lightGridSRV.get()
	};
	a_context->PSSetShaderResources(35, ARRAYSIZE(views), views);
	state.clusterSRVsBound = true;

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state cluster SRVs bound to PS t35-t37 binds={} context=0x{:X} lights={} strict={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_context),
			state.lightCount,
			state.strictLightCount,
			clusterSize[0] * clusterSize[1] * clusterSize[2],
			state.shadowBitMask,
			state.strictCBBound);
	}

	static std::atomic_uint32_t nonZeroBindCount = 0;
	const auto nonZeroBindIndex = ++nonZeroBindCount;
	if (nonZeroBindIndex <= 8 || nonZeroBindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state cluster SRVs nonzero bind proof nonzeroBinds={} binds={} context=0x{:X} lights={} strict={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
			nonZeroBindIndex,
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_context),
			state.lightCount,
			state.strictLightCount,
			clusterSize[0] * clusterSize[1] * clusterSize[2],
			state.shadowBitMask,
			state.strictCBBound);
	}

	return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightNoOpPassResources(
	ID3D11DeviceContext* a_context,
	const char* a_passName)
{
	PreNGDFLightResourceBindingState state{};
	state.lightCount = currentLightCount;
	state.strictLightCount = currentStrictLightCount;
	state.shadowBitMask = strictLightDataTemp.ShadowBitMask;
	const char* passName = a_passName ? a_passName : "unknown no-op pass";

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG DFLight {} LLF resource bind held failures={} reason={} context=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
				passName,
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_context),
				state.lightCount,
				state.strictLightCount,
				state.shadowBitMask);
		}
	};

	if (!a_context) {
		logBindFailure("context-unavailable");
		return state;
	}
	if (!HasResources()) {
		logBindFailure("gpu-resources-incomplete");
		return state;
	}
	if (!strictLightDataCB) {
		logBindFailure("missing-strict-cb");
		return state;
	}
	if (!lightsSRV || !lightIndexListSRV || !lightGridSRV) {
		logBindFailure("missing-cluster-srvs");
		return state;
	}
	if (currentLightCount == 0) {
		logBindFailure("cluster-prepass-not-ready");
		return state;
	}
	if (!UpdatePreNGStrictLightDataCB()) {
		logBindFailure("strict-cb-upload-failed");
		return state;
	}

	ID3D11Buffer* strictCB = strictLightDataCB.get();
	a_context->PSSetConstantBuffers(3, 1, &strictCB);
	state.strictCBBound = true;

	ID3D11ShaderResourceView* views[3]{
		lightsSRV.get(),
		lightIndexListSRV.get(),
		lightGridSRV.get()
	};
	a_context->PSSetShaderResources(35, ARRAYSIZE(views), views);
	state.clusterSRVsBound = true;

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG DFLight {} LLF resources bound binds={} context=0x{:X} lights={} strict={} clusters={} shadowMask=0x{:08X}",
			passName,
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_context),
			state.lightCount,
			state.strictLightCount,
			clusterSize[0] * clusterSize[1] * clusterSize[2],
			state.shadowBitMask);
	}

	return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightResourceNoOpPass(ID3D11DeviceContext* a_context)
{
	return BindPreNGDFLightNoOpPassResources(a_context, "resource no-op pass");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightFullContractNoOpPass(ID3D11DeviceContext* a_context)
{
	return BindPreNGDFLightNoOpPassResources(a_context, "full contract no-op pass");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightLLFAdditivePass(ID3D11DeviceContext* a_context)
{
	return BindPreNGDFLightNoOpPassResources(a_context, "LLF additive pass");
}

bool LightLimitFix::TracePreNGActiveLightingBindings(
	const char* a_source,
	std::int32_t a_shaderType,
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_found,
	std::uintptr_t a_lookupPixelShader,
	ID3D11DeviceContext* a_contextOverride)
{
	static std::atomic_bool descriptorDFLightBindAuditComplete = false;
	static std::atomic_bool descriptorDFLightBindSkipLogged = false;
	static std::atomic_uint32_t descriptorDFLightBindSkippedAuditCount = 0;
	static std::atomic_bool descriptorDFCompositeSafeBindAuditComplete = false;
	static std::atomic_bool descriptorDFCompositeSafeBindSkipLogged = false;
	static std::atomic_uint32_t descriptorDFCompositeSafeBindSkippedAuditCount = 0;

	const std::string_view sourceName = a_source ? std::string_view{ a_source } : std::string_view{};
	const bool descriptorDFLightBindAudit = sourceName == "descriptor-dflight-bind";
	const bool descriptorDFCompositeSafeBindAudit = sourceName == "descriptor-dfcomposite-safe-bind";
	if (descriptorDFLightBindAudit && descriptorDFLightBindAuditComplete.load(std::memory_order_relaxed)) {
		const auto skipped = descriptorDFLightBindSkippedAuditCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (!descriptorDFLightBindSkipLogged.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG descriptor DFLight binding audit complete; skipping repeated D3D state queries source={} skipped={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
				a_source,
				skipped,
				a_shaderType,
				a_vertexDescriptor,
				a_pixelDescriptor);
		}
		return true;
	}
	if (descriptorDFCompositeSafeBindAudit && descriptorDFCompositeSafeBindAuditComplete.load(std::memory_order_relaxed)) {
		const auto skipped = descriptorDFCompositeSafeBindSkippedAuditCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (!descriptorDFCompositeSafeBindSkipLogged.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG descriptor DFComposite safe-bind audit complete; skipping repeated D3D state queries source={} skipped={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
				a_source,
				skipped,
				a_shaderType,
				a_vertexDescriptor,
				a_pixelDescriptor);
		}
		return true;
	}

	std::optional<CommunityShaders::ShaderCache::ShaderMetadata> currentPixelShaderMetadata;
	std::optional<CommunityShaders::ShaderCache::ShaderMetadata> lookupPixelShaderMetadata;

	auto logAudit = [&](
						const char* a_reason,
						ID3D11DeviceContext* a_context,
						std::uintptr_t a_currentPixelShader,
						bool a_pixelShaderMatches,
						bool a_b3Matches,
						bool a_t35Matches,
						bool a_t36Matches,
						bool a_t37Matches,
						ID3D11Buffer* a_boundB3,
						ID3D11ShaderResourceView* a_boundT35,
						ID3D11ShaderResourceView* a_boundT36,
						ID3D11ShaderResourceView* a_boundT37) {
		static std::atomic_uint32_t setupGeometryQueriedAuditCount = 0;
		static std::atomic_uint32_t shaderLookupQueriedAuditCount = 0;
		static std::atomic_uint32_t dflightDrawStateQueriedAuditCount = 0;
		static std::atomic_uint32_t dflightCandidateBindQueriedAuditCount = 0;
		static std::atomic_uint32_t descriptorDFLightBindQueriedAuditCount = 0;
		static std::atomic_uint32_t descriptorDFCompositeSafeBindQueriedAuditCount = 0;
		static std::atomic_uint32_t pointLightHookQueriedAuditCount = 0;
		static std::atomic_uint32_t otherQueriedAuditCount = 0;
		static std::atomic_uint32_t heldAuditCount = 0;
		static std::atomic_bool dflightDrawStateB3Logged = false;
		const bool queried = std::strcmp(a_reason, "queried") == 0;
		const bool dflightDrawStateAudit = queried && sourceName == "dflight-draw-state";
		std::atomic_uint32_t* auditCounter = &heldAuditCount;
		if (queried) {
			if (sourceName == "setup-geometry") {
				auditCounter = &setupGeometryQueriedAuditCount;
			} else if (sourceName == "shader-lookup") {
				auditCounter = &shaderLookupQueriedAuditCount;
			} else if (sourceName == "dflight-draw-state") {
				auditCounter = &dflightDrawStateQueriedAuditCount;
			} else if (sourceName == "dflight-full-shadowed-candidate-bind") {
				auditCounter = &dflightCandidateBindQueriedAuditCount;
			} else if (sourceName == "descriptor-dflight-bind") {
				auditCounter = &descriptorDFLightBindQueriedAuditCount;
			} else if (sourceName == "descriptor-dfcomposite-safe-bind") {
				auditCounter = &descriptorDFCompositeSafeBindQueriedAuditCount;
			} else if (sourceName == "point-light-hook") {
				auditCounter = &pointLightHookQueriedAuditCount;
			} else {
				auditCounter = &otherQueriedAuditCount;
			}
		}
		const auto auditIndex = ++(*auditCounter);
		const bool resourceComplete = a_b3Matches && a_t35Matches && a_t36Matches && a_t37Matches;
		const bool complete = a_pixelShaderMatches && resourceComplete;
		const auto currentEvidence = GetPreNGShaderSlotEvidence(currentPixelShaderMetadata);
		const auto lookupEvidence = GetPreNGShaderSlotEvidence(lookupPixelShaderMetadata);
		const bool llfConsumerComplete =
			complete &&
			currentEvidence.hasMetadata &&
			currentEvidence.declaresCB3 &&
			currentEvidence.declaresT35 &&
			currentEvidence.declaresT36 &&
			currentEvidence.declaresT37 &&
			currentEvidence.samplesT35 > 0 &&
			currentEvidence.samplesT36 > 0 &&
			currentEvidence.samplesT37 > 0;
		const bool descriptorCompleteAudit = queried && descriptorDFLightBindAudit && llfConsumerComplete;
		const bool descriptorDFCompositeSafeCompleteAudit = queried && descriptorDFCompositeSafeBindAudit && llfConsumerComplete;
		const auto initialAuditLimit = dflightDrawStateAudit ? 64u : ((descriptorDFLightBindAudit || descriptorDFCompositeSafeBindAudit) ? 32u : 16u);
		const bool forceFirstDFLightB3 =
			dflightDrawStateAudit &&
			a_b3Matches &&
			!dflightDrawStateB3Logged.exchange(true, std::memory_order_relaxed);
		if (!forceFirstDFLightB3 && !descriptorCompleteAudit && !descriptorDFCompositeSafeCompleteAudit && auditIndex > initialAuditLimit && auditIndex % 128 != 0) {
			return;
		}

		const bool currentFullShadowed = HasPreNGFullShadowedDFLightVanillaContract(currentPixelShaderMetadata);
		const bool lookupFullShadowed = HasPreNGFullShadowedDFLightVanillaContract(lookupPixelShaderMetadata);
		logger::info(
			"[LightLimitFix] PreNG active lighting binding audit audits={} source={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X} found={} reason={} context=0x{:X} currentPS=0x{:X} lookupPS=0x{:X} psMatch={} b3={} t35={} t36={} t37={} resourceComplete={} complete={} llfConsumerComplete={} currentMeta={} lookupMeta={} currentFullShadowed={} lookupFullShadowed={} currentDecl=(meta={},cb3={},t35={},t36={},t37={}) currentSamples=(t35={},t36={},t37={}) lookupDecl=(meta={},cb3={},t35={},t36={},t37={}) lookupSamples=(t35={},t36={},t37={}) strict={} lights={} shadowMask=0x{:08X} bound=(b3=0x{:X},t35=0x{:X},t36=0x{:X},t37=0x{:X}) expected=(b3=0x{:X},t35=0x{:X},t36=0x{:X},t37=0x{:X})",
			auditIndex,
			a_source ? a_source : "<null>",
			a_shaderType,
			a_vertexDescriptor,
			a_pixelDescriptor,
			a_found,
			a_reason,
			reinterpret_cast<std::uintptr_t>(a_context),
			a_currentPixelShader,
			a_lookupPixelShader,
			a_pixelShaderMatches,
			a_b3Matches,
			a_t35Matches,
			a_t36Matches,
			a_t37Matches,
			resourceComplete,
			complete,
			llfConsumerComplete,
			FormatPreNGShaderMetadata(currentPixelShaderMetadata),
			FormatPreNGShaderMetadata(lookupPixelShaderMetadata),
			currentFullShadowed,
			lookupFullShadowed,
			currentEvidence.hasMetadata,
			currentEvidence.declaresCB3,
			currentEvidence.declaresT35,
			currentEvidence.declaresT36,
			currentEvidence.declaresT37,
			currentEvidence.samplesT35,
			currentEvidence.samplesT36,
			currentEvidence.samplesT37,
			lookupEvidence.hasMetadata,
			lookupEvidence.declaresCB3,
			lookupEvidence.declaresT35,
			lookupEvidence.declaresT36,
			lookupEvidence.declaresT37,
			lookupEvidence.samplesT35,
			lookupEvidence.samplesT36,
			lookupEvidence.samplesT37,
			currentStrictLightCount,
			currentLightCount,
			strictLightDataTemp.ShadowBitMask,
			reinterpret_cast<std::uintptr_t>(a_boundB3),
			reinterpret_cast<std::uintptr_t>(a_boundT35),
			reinterpret_cast<std::uintptr_t>(a_boundT36),
			reinterpret_cast<std::uintptr_t>(a_boundT37),
			reinterpret_cast<std::uintptr_t>(strictLightDataCB.get()),
			reinterpret_cast<std::uintptr_t>(lightsSRV.get()),
			reinterpret_cast<std::uintptr_t>(lightIndexListSRV.get()),
			reinterpret_cast<std::uintptr_t>(lightGridSRV.get()));
	};

	if (!HasResources() || !strictLightDataCB) {
		logAudit("resources-incomplete", nullptr, 0, false, false, false, false, false, nullptr, nullptr, nullptr, nullptr);
		return false;
	}

	auto* context = a_contextOverride;
	if (!context) {
		auto* rendererData = fo4cs::GetRendererData();
		if (!rendererData) {
			logAudit("renderer-data-unavailable", nullptr, 0, false, false, false, false, false, nullptr, nullptr, nullptr, nullptr);
			return false;
		}
		context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	}
	if (!context) {
		logAudit("context-unavailable", nullptr, 0, false, false, false, false, false, nullptr, nullptr, nullptr, nullptr);
		return false;
	}

	ID3D11PixelShader* currentPixelShader = nullptr;
	context->PSGetShader(&currentPixelShader, nullptr, nullptr);
	ID3D11Buffer* boundStrictCB = nullptr;
	context->PSGetConstantBuffers(3, 1, &boundStrictCB);
	ID3D11ShaderResourceView* boundSRVs[3]{};
	context->PSGetShaderResources(35, ARRAYSIZE(boundSRVs), boundSRVs);

	const auto currentPixelShaderAddress = reinterpret_cast<std::uintptr_t>(currentPixelShader);
	if (auto* shaderCache = CommunityShaders::ShaderCache::GetSingleton()) {
		currentPixelShaderMetadata = shaderCache->GetMetadataForD3DShaderObject(CommunityShaders::ShaderStage::Pixel, currentPixelShaderAddress);
		if (a_lookupPixelShader != 0) {
			lookupPixelShaderMetadata = a_lookupPixelShader == currentPixelShaderAddress ?
				currentPixelShaderMetadata :
				shaderCache->GetMetadataForD3DShaderObject(CommunityShaders::ShaderStage::Pixel, a_lookupPixelShader);
		}
	}
	const bool pixelShaderMatches = a_lookupPixelShader != 0 && currentPixelShaderAddress == a_lookupPixelShader;
	const bool b3Matches = boundStrictCB == strictLightDataCB.get();
	const bool t35Matches = boundSRVs[0] == lightsSRV.get();
	const bool t36Matches = boundSRVs[1] == lightIndexListSRV.get();
	const bool t37Matches = boundSRVs[2] == lightGridSRV.get();
	const bool resourceComplete = b3Matches && t35Matches && t36Matches && t37Matches;
	const bool complete = pixelShaderMatches && resourceComplete;
	const auto currentCompletionEvidence = GetPreNGShaderSlotEvidence(currentPixelShaderMetadata);
	const bool llfConsumerComplete =
		complete &&
		currentCompletionEvidence.hasMetadata &&
		currentCompletionEvidence.declaresCB3 &&
		currentCompletionEvidence.declaresT35 &&
		currentCompletionEvidence.declaresT36 &&
		currentCompletionEvidence.declaresT37 &&
		currentCompletionEvidence.samplesT35 > 0 &&
		currentCompletionEvidence.samplesT36 > 0 &&
		currentCompletionEvidence.samplesT37 > 0;

	logAudit(
		"queried",
		context,
		currentPixelShaderAddress,
		pixelShaderMatches,
		b3Matches,
		t35Matches,
		t36Matches,
		t37Matches,
		boundStrictCB,
		boundSRVs[0],
		boundSRVs[1],
		boundSRVs[2]);

	if (currentPixelShader) {
		currentPixelShader->Release();
	}
	if (boundStrictCB) {
		boundStrictCB->Release();
	}
	for (auto* srv : boundSRVs) {
		if (srv) {
			srv->Release();
		}
	}

	if (descriptorDFLightBindAudit && llfConsumerComplete) {
		if (!descriptorDFLightBindAuditComplete.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG descriptor DFLight binding audit reached LLF consumer completion; future descriptor-dflight-bind audits skip D3D state queries source={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
				a_source,
				a_shaderType,
				a_vertexDescriptor,
				a_pixelDescriptor);
		}
	}
	if (descriptorDFCompositeSafeBindAudit && llfConsumerComplete) {
		if (!descriptorDFCompositeSafeBindAuditComplete.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[LightLimitFix] PreNG descriptor DFComposite safe-bind audit reached LLF consumer completion; future descriptor-dfcomposite-safe-bind audits skip D3D state queries source={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
				a_source,
				a_shaderType,
				a_vertexDescriptor,
				a_pixelDescriptor);
		}
	}

	return llfConsumerComplete;
}
#endif

void LightLimitFix::CollectLightCB()
{
	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!ctx || !device) return;

	ID3D11Buffer* lightCB = nullptr;
	ctx->PSGetConstantBuffers(2, 1, &lightCB);
	if (!lightCB) return;

	D3D11_BUFFER_DESC desc;
	lightCB->GetDesc(&desc);
	if (desc.ByteWidth < 48) { lightCB->Release(); return; }

	D3D11_BUFFER_DESC stagingDesc{};
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.ByteWidth = desc.ByteWidth;

	ID3D11Buffer* stagingCB = nullptr;
	if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &stagingCB))) {
		lightCB->Release();
		return;
	}

	ctx->CopyResource(stagingCB, lightCB);
	lightCB->Release();

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped))) {
		stagingCB->Release();
		return;
	}

	const float* rawData = static_cast<const float*>(mapped.pData);
	std::uint32_t lightCount = desc.ByteWidth / 48;
	if (lightCount > 4) lightCount = 4;

	for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++) {
		const float* l = rawData + i * 12;

		if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f) continue;

		auto cbHash = static_cast<std::uint64_t>(l[0] * 1000.0f) ^
		              (static_cast<std::uint64_t>(l[1] * 1000.0f) << 20) ^
		              (static_cast<std::uint64_t>(l[4] * 255.0f) << 40);

		if (seenCBHashes.contains(cbHash)) continue;
		seenCBHashes.insert(cbHash);

		LightData data{};
		data.positionWS[0].data.x = l[0];
		data.positionWS[0].data.y = l[1];
		data.positionWS[0].data.z = l[2];
		data.radius       = l[3];
		data.color.x      = l[4];
		data.color.y      = l[5];
		data.color.z      = l[6];
		data.fade         = l[7];
		data.invRadius    = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.lightFlags   = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}

	ctx->Unmap(stagingCB, 0);
	stagingCB->Release();
}

void LightLimitFix::CollectLightsFromBSLight()
{
	for (auto* light : seenLights) {
		if (!light || frameLights.size() >= kMaxLights) break;
		auto* niLight = reinterpret_cast<NiLightView*>(light);
		LightData data{};
		data.color.x = niLight->diff.r;
		data.color.y = niLight->diff.g;
		data.color.z = niLight->diff.b;
		data.fade = niLight->dimmer;
		data.radius = niLight->modelBound.fRadius;
		data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.positionWS[0].data.x = niLight->world.translate.x;
		data.positionWS[0].data.y = niLight->world.translate.y;
		data.positionWS[0].data.z = niLight->world.translate.z;
		data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}
}

void LightLimitFix::CollectLightsFromScene()
{
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return;

	auto& refs = dh->GetFormArray<RE::TESObjectREFR>();
	for (auto* ref : refs) {
		if (!ref || frameLights.size() >= kMaxLights) break;

		auto* baseObj = ref->GetObjectReference();
		auto* lightForm = baseObj ? baseObj->As<RE::TESObjectLIGH>() : nullptr;
		if (!lightForm) continue;

		auto* niObj = ref->Get3D();
		if (!niObj) continue;

		auto* niLight = reinterpret_cast<NiLightView*>(niObj);
		if (!niLight) continue;

		auto* lightKey = reinterpret_cast<RE::BSLight*>(niLight);
		if (seenLights.contains(lightKey)) continue;
		seenLights.insert(lightKey);

		LightData data{};
		data.color.x = niLight->diff.r;
		data.color.y = niLight->diff.g;
		data.color.z = niLight->diff.b;
		data.fade = niLight->dimmer;
		data.radius = niLight->modelBound.fRadius;
		data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.positionWS[0].data.x = niLight->world.translate.x;
		data.positionWS[0].data.y = niLight->world.translate.y;
		data.positionWS[0].data.z = niLight->world.translate.z;
		data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass* /*a_pass*/)
{
	seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass* a_pass)
{
#if defined(FALLOUT_PRE_NG)
	if (!TryReservePreNGSetupGeometryCall()) {
		return;
	}
	if (!TryReservePreNGSetupGeometryFrameSample()) {
		return;
	}

	const auto collected = CollectLightsFromPreNGSceneLights(a_pass);
	if (collected > 0) {
		const bool strictCBBindGate = ShouldBindPreNGSetupGeometryStrictLightCB();
		const bool strictCBPersistRequested = ShouldPersistPreNGSetupGeometryStrictLightCB();
		const bool strictCBPersistentActive = strictCBPersistRequested && strictCBBindGate;
		const bool strictCBProofRequested = ShouldUpdatePreNGStrictLightCB() || strictCBBindGate;
		std::uint32_t strictCBProofSample = 0;
		bool strictCBProofActive = false;
		if (strictCBProofRequested) {
			if (strictCBPersistentActive) {
				static std::atomic_uint32_t setupGeometryPersistentStrictCBSamples = 0;
				strictCBProofSample = setupGeometryPersistentStrictCBSamples.fetch_add(1, std::memory_order_relaxed) + 1;
				strictCBProofActive = true;
				static bool loggedPersistentStrictCB = false;
				if (!loggedPersistentStrictCB) {
					logger::info(
						"[LightLimitFix] PreNG SetupGeometry persistent strict-CB path active; continuing upload/bind beyond the {} sample proof window",
						kPreNGSetupGeometryStrictCBProofMaxSamples);
					loggedPersistentStrictCB = true;
				}
			} else {
				static std::atomic_uint32_t setupGeometryStrictCBProofSamples = 0;
				const auto proofIndex = setupGeometryStrictCBProofSamples.fetch_add(1, std::memory_order_relaxed);
				if (proofIndex < kPreNGSetupGeometryStrictCBProofMaxSamples) {
					strictCBProofActive = true;
					strictCBProofSample = proofIndex + 1;
				} else {
					static bool loggedStrictCBProofComplete = false;
					if (!loggedStrictCBProofComplete) {
						logger::info(
							"[LightLimitFix] PreNG SetupGeometry strict-CB proof window complete; holding upload/bind after {} accepted samples",
							kPreNGSetupGeometryStrictCBProofMaxSamples);
						loggedStrictCBProofComplete = true;
					}
				}
			}
		}
		const bool strictCBUploaded = strictCBProofActive ? UploadPreNGStrictLightDataDiagnostic() : false;
		const bool strictCBBindRequested = strictCBProofActive && strictCBBindGate;
		ID3D11DeviceContext* setupGeometryContext = nullptr;
		ID3D11Buffer* previousStrictCB = nullptr;
		bool strictCBRestorePrepared = false;
		if (strictCBBindRequested && !strictCBPersistRequested) {
			if (auto* rendererData = fo4cs::GetRendererData()) {
				setupGeometryContext = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
				if (setupGeometryContext) {
					setupGeometryContext->PSGetConstantBuffers(3, 1, &previousStrictCB);
					strictCBRestorePrepared = true;
				}
			}
		}
		const bool strictCBBound = strictCBBindRequested && (strictCBPersistRequested || strictCBRestorePrepared) ?
			BindPreNGStrictLightDataCBToPixelShader(a_pass, collected, strictCBUploaded) :
			false;
		const bool strictCBAvailable = strictCBUploaded || strictCBBound;
		if (strictCBBound) {
			TracePreNGActiveLightingBindings("setup-geometry", -1, 0, 0, false, 0);
			if (!strictCBPersistRequested && strictCBRestorePrepared && setupGeometryContext) {
				setupGeometryContext->PSSetConstantBuffers(3, 1, &previousStrictCB);
			}
		}
		if (previousStrictCB) {
			previousStrictCB->Release();
		}
		const char* strictCBRestoreState = strictCBBound ?
			(strictCBPersistRequested ? "persistent" : (strictCBRestorePrepared ? "restored" : "restore-unavailable")) :
			"held";
		const char* strictCBProofState = strictCBProofRequested ?
			(strictCBPersistentActive ? "persistent" : (strictCBProofActive ? "active" : "complete")) :
			"off";
		static std::atomic_uint32_t setupGeometryCollectCount = 0;
		const auto collectIndex = ++setupGeometryCollectCount;
		if (collectIndex <= 8 || collectIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry scene-light collection accepted samples={} pass=0x{:X} collected={} strict={} strictCB={} b3={} b3Restore={} setupGeometryBindGate={} setupGeometryPersistGate={} strictCBProof={} proofSample={} shadowMask=0x{:08X}",
				collectIndex,
				reinterpret_cast<std::uintptr_t>(a_pass),
				collected,
				currentStrictLightCount,
				strictCBAvailable ? "uploaded" : "held",
				strictCBBound ? "bound" : "held",
				strictCBRestoreState,
				strictCBBindGate ? "on" : "off",
				strictCBPersistRequested ? "on" : "off",
				strictCBProofState,
				strictCBProofSample,
				strictLightDataTemp.ShadowBitMask);
		}
	} else {
		static std::atomic_uint32_t setupGeometryEmptyCount = 0;
		const auto emptyIndex = ++setupGeometryEmptyCount;
		if (emptyIndex <= 8 || emptyIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG SetupGeometry scene-light collection empty samples={} pass=0x{:X} strict={} shadowMask=0x{:08X}",
				emptyIndex,
				reinterpret_cast<std::uintptr_t>(a_pass),
				currentStrictLightCount,
				strictLightDataTemp.ShadowBitMask);
		}
	}
#else
	CollectLightsFromPass(a_pass);
#endif
}

namespace RE::VTABLE
{
}

void LightLimitFix::Hooks::Install(bool a_includeEffectShader)
{
	static std::atomic_bool lightingInstalled = false;
	static std::atomic_bool effectInstalled = false;

	const bool installedLightingNow = !lightingInstalled.exchange(true, std::memory_order_acq_rel);
	bool installedEffectNow = false;
	if (installedLightingNow) {
		stl::write_vfunc<0x7, BSLightingShader_SetupGeometry>(RE::VTABLE::BSLightingShader[0]);
	}
#if defined(FALLOUT_PRE_NG)
	s_preNGBSLightingSetupGeometryHookInstalled.store(true, std::memory_order_release);
#endif
	if (a_includeEffectShader) {
		installedEffectNow = !effectInstalled.exchange(true, std::memory_order_acq_rel);
		if (installedEffectNow) {
			stl::write_vfunc<0x7, BSEffectShader_SetupGeometry>(RE::VTABLE::BSEffectShader[0]);
		}
	}

	const auto* state = CommunityShaders::State::GetSingleton();
	const char* runtimeName = state ? state->GetRuntimeName().c_str() : "unknown";
	if (installedLightingNow || installedEffectNow) {
		logger::info(
			"[LightLimitFix] Installed SetupGeometry hooks (runtime={}, vfunc index 7, lightingInstalled={} effectInstalled={} effectRequested={})",
			runtimeName,
			installedLightingNow,
			installedEffectNow,
			a_includeEffectShader);
	} else {
		logger::info(
			"[LightLimitFix] SetupGeometry hooks already installed; skipping duplicate install (runtime={}, effectRequested={})",
			runtimeName,
			a_includeEffectShader);
	}
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass)
{
	auto& self = globals::features::lightLimitFix;
#if defined(FALLOUT_PRE_NG)
	const auto totalSetupGeometryCalls = s_preNGBSLightingSetupGeometryHookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
	if (!self.ShouldProcessPreNGBSLightingSetupGeometryProof()) {
		const auto previewReason = GetCachedPreNGBSLightingSetupGeometryPreviewReason();
		const bool preserveSceneLightCollection = previewReason == 0;
		const auto bypassCallIndex = s_preNGBSLightingSetupGeometryBypassCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (bypassCallIndex <= 8 || (bypassCallIndex & (bypassCallIndex - 1)) == 0) {
			logger::info(
				"[LightLimitFix] PreNG BSLighting SetupGeometry proof bypass calling vanilla calls={} totalCalls={} reason={} collectSceneLights={} shader=0x{:X} pass=0x{:X}",
				bypassCallIndex,
				totalSetupGeometryCalls,
				GetPreNGBSLightingSetupGeometryPreviewReasonName(previewReason),
				preserveSceneLightCollection,
				reinterpret_cast<std::uintptr_t>(a_this),
				reinterpret_cast<std::uintptr_t>(a_pass));
		}
		if (preserveSceneLightCollection) {
			self.SetupGeometryBefore(a_pass);
			func(a_this, a_pass);
			self.SetupGeometryAfter(a_pass);
		} else {
			func(a_this, a_pass);
		}
		return;
	}

	static std::atomic_uint32_t preNGBSLightingSetupGeometryCalls = 0;
	const auto callIndex = ++preNGBSLightingSetupGeometryCalls;
	if (callIndex <= 8 || callIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG BSLightingShader SetupGeometry hook reached calls={} totalCalls={} shader=0x{:X} pass=0x{:X}",
			callIndex,
			totalSetupGeometryCalls,
			reinterpret_cast<std::uintptr_t>(a_this),
			reinterpret_cast<std::uintptr_t>(a_pass));
	}
#endif
	self.SetupGeometryBefore(a_pass);
	func(a_this, a_pass);
	self.SetupGeometryAfter(a_pass);
#if defined(FALLOUT_PRE_NG)
	self.BindPreNGBSLightingSetupGeometryResources(a_pass);
#endif
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass)
{
	func(a_this, a_pass);
	auto& self = globals::features::lightLimitFix;
	self.SetupGeometryBefore(a_pass);
	self.SetupGeometryAfter(a_pass);
}
