#include "MotionVectorFixes.h"

inline static void ResetPreviousWorldTransformDownwards(RE::NiAVObject* a_self)
{
	if (a_self == nullptr)
		return;

	if (RE::NiNode* node = fallout_cast<RE::NiNode*>(a_self))
		for (auto& child : node->children)
			ResetPreviousWorldTransformDownwards(child.get());

	a_self->previousWorld = a_self->world;
}

struct TESObjectREFR_SetSequencePosition
{
	static void thunk(RE::NiAVObject* This, RE::NiUpdateData* a_updateData)
	{
		func(This, a_updateData);
		ResetPreviousWorldTransformDownwards(This);
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

inline static void CachePreviousWorldTransformDownwards(RE::NiAVObject* a_self, std::unordered_map<RE::NiAVObject*, RE::NiTransform>& playerWorldCache)
{
	if (a_self == nullptr)
		return;

	if (RE::NiNode* node = fallout_cast<RE::NiNode*>(a_self))
		for (auto& child : node->children)
			CachePreviousWorldTransformDownwards(child.get(), playerWorldCache);

	playerWorldCache.try_emplace(a_self, a_self->world);

}

inline static void SetPreviousWorldTransformDownwards(RE::NiAVObject* a_self, std::unordered_map<RE::NiAVObject*, RE::NiTransform>& playerWorldCache)
{
	if (a_self == nullptr)
		return;

	if (RE::NiNode* node = fallout_cast<RE::NiNode*>(a_self))
		for (auto& child : node->children)
			SetPreviousWorldTransformDownwards(child.get(), playerWorldCache);

	if (auto it = playerWorldCache.find(a_self); it != playerWorldCache.end())
		a_self->previousWorld = it->second;
}

struct OnIdle_UpdatePlayer
{
	static void thunk(RE::Main* This) {
		std::unordered_map<RE::NiAVObject*, RE::NiTransform> playerWorldCache;

		if (auto player = RE::PlayerCharacter::GetSingleton())
			CachePreviousWorldTransformDownwards(player->Get3D(0), playerWorldCache);

		func(This);

		if (auto player = RE::PlayerCharacter::GetSingleton())
			SetPreviousWorldTransformDownwards(player->Get3D(0), playerWorldCache);

		playerWorldCache.clear();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void MotionVectorFixes::InstallHooks()
{
#if defined(FALLOUT_POST_NG)
	stl::detour_thunk<OnIdle_UpdatePlayer>(REL::ID(2228929));

	stl::write_thunk_call<TESObjectREFR_SetSequencePosition>(REL::ID(2200766).address() + 0x1D7);
#else
	// Fix weapon model world transform getting overwritten
	stl::detour_thunk<OnIdle_UpdatePlayer>(REL::ID(1318162));

	// Fix incorrect previous world transform on some animated objects, e.g. doors
	stl::write_thunk_call<TESObjectREFR_SetSequencePosition>(REL::ID(854236).address() + 0x1D7);
#endif

	// Fix vanilla motion vectors not updating in menus or when time is frozen
	stl::write_vfunc<43, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE::BSLightingShaderProperty[0]);

	logger::info("[MotionVectorFixes] Installed hooks");
}