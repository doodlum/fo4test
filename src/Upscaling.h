#pragma once

#include <shared_mutex>

#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

class Upscaling
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
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		float sharpness = 0.0f;
		uint dlssPreset = (uint)sl::DLSSPreset::ePresetK;
	};

	Settings settings;

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* rcasCS;
	ID3D11ComputeShader* GetRCASComputeShader();

	void UpdateJitter();
	void Upscale();

	Texture2D* upscalingTexture;

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
		static bool thunk(struct ImageSpaceEffectTemporalAA*)
		{
			return false;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectUpsampleDynamicResolution_IsActive
	{
		static bool thunk(struct ImageSpaceEffectUpsampleDynamicResolution*)
		{
			return false;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics__RenderTargetManager__UpdateDynamicResolution
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This,
			float*,
			float*,
			float*,
			float*)
		{
			This->dynamicWidthRatio = 0.5;
			This->dynamicHeightRatio = 0.5;

			This->isDynamicResolutionCurrentlyActivated = true;

			This->SetEnableDynamicResolution(true);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
		{
			func(This, a_true);
			GetSingleton()->Upscale();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);

		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

		//stl::write_vfunc<0x8, ImageSpaceEffectUpsampleDynamicResolution_IsActive>(RE::VTABLE::ImageSpaceEffectUpsampleDynamicResolution[0]);

		//stl::detour_thunk<BSGraphics__RenderTargetManager__UpdateDynamicResolution>(REL::ID(1115215)); // 141D31B90
	}
};
