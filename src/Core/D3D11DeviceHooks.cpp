#include "Core/Hooks.h"

#include "Core/HooksInternal.h"
#include "Core/PreNGEnvironment.h"
#include "Core/ShaderCache.h"

namespace CommunityShaders::Hooks
{
	namespace
	{
		using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
		using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);

		CreateVertexShaderFn createVertexShader = nullptr;
		CreateComputeShaderFn createComputeShader = nullptr;
		bool installedDeviceHooks = false;

#if defined(FALLOUT_PRE_NG)
		using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);

		CreateDeferredContextFn createDeferredContext = nullptr;
		constexpr const char* kPreNGShaderObjectMetadataEnv = "FO4CS_LLF_PRENG_SHADER_OBJECT_METADATA";

		bool ShouldMapPreNGShaderObjectMetadata()
		{
			static const bool enabled =
				ReadPreNGEnvironmentSwitch(kPreNGShaderObjectMetadataEnv) ||
				ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGShaderLookupDiagEnv);
			return enabled;
		}

		bool ShouldCapturePreNGDFLightVanillaDumpBytecode()
		{
			static const bool enabled =
				ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGBSLightingVanillaDumpEnv) ||
				ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGDFLightVanillaDumpEnv) ||
				ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGDFCompositeVanillaDumpEnv);
			return enabled;
		}
#endif
	}

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
				"[LightLimitFix] PreNG targeted vanilla shader bytecode capture active; set {}/{}/{}=0 to hold it after the targeted dump run",
				PreNGEnvironment::kPreNGBSLightingVanillaDumpEnv,
				PreNGEnvironment::kPreNGDFLightVanillaDumpEnv,
				PreNGEnvironment::kPreNGDFCompositeVanillaDumpEnv);
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
}
