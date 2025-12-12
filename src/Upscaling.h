#pragma once

#include "Util.h"
#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

#include <array>
#include <memory>
#include <winrt/base.h>

const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61 };

class Upscaling : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kDisabled,
		kFSR,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethodPreference = (uint)UpscaleMethod::kDLSS;
		float sharpness = 0.0f;
		uint qualityMode = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
	};

	Settings settings;

	std::array<winrt::com_ptr<ID3D11SamplerState>, 320> originalSamplerStates;
	std::array<winrt::com_ptr<ID3D11SamplerState>, 320> biasedSamplerStates;

	winrt::com_ptr<ID3D11ShaderResourceView> originalDepthView;

	void LoadSettings();

	void OnDataLoaded();

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

	void UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio);
	void OverrideRenderTarget(int index);
	void ResetRenderTarget(int index);

	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);

	RE::BSGraphics::RenderTarget originalRenderTargets[101];
	RE::BSGraphics::RenderTarget proxyRenderTargets[101];
	RE::BSGraphics::RenderTargetProperties originalRenderTargetData[100];

	std::unique_ptr<Texture2D> depthOverrideTexture;

	void OverrideRenderTargets();
	void ResetRenderTargets();

	void OverrideDepth();
	void ResetDepth();

	void UpdateSamplerStates(float a_currentMipBias);

	void OverrideSamplerStates();
	void ResetSamplerStates();

	void CopyDepth();

	void PatchSSRShader();

	UpscaleMethod GetUpscaleMethod(bool a_checkMenu);

	void CheckResources();

	winrt::com_ptr<ID3D11ComputeShader> rcas;
	ID3D11ComputeShader* GetRCAS();

	winrt::com_ptr<ID3D11ComputeShader> dilateMotionVectorCS;
	ID3D11ComputeShader* GetDilateMotionVectorCS();

	winrt::com_ptr<ID3D11ComputeShader> overrideDepthCS;
	ID3D11ComputeShader* GetOverrideDepthCS();

	winrt::com_ptr<ID3D11PixelShader> BSImagespaceShaderSSLRRaytracing;
	ID3D11PixelShader* GetBSImagespaceShaderSSLRRaytracing();

	ConstantBuffer* GetUpscalingCB();

	void UpdateJitter();
	void Upscale();

	std::unique_ptr<Texture2D> upscalingTexture;
	std::unique_ptr<Texture2D> dilatedMotionVectorTexture;

	struct UpscalingCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		float4 CameraData;
	};

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	struct BSGraphics_State_UpdateTemporalData
	{
		static void thunk(RE::BSGraphics::State* a_state)
		{
			func(a_state);
			GetSingleton()->UpdateJitter();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectTemporalAA_IsActive
	{
		static bool thunk(struct ImageSpaceEffectTemporalAA* This)
		{
			auto upscaleMethod = GetSingleton()->GetUpscaleMethod(true);
			return upscaleMethod == UpscaleMethod::kDisabled && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectUpsampleDynamicResolution_IsActive
	{
		static bool thunk(struct ImageSpaceEffectUpsampleDynamicResolution*)
		{
			auto upscaleMethod = GetSingleton()->GetUpscaleMethod(true);
			return upscaleMethod == UpscaleMethod::kDisabled;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
		{
			func(This, a_true);

			auto upscaling = Upscaling::GetSingleton();
			upscaling->Upscale();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_DeferredPrePass
	{
		static void thunk(struct DrawWorld* This)
		{
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_DeferredDecals
	{
		static void thunk(struct DrawWorld* This)
		{
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_Forward
	{
		static void thunk(struct DrawWorld* This)
		{
			auto upscaling = Upscaling::GetSingleton();

			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();

			auto upscaleMethod = upscaling->GetUpscaleMethod(false);
			auto fidelityFX = FidelityFX::GetSingleton();

			if (upscaleMethod == UpscaleMethod::kFSR)
				fidelityFX->GenerateReactiveMask();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSDFComposite_Envmap
	{
		static void thunk(void* This, uint a2, bool a3)
		{
			auto upscaling = Upscaling::GetSingleton();

			static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
			bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

			if (requiresOverride) {
				upscaling->OverrideRenderTargets();
				upscaling->OverrideDepth();
			}

			func(This, a2, a3);

			if (requiresOverride) {
				upscaling->ResetDepth();
				upscaling->ResetRenderTargets();
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderLensFlare_RenderLensFlare
	{
		static void thunk(RE::NiCamera* a_camera)
		{
			auto upscaling = Upscaling::GetSingleton();

			static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
			bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

			if (requiresOverride) {
				upscaling->OverrideDepth();
			}

			func(a_camera);

			if (requiresOverride) {
				upscaling->ResetDepth();
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique
	{
		static void thunk(RE::BSShader* This, uint a2, uint a3, uint a4, uint a5)
		{
			func(This, a2, a3, a4, a5);
			Upscaling::GetSingleton()->PatchSSRShader();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Forward_ForwardAlphaImpl
	{
		static void thunk(struct DrawWorld* This)
		{
			auto upscaling = Upscaling::GetSingleton();
			auto upscaleMethod = upscaling->GetUpscaleMethod(false);
			auto fidelityFX = FidelityFX::GetSingleton();

			if (upscaleMethod == UpscaleMethod::kFSR)
				fidelityFX->CopyOpaqueTexture();

			func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		// Control jitters, dynamic resolution, and sampler states
		stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);
		
		// Disable TAA shader if using alternative scaling method
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		
		// Disable dynamic resolution shader if using alternative scaling method
		stl::write_vfunc<0x8, ImageSpaceEffectUpsampleDynamicResolution_IsActive>(RE::VTABLE::ImageSpaceEffectUpsampleDynamicResolution[0]);

		// Replace original upscaling pass
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);
		
		// Disable BSGraphics::RenderTargetManager::UpdateDynamicResolution
		REL::Relocation<std::uintptr_t> target{ REL::ID(984743), 0x14B };
		REL::safe_fill(target.address(), 0x90, 5);

		// Control sampler states for mipmap bias
		stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(984743).address() + 0x17F);
		stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredDecals>(REL::ID(984743).address() + 0x189);
		stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(984743).address() + 0x1C9);

		// Fix dynamic resolution for BSDFComposite
		stl::write_thunk_call<BSDFComposite_Envmap>(REL::ID(728427).address() + 0x8DC);

		// Fix dynamic resolution for Lens Flare visibility
		stl::detour_thunk<BSImagespaceShaderLensFlare_RenderLensFlare>(REL::ID(676108));

		// Fix dynamic resolution for Screenspace Reflections
		stl::write_thunk_call<BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique>(REL::ID(779077).address() + 0x1C);

		// Generate reactive mask for FSR
		stl::write_thunk_call<DrawWorld_Forward_ForwardAlphaImpl>(REL::ID(656535).address() + 0x2E8);
	}
};
