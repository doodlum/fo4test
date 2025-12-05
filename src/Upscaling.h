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
		kNone,
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
		uint qualityMode = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
	};

	Settings settings;

	ID3D11SamplerState* forwardSamplerStates[320];
	ID3D11SamplerState* biasedSamplerStates[320];

	void OverrideSamplerStates();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* rcas;
	ID3D11ComputeShader* GetRCAS();
	
	ID3D11ComputeShader* dilateMotionVectorCS;
	ID3D11ComputeShader* GetDilateMotionVectorCS();

	void UpdateJitter();
	void Upscale();

	void UpdateDynamicResolution(RE::BSGraphics::RenderTargetManager* a_renderTargetManager);

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

	bool enableDynamicResolution = false;
	float currentMipBias = 0.0f;

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
			return GetSingleton()->enableDynamicResolution;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_RenderTargetManager_UpdateDynamicResolution
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This,
			float*,
			float*,
			float*,
			float*)
		{
			GetSingleton()->UpdateDynamicResolution(This);
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

	struct DrawWorld_Imagespace
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This)
		{
			GetSingleton()->enableDynamicResolution = false;
			func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	//static void ApplyJitterToProjection(RE::BSGraphics::CameraStateData& cameraData, float jitterX, float jitterY,
	//	uint32_t renderWidth, uint32_t renderHeight)
	//{
	//	if (!cameraData.useJitter)
	//		return;

	//	// Convert pixel jitter to NDC space [-1, 1]
	//	float jitterNDC_X = (jitterX * 2.0f) / renderWidth;
	//	float jitterNDC_Y = (jitterY * 2.0f) / renderHeight;

	//	// Apply jitter to projection matrix
	//	// projMat[2] contains the third row (m02, m12, m22, m32)
	//	// In DX, these are the z-related terms, but we offset x/y components
	//	__m128 jitterOffset = _mm_set_ps(0.0f, 0.0f, jitterNDC_Y, jitterNDC_X);

	//	// Apply to projection matrix - offset the projection center
	//	cameraData.camViewData.projMat[2] = _mm_add_ps(
	//		cameraData.camViewData.projMat[2],
	//		jitterOffset
	//	);

	//	// Recompute view-projection matrix with jittered projection
	//	for (int row = 0; row < 4; row++) {
	//		__m128 result = _mm_setzero_ps();
	//		for (int i = 0; i < 4; i++) {
	//			__m128 viewRow = cameraData.camViewData.viewMat[i];
	//			__m128 projCol = _mm_set_ps(
	//				cameraData.camViewData.projMat[3].m128_f32[row],
	//				cameraData.camViewData.projMat[2].m128_f32[row],
	//				cameraData.camViewData.projMat[1].m128_f32[row],
	//				cameraData.camViewData.projMat[0].m128_f32[row]
	//			);
	//			result = _mm_add_ps(result, _mm_mul_ps(viewRow, projCol));
	//		}
	//		cameraData.camViewData.viewProjMat[row] = result;
	//	}
	//}

	//struct BSGraphics_State_BuildCameraStateData
	//{
	//	static void thunk(RE::BSGraphics::State* This,
	//		RE::BSGraphics::CameraStateData* a_stateData,
	//		RE::NiCamera* a_camera,
	//		bool a_useJitter)
	//	{
	//		func(This, a_stateData, a_camera, false);

	//		a_stateData->useJitter = a_useJitter;

	//		static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	//		static auto renderTargetManager = RE::BSGraphics::RenderTargetManager::GetSingleton();
	//		auto renderSize = float2(float(gameViewport.screenWidth) * renderTargetManager.dynamicWidthRatio, float(gameViewport.screenHeight) * renderTargetManager.dynamicHeightRatio);

	//		auto renderWidth = static_cast<uint>(renderSize.x);
	//		auto renderHeight = static_cast<uint>(renderSize.y);

	//		ApplyJitterToProjection(&a_stateData,
	//			GetSingleton()->jitter.x,
	//			GetSingleton()->jitter.y,
	//			renderWidth,
	//			renderHeight);
	//	}
	//	static inline REL::Relocation<decltype(thunk)> func;
	//};

	static void InstallHooks()
	{
		stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);
		
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectUpsampleDynamicResolution_IsActive>(RE::VTABLE::ImageSpaceEffectUpsampleDynamicResolution[0]);
		
		stl::detour_thunk<DrawWorld_Imagespace>(REL::ID(587723)); // 141D31B90
		stl::detour_thunk<BSGraphics_RenderTargetManager_UpdateDynamicResolution>(REL::ID(1115215)); // 141D31B90

		//stl::detour_thunk<BSGraphics_State_BuildCameraStateData>(REL::ID(2424)); // 141D31B90
	}
};
