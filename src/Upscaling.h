#pragma once

#include <shared_mutex>

#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9 };
const uint depthStencilTargetPatch[] = { 2 };

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

	ID3D11SamplerState* originalSamplerStates[320];
	ID3D11SamplerState* biasedSamplerStates[320];

	ID3D11ShaderResourceView* originalDepthView;

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

	Texture2D* depthOverrideTexture;

	void OverrideRenderTargets();
	void ResetRenderTargets();

	void OverrideDepth();
	void ResetDepth();

	void UpdateSamplerStates(float a_currentMipBias);

	void OverrideSamplerStates();
	void ResetSamplerStates();

	void OverrideLinearDepth();

	UpscaleMethod GetUpscaleMethod(bool a_checkMenu);

	void CheckResources();

	ID3D11ComputeShader* rcas;
	ID3D11ComputeShader* GetRCAS();
	
	ID3D11ComputeShader* dilateMotionVectorCS;
	ID3D11ComputeShader* GetDilateMotionVectorCS();
	
	ID3D11ComputeShader* generateReactiveMaskCS;
	ID3D11ComputeShader* GetGenerateReactiveMaskCS();

	ID3D11ComputeShader* overrideLinearDepthCS;
	ID3D11ComputeShader* GetOverrideLinearDepthCS();

	void GenerateReactiveMask();

	void UpdateJitter();
	void Upscale();

	Texture2D* upscalingTexture;
	Texture2D* dilatedMotionVectorTexture;
	Texture2D* reactiveMaskTexture;

	struct UpscalingDataCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		float4 CameraData;
	};

	ConstantBuffer* upscalingDataCB = nullptr;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	[[nodiscard]] static RE::BSGraphics::State* State_GetSingleton()
	{
		REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID(600795) };
		return singleton.get();
	}

	[[nodiscard]] static RE::BSGraphics::RenderTargetManager* RenderTargetManager_GetSingleton()
	{
		REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID(1508457) };
		return singleton.get();
	}

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

			//auto upscaling = Upscaling::GetSingleton();
			//upscaling->Upscale();
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

			static auto renderTargetManager = RenderTargetManager_GetSingleton();

			upscaling->GenerateReactiveMask();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSDFComposite_Envmap
	{
		static void thunk(void* This, uint a2, bool a3)
		{
			auto upscaling = Upscaling::GetSingleton();
			static auto renderTargetManager = RenderTargetManager_GetSingleton();
			
			upscaling->OverrideRenderTargets();
			upscaling->OverrideDepth();
			func(This, a2, a3);
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		// Control jitters, dynamic resolution, and sampler states
		stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);
		
		// Disable TAA shader
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		
		// Enable dynamic resolution shader if TAA is enabled
		stl::write_vfunc<0x8, ImageSpaceEffectUpsampleDynamicResolution_IsActive>(RE::VTABLE::ImageSpaceEffectUpsampleDynamicResolution[0]);

		// Upscaling pass
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);
		
		// Disable BSGraphics::RenderTargetManager::UpdateDynamicResolution
		REL::Relocation<std::uintptr_t> target{ REL::ID(984743), 0x14B };
		REL::safe_fill(target.address(), 0x90, 5);

		// Control sampler states
		stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(984743).address() + 0x17F);
		stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredDecals>(REL::ID(984743).address() + 0x189);
		stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(984743).address() + 0x1C9);

		// Fix env map with dynamic resolution
		stl::write_thunk_call<BSDFComposite_Envmap>(REL::ID(728427).address() + 0x8DC);
	}
};
