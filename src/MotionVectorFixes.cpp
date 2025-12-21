#include "MotionVectorFixes.h"

struct __declspec(novtable) BSShaderProperty :
	public RE::NiShadeProperty  // 00
{
private:
	static constexpr auto BIT64 = static_cast<std::uint64_t>(1);

public:
	static constexpr auto RTTI{ RE::RTTI::BSShaderProperty };
	static constexpr auto VTABLE{ RE::VTABLE::BSShaderProperty };
	static constexpr auto Ni_RTTI{ RE::Ni_RTTI::BSShaderProperty };

	enum class TextureTypeEnum
	{
		kBase = 0,
		kNormal,
		kGlow,
		kHeight,
		kEnv,
		kWrinkles,
		kMultilayer,
		kBacklightMask,
		kSmoothSpec = kBacklightMask,

		kTotal,
	};

	enum class EShaderPropertyFlag : std::uint64_t
	{
		kSpecular = BIT64 << 0,
		kSkinned = BIT64 << 1,
		kTempRefraction = BIT64 << 2,
		kVertexAlpha = BIT64 << 3,
		kGrayscaleToPaletteColor = BIT64 << 4,
		kGrayscaleToPaletteAlpha = BIT64 << 5,
		kFalloff = BIT64 << 6,
		kEnvMap = BIT64 << 7,
		kRGBFalloff = BIT64 << 8,
		kCastShadows = BIT64 << 9,
		kFace = BIT64 << 10,
		kUIMaskRects = BIT64 << 11,
		kModelSpaceNormals = BIT64 << 12,
		kRefractionClamp = BIT64 << 13,
		kMultiTextureLandscape = BIT64 << 14,
		kRefraction = BIT64 << 15,
		kRefractionFalloff = BIT64 << 16,
		kEyeReflect = BIT64 << 17,
		kHairTint = BIT64 << 18,
		kScreendoorAlphaFade = BIT64 << 19,
		kLocalMapClear = BIT64 << 20,
		kFaceGenRGBTint = BIT64 << 21,
		kOwnEmit = BIT64 << 22,
		kProjectedUV = BIT64 << 23,
		kMultipleTextures = BIT64 << 24,
		kTesselate = BIT64 << 25,
		kDecal = BIT64 << 26,
		kDynamicDecal = BIT64 << 27,
		kCharacterLight = BIT64 << 28,
		kExternalEmittance = BIT64 << 29,
		kSoftEffect = BIT64 << 30,
		kZBufferTest = BIT64 << 31,
		kZBufferWrite = BIT64 << 32,
		kLODLandscape = BIT64 << 33,
		kLODObjects = BIT64 << 34,
		kNoFade = BIT64 << 35,
		kTwoSided = BIT64 << 36,
		kVertexColors = BIT64 << 37,
		kGlowMap = BIT64 << 38,
		kTransformChanged = BIT64 << 39,
		kDismembermentMeatCuff = BIT64 << 40,
		kTint = BIT64 << 41,
		kVertexLighting = BIT64 << 42,
		kUniformScale = BIT64 << 43,
		kFitSlope = BIT64 << 44,
		kBillboard = BIT64 << 45,
		kLODLandBlend = BIT64 << 46,
		kDismemberment = BIT64 << 47,
		kWireframe = BIT64 << 48,
		kWeaponBlood = BIT64 << 49,
		kHideOnLocalMap = BIT64 << 50,
		kPremultAlpha = BIT64 << 51,
		kVATSTarget = BIT64 << 52,
		kAnisotropicLighting = BIT64 << 53,
		kSkewSpecularAlpha = BIT64 << 54,
		kMenuScreen = BIT64 << 55,
		kMultiLayerParallax = BIT64 << 56,
		kAlphaTest = BIT64 << 57,
		kInvertedFadePattern = BIT64 << 58,
		kVATSTargetDrawAll = BIT64 << 59,
		kPipboyScreen = BIT64 << 60,
		kTreeAnim = BIT64 << 61,
		kEffectLighting = BIT64 << 62,
		kRefractionWritesDepth = BIT64 << 63
	};

	class ForEachVisitor;

	class RenderPassArray
	{
	public:
		constexpr RenderPassArray() noexcept {}  // NOLINT(modernize-use-equals-default)

		// members
		RE::BSRenderPass* passList{ nullptr };  // 0
	};
	static_assert(sizeof(RenderPassArray) == 0x8);

	// members
	float                                            alpha;                  // 28
	std::int32_t                                     lastRenderPassState;    // 2C
	std::uint64_t									 flags;                  // 30
	RenderPassArray                                  renderPassList;         // 38
	RenderPassArray                                  debugRenderPassList;    // 40
	RE::BSFadeNode*                                  fadeNode;               // 48
	RE::BSEffectShaderData*                          effectData;             // 50
	RE::BSShaderMaterial*                            material;               // 58
	std::uint32_t                                    lastAccumTime;          // 60
	float                                            lodFade;                // 64
	RE::BSNonReentrantSpinLock                       clearRenderPassesLock;  // 68
};
static_assert(sizeof(BSShaderProperty) == 0x70);


bool isLoadingMenuOpen = false;

class MenuOpenCloserHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static MenuOpenCloserHandler* GetSingleton()
	{
		static MenuOpenCloserHandler singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event.menuName == "LoadingMenu")
			isLoadingMenuOpen = a_event.opening;

		return RE::BSEventNotifyControl::kContinue;
	}
};

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
		BSShaderProperty* This,
		RE::NiAVObject* a2,
		int a3,
		RE::ShadowSceneNode** a4)
	{
		thread_local static auto main = RE::Main::GetSingleton();

		bool frozenTime = main->gameActive && (main->inMenuMode || main->freezeTime);
		bool lodObject = (This->flags & ((uint64_t)BSShaderProperty::EShaderPropertyFlag::kLODObjects | (uint64_t)BSShaderProperty::EShaderPropertyFlag::kLODLandscape | (uint64_t)BSShaderProperty::EShaderPropertyFlag::kLODLandBlend | (uint64_t)BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape));
		
		if (!isLoadingMenuOpen && (frozenTime || lodObject))
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

void MotionVectorFixes::OnDataLoaded()
{
	RE::UI::GetSingleton()->RegisterSink<RE::MenuOpenCloseEvent>(MenuOpenCloserHandler::GetSingleton());
}

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