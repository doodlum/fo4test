#include "MotionVectorFixes.h"

#include <d3d11.h>

enum class RenderTarget
{
	kFrameBuffer = 0,

	kRefractionNormal = 1,
	
	kMainPreAlpha = 2,
	kMain = 3,
	kMainTemp = 4,

	kSSRRaw = 7,
	kSSRBlurred = 8,
	kSSRBlurredExtra = 9,

	kMainVerticalBlur = 14,
	kMainHorizontalBlur = 15,

	kSSRDirection = 10,
	kSSRMask = 11,

	kUI = 17,
	kUITemp = 18,

	kGbufferNormal = 20,
	kGbufferNormalSwap = 21,
	kGbufferAlbedo = 22,
	kGbufferEmissive = 23,
	kGbufferMaterial = 24, //  Glossiness, Specular, Backlighting, SSS

	kSSAO = 28,

	kTAAAccumulation = 26,
	kTAAAccumulationSwap = 27,

	kMotionVectors = 29,

	kUIDownscaled = 36,
	kUIDownscaledComposite = 37,

	kMainDepthMips = 39,

	kUnkMask = 57,

	kSSAOTemp = 48,
	kSSAOTemp2 = 49,
	kSSAOTemp3 = 50,

	kDiffuseBuffer = 58,
	kSpecularBuffer = 59,

	kDownscaledHDR = 64,
	kDownscaledHDRLuminance2 = 65,
	kDownscaledHDRLuminance3 = 66,
	kDownscaledHDRLuminance4 = 67,
	kDownscaledHDRLuminance5Adaptation = 68,
	kDownscaledHDRLuminance6AdaptationSwap = 69,
	kDownscaledHDRLuminance6 = 70,

	kCount = 101
};

thread_local bool fixWeaponModel = false;
thread_local bool fixAnimation = false;

struct BSGeometry_UpdateWorldData
{
	static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_updateData)
	{
		auto prevWorld = a_object->world;

		func(a_object, a_updateData);

		if (fixWeaponModel)
			a_object->world = prevWorld;

		if (fixAnimation)
			a_object->previousWorld = a_object->world;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct PlayerCharacter_UpdateScenegraphNG
{
	static void thunk(float a1, void* a2, __int64 a3, unsigned __int8 a4)
	{
		fixWeaponModel = true;
		func(a1, a2, a3, a4);
		fixWeaponModel = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct PlayerCharacter_UpdateScenegraph
{
	static void thunk(RE::NiAVObject* NiAVObject, RE::NiUpdateData* NiUpdateData)
	{
		fixWeaponModel = true;
		func(NiAVObject, NiUpdateData);
		fixWeaponModel = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESObjectREFR_SetSequencePosition
{
	static void thunk(void* This, RE::NiControllerManager* a2,
		const char* a3,
		float a4)
	{
		fixAnimation = true;
		func(This, a2, a3, a4);
		fixAnimation = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct DrawWorld_DeferredPrePass
{
	static void thunk(void* This)
	{
		func(This);

		if (auto main = RE::Main::GetSingleton()){
			if (main->inMenuMode) {
				auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
				auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

				auto& motionVector = rendererData->renderTargets[(uint32_t)RenderTarget::kMotionVectors];
				FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearRenderTargetView(reinterpret_cast<ID3D11RenderTargetView*>(motionVector.rtView), clearColor);
			}
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void MotionVectorFixes::InstallHooks()
{
#if defined(FALLOUT_POST_NG)
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(2270409));

	stl::write_thunk_call<PlayerCharacter_UpdateScenegraphNG>(REL::ID(2233006).address() + 0x286);

	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(2200766));

	stl::detour_thunk<DrawWorld_DeferredPrePass>(REL::ID(2318301));
#else
	// Fix broken motion vectors from bad geometry updates
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(551661));

	// Fix weapon model world transform getting overwritten
	stl::write_thunk_call<PlayerCharacter_UpdateScenegraph>(REL::ID(978934).address() + 0x2ED);

	// Fix incorrect previous world transform on some animated objects, e.g. doors
	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(854236));

	// Fix vanilla motion vectors not updating in menus
	stl::detour_thunk<DrawWorld_DeferredPrePass>(REL::ID(56596));
#endif

	logger::info("[MotionVectorFixes] Installed hooks");
}