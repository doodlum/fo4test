#include "MotionVectorFixes.h"

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

struct BSLightingShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(
		RE::BSLightingShaderProperty* This,
		RE::NiAVObject* a2,
		int a3,
		RE::ShadowSceneNode** a4)
	{
		thread_local static auto main = RE::Main::GetSingleton();
		if (main->gameActive && (main->inMenuMode || main->freezeTime))
			a2->previousWorld = a2->world;
		
		return func(This, a2, a3, a4);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void MotionVectorFixes::InstallHooks()
{
#if defined(FALLOUT_POST_NG)
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(2270409));

	stl::write_thunk_call<PlayerCharacter_UpdateScenegraphNG>(REL::ID(2233006).address() + 0x286);

	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(2200766));
#else
	// Fix broken motion vectors from bad geometry updates
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(551661));

	// Fix weapon model world transform getting overwritten
	stl::write_thunk_call<PlayerCharacter_UpdateScenegraph>(REL::ID(978934).address() + 0x2ED);

	// Fix incorrect previous world transform on some animated objects, e.g. doors
	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(854236));
#endif

	// Fix vanilla motion vectors not updating in menus
	stl::write_vfunc<43, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE::BSLightingShaderProperty[0]);

	logger::info("[MotionVectorFixes] Installed hooks");
}