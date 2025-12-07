#pragma once

#include <shared_mutex>

#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

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
		kTAA,
		kFSR,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethodPreference = (uint)UpscaleMethod::kDLSS;
		float sharpness = 0.0f;
		uint dlssPreset = (uint)sl::DLSSPreset::ePresetK;
		uint qualityMode = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
	};

	Settings settings;

	ID3D11SamplerState* originalSamplerStates[320];
	ID3D11SamplerState* biasedSamplerStates[320];

	void LoadSettings();

	void OnDataLoaded();

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

	void UpdateSamplerStates();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* rcas;
	ID3D11ComputeShader* GetRCAS();
	
	ID3D11ComputeShader* dilateMotionVectorCS;
	ID3D11ComputeShader* GetDilateMotionVectorCS();

	void UpdateJitter();
	void Upscale();

	Texture2D* upscalingTexture;
	Texture2D* dilatedMotionVectorTexture;

	float2 resolutionScale = float2(1, 1);

	struct UpscalingDataCB
	{
		float2 trueSamplingDim;  // BufferDim.xy * ResolutionScale
		float2 pad0;
		float4 cameraData;
	};

	ConstantBuffer* upscalingDataCB = nullptr;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	float currentMipBias = 0.0f;

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
		static bool thunk(struct ImageSpaceEffectTemporalAA*)
		{
			return false;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport
	{
		static void thunk(RE::BSGraphics::RenderTargetManager*, bool)
		{
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceManager_RenderEffectRange
	{
		static void thunk(RE::ImageSpaceManager*, uint, uint, uint, uint)
		{
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceManager_RenderEffectRange2
	{
		static void thunk(RE::ImageSpaceManager* This, uint a1, uint a2, uint a3, uint a4)
		{
			auto upscaling = Upscaling::GetSingleton();

			func(This, a1, a2, a3, a4);

			ImageSpaceManager_RenderEffectRange::func(This, 15, 21, 1, 0);

			static auto renderTargetManager = RenderTargetManager_GetSingleton();
			DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);

			upscaling->Upscale();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShaderUtil_SetCameraFOV
	{
		static void thunk(RE::ImageSpaceManager* This, uint a1, uint a2, uint a3, uint a4)
		{
			auto upscaling = Upscaling::GetSingleton();

			func(This, a1, a2, a3, a4);

			ImageSpaceManager_RenderEffectRange::func(This, 15, 21, 1, 0);

			static auto renderTargetManager = RenderTargetManager_GetSingleton();
			DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);

			upscaling->Upscale();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		// Control jitters, dynamic resolution, and sampler states
		stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);
		
		// Disable TAA shader
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		
		// Controls upscaling, and fixes the companion app and pipboy screen to render afterwards
		stl::write_thunk_call<ImageSpaceManager_RenderEffectRange>(REL::ID(587723).address() + 0xD3);
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);
		stl::write_thunk_call<ImageSpaceManager_RenderEffectRange2>(REL::ID(587723).address() + 0x9F);
		
		// Disable BSGraphics::RenderTargetManager::UpdateDynamicResolution
		REL::Relocation<std::uintptr_t> target{ REL::ID(984743), 0x14B };
		REL::safe_fill(target.address(), 0x90, 5);
	}
};
