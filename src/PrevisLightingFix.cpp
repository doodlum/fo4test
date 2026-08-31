// VirtualProtect/FlushInstructionCache for the no-skip byte patch.  NOGDI
// keeps windows.h from defining an ERROR macro over REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#ifdef ERROR
#	undef ERROR
#endif

#include "PCH.h"

#include "PrevisLightingFix.h"

namespace
{
	// REL::ID(2318289) is the world-draw function that chooses between the
	// scene-graph traversal and the precomputed previs list.  +0x192 is the
	// `call` that accumulates the previs batch; rcx holds that batch.
	constexpr std::uint64_t  kDrawWorldID = 2318289;
	constexpr std::ptrdiff_t kAccumulateCallOffset = 0x192;

	// Control byte selecting the per-object lighting path, and the batch
	// field the "set" path forwards.
	constexpr std::ptrdiff_t kLightingPathFlag = 0x169;

	// The sole call into the function that branches on the flag.
	constexpr std::uint64_t  kConsumerCallerID = 2275918;
	constexpr std::ptrdiff_t kConsumerCallOffset = 0x2e2;

	// The batch globals and the previs-activity check the redirect keys on.
	constexpr std::uint64_t  kParam2BatchID = 4784648;         // 0x142F97C40
	constexpr std::uint64_t  kPrevisBatchID = 4784667;         // 0x142F98090
	// Verified against the address library: rva 0x21ae520 -> EXACT 2317322.
	// (2317355, the first value used here, resolves to an unrelated function;
	// calling it as the gate was what crashed and hung the redirect runs.)
	constexpr std::uint64_t  kIsPreCullingActiveID = 2317322;  // 0x1421AE520

	// DrawWorld's child loop routes each object by objFlags bit 40: clear ->
	// accumulate into the param_2 batch, set -> the traversal batch.  This is
	// the (sole) call that accumulates the bit-40-clear objects:
	//
	//     lea  rcx, [rip+...]      ; 0x142F97C40, the param_2 batch
	//     xor  r9d, r9d
	//     lea  r8, [rdx+0xB0]      ; the object's world bound
	//     call FUN_1417e0030       ; +0x328
	constexpr std::ptrdiff_t kChildAccumulateCall = 0x328;

	// The engine's frame counter (the same one the harness samples for fps).
	constexpr std::uint64_t kFrameCounterID = 4784456;

	// The traversal root global (0x143e5df60) and the traversal batch
	// (0x142f97db0) -- the batch that is submitted whether previs is on or
	// off, which is what makes it the safe carrier for the light entries.
	constexpr std::uint64_t kTraversalRootID = 2712479;
	constexpr std::uint64_t kTraversalBatchID = 4784655;

	// The engine's light-to-subtree attach: rva 0x21bafb0 -> EXACT 2317474
	// (rva2id.py).  The camera pre-pass calls it as (mainLight, root, 1);
	// FUN_1421bb030 underneath recurses the subtree culled by the light's
	// bound and refreshes each intersecting BSFadeNode::lightData (+0x140),
	// which BSLightingShaderProperty::GetRenderPasses reads its per-pass
	// lights from (via 0x142240540: slot 0 = sun, then the node's list).
	constexpr std::uint64_t kAttachLightID = 2317474;
	using AttachLight_t = std::uint64_t (*)(void*, void*, std::uint32_t);

	// The per-node fade + light-attach update (0x14217d450; EXACT id from
	// callers.py).  The previs-off batch consumer calls it per accumulated
	// entry with the camera context stored at batch+0x160; the previs branch
	// never calls it, which is the source of the stale per-node light lists.
	constexpr std::uint64_t  kNodeUpdateID = 2316460;

	// The light-into-node attach the node update guards (0x1421bb030 -> EXACT
	// 2317475), the scene-root array it scans (2712479 = 0x143e5df60) and the
	// active-root index byte (2712516 = 0x143e5e009).  The engine runs this
	// ONCE per node, at fade-in:
	//     if (!bit51(node) && (bit33(node) || rootDirty || mid-fade))
	//         attach(node, root[idx], 0, 0, radius < 150, flag);
	// A node faded in under previs never takes the mid-fade branch (the batch
	// consumer that drives it only sees previs's 23 entries), so its
	// lightData stays empty from load onward -- the source of the bug.
	constexpr std::uint64_t kLightIntoNodeID = 2317475;
	// The renderer's accumulation stamp (0x143e5dfa0 -> EXACT 2712486);
	// RegisterObject writes it into prop->lastAccumTime, and the forward
	// pass builder rebuilds a property's cached passes only when
	// lastAccumTime <= lightData.lightListChanged.  Bumping the node's
	// lightListChanged to this stamp is therefore the engine's own "light
	// list changed, rebuild passes" signal -- the piece the bare attach was
	// missing (passes stayed cached with stale lights, so attaching alone
	// changed nothing on screen).
	constexpr std::uint64_t  kAccumStampID = 2712486;

	// Light-vs-bound intersection used by the engine's own gather
	// (0x1421fe3e0 -> EXACT 2318424, rva2id.py): (BSLight*, const NiBound*,
	// NiAVObject* lightObj, float scale) -> bool.
	constexpr std::uint64_t kLightIntersectID = 2318424;
	using LightIntersect_t = bool (*)(void*, const float*, void*, float);
	constexpr std::ptrdiff_t kLightListChanged = 0x148;
	constexpr std::uint64_t kRootIndexID = 2712516;
	using LightIntoNode_t =
		void (*)(void*, void*, std::uint32_t, std::uint32_t, bool, std::uint8_t);
	constexpr std::ptrdiff_t kBatchCamContext = 0x160;
	using NodeUpdate_t = void (*)(void*, void*, std::uint8_t);

	// BSFadeNode's vtable (IDs_VTABLE.h) and the OnVisible slot, the same
	// slot VisibilityProbe patches on the geometry classes.
	constexpr std::uint64_t  kBSFadeNodeVtblID = 93613;  // IDs_VTABLE.h, verified
	constexpr std::ptrdiff_t kOnVisibleSlot = 0x1C8;

	// The per-object batch append, REL::ID(2275908) = 0x1417deb90 (verified
	// with rva2id.py).  It computes
	//     bVar11 = IsPreCullingActive() && page[0x3a6f] == 0
	// and uses it twice: once for the entry's marker byte, and once at +0x121
	//     0x1417decae  test r10b, r10b
	//     0x1417decb1  jne  +0x155          ; 0F 85 55 01 00 00
	//     0x1417decb7  call [rax+0x40]      ; GetLight -> type 0xf ->
	//                                       ; append per-affected-node records
	// to skip the light-record block whenever previs is active.  Those records
	// are the light<->geometry association the transparent shader needs; with
	// previs on they are never written, which is the bug.  NOPing only this
	// branch appends them unconditionally while the marker byte -- whose
	// previs-on value rendering depends on (forcing it previs-off broke the
	// whole frame at 571 fps) -- keeps its vanilla semantics.
	constexpr std::uint64_t  kAppendFnID = 2275908;
	constexpr std::ptrdiff_t kLightSkipBranch = 0x121;
	constexpr std::uint8_t   kLightSkipBytes[6] = { 0x0F, 0x85, 0x55, 0x01, 0x00, 0x00 };

	// DrawWorld's previs-gated child skip: `jne <next child>`, 2 bytes.
	constexpr std::uint64_t  kDrawWorldMainID = 2318292;
	constexpr std::ptrdiff_t kSkipJumpOffset = 0x315;
	constexpr std::uint8_t   kSkipJump[2] = { 0x75, 0x16 };  // jne +0x16
	constexpr std::uint8_t   kNops[2] = { 0x90, 0x90 };

	using Accumulate_t = void (*)(std::uint8_t*, void*, void*, std::uint64_t);
	using Consume_t = void (*)(std::uint8_t*, void*, void*);

	Accumulate_t         g_originalChildAccumulate{ nullptr };
	Accumulate_t         g_originalPrevisAccumulate{ nullptr };
	std::atomic_uint64_t g_redirected{ 0 };

	// Frame number of the most recent previs-root accumulation, i.e. the last
	// frame on which the engine itself reset, filled, and will submit the
	// previs batch.  ~0 means never.
	std::atomic_uint64_t g_previsPreppedFrame{ ~0ull };

	// kWrap* diagnostics: when false the redirect gate never diverts, making
	// both hooks pure pass-through wrappers.
	bool g_divert{ true };

	// Diversion is armed explicitly (SetWorldReady) and dropped on
	// kPreLoadGame.  Every automatic arming condition tried so far -- previs
	// prep having run this frame, kPostLoadGame, the player having a parent
	// cell -- turned out to become true while the save load is still in
	// flight, and diverting in that window reliably breaks the load (the
	// player never finishes attaching to the cell).  The harness arms it once
	// its own load-settled check passes; whether steady-state diversion is
	// safe is exactly what that measures.
	std::atomic_bool g_worldReady{ false };


	[[nodiscard]] std::uint64_t CurrentFrame()
	{
		static REL::Relocation<std::uint32_t*> counter{ REL::ID(kFrameCounterID) };
		return *counter;
	}

	// Chaining hook on the previs branch's own accumulate (REL::ID(2318289)
	// +0x192).  Its only job is to record that the previs batch is live this
	// frame; the original always runs.
	// ---- kLightScoped ----------------------------------------------------

	std::atomic_uint64_t g_lightsAccumulated{ 0 };

	// NiAVObject basics, laid out as the walk in FUN_1421f1010 reads them.
	constexpr std::ptrdiff_t kChildArray = 0x128;   // NiPointer<NiAVObject>[]
	constexpr std::ptrdiff_t kChildCount = 0x132;   // std::uint16_t
	constexpr std::ptrdiff_t kObjFlags = 0x108;     // bit 0 = culled
	constexpr std::ptrdiff_t kWorldBound = 0xB0;

	[[nodiscard]] void* GetLightOf(std::uint8_t* a_node)
	{
		// vtable slot +0x40 -- the same virtual the engine's append uses to
		// route light objects.  Mid-teardown nodes can carry garbage vtable
		// pointers (measured: called one, crashed in heap), so refuse any
		// vtable outside the game module.
		const auto vtbl = *reinterpret_cast<std::uintptr_t*>(a_node);
		static const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		if (vtbl <= base || vtbl - base >= 0x10000000) {
			return nullptr;
		}
		const auto fn = *reinterpret_cast<void* (**)(std::uint8_t*)>(vtbl + 0x40);
		return fn(a_node);
	}

	// Depth-limited walk accumulating light-carrying nodes into a_batch.
	void AccumulateLights(std::uint8_t* a_batch, std::uint8_t* a_node, int a_depth,
		int& a_budget)
	{
		if (!a_node || a_depth > 6 || a_budget <= 0) {
			return;
		}
		if (*reinterpret_cast<std::uint64_t*>(a_node + kObjFlags) & 1) {
			return;  // culled subtree
		}

		if (GetLightOf(a_node)) {
			static REL::Relocation<Accumulate_t> accumulate{ REL::ID(2275917) };
			accumulate(a_batch, a_node, a_node + kWorldBound, 0);
			--a_budget;
			g_lightsAccumulated.fetch_add(1, std::memory_order_relaxed);
			return;  // a light is a leaf for our purposes
		}

		const auto count = *reinterpret_cast<std::uint16_t*>(a_node + kChildCount);
		auto* children = *reinterpret_cast<std::uint8_t***>(a_node + kChildArray);
		if (!children) {
			return;
		}
		for (std::uint16_t i = 0; i < count && a_budget > 0; ++i) {
			AccumulateLights(a_batch, children[i], a_depth + 1, a_budget);
		}
	}


	bool g_lightScoped{ false };
	bool g_attachLights{ false };
	bool g_nodeUpdateDiag{ false };

	// ---- kPackedLights ----------------------------------------------------
	//
	// Proxy "fade node" for combined-mesh properties.  Only the fields the
	// forward pass builder (FUN_14217aea0) and GetRenderPasses actually read
	// exist meaningfully; everything else stays zero.
	struct ProxyFadeNode
	{
		std::uint8_t   pad000[0x108]{};
		std::uint64_t  flags{ 1ull << 15 };       // 108: builder visibility bit
		std::uint8_t   pad110[0x0C]{};            // 110
		std::uint8_t   byte11C{ 0 };              // 11C: != 6 -> plain path
		std::uint8_t   pad11D{ 0 };               // 11D
		std::uint8_t   byte11E{ 3 };              // 11E: LOD mode default
		std::uint8_t   pad11F[0x21]{};            // 11F
		std::uint32_t  lightListFence{ 0 };       // 140
		std::uint32_t  shadowAccumFlags{ 0 };     // 144
		std::uint32_t  lightListChanged{ 0 };     // 148
		std::uint32_t  pad14C{ 0 };               // 14C
		void*          lightListData{ nullptr };  // 150
		std::uint32_t  lightListCapacity{ 0 };    // 158
		std::uint32_t  pad15C{ 0 };               // 15C
		std::uint32_t  lightListCount{ 0 };       // 160
		std::uint8_t   pad164[0x3C]{};            // 164
		float          currentFade{ 1.0f };       // 1A0
		float          currentDecalFade{ 1.0f };  // 1A4
		std::uint8_t   pad1A8[0x10]{};            // 1A8
		float          posX{ 0.0f };              // 1B8
		float          posY{ 0.0f };              // 1BC
		std::uint8_t   tail[0x40]{};              // 1C0 slack

		// our storage for the light list
		void* lights[16]{};
	};
	static_assert(offsetof(ProxyFadeNode, flags) == 0x108);
	static_assert(offsetof(ProxyFadeNode, lightListFence) == 0x140);
	static_assert(offsetof(ProxyFadeNode, lightListData) == 0x150);
	static_assert(offsetof(ProxyFadeNode, lightListCount) == 0x160);
	static_assert(offsetof(ProxyFadeNode, currentFade) == 0x1A0);
	static_assert(offsetof(ProxyFadeNode, posX) == 0x1B8);

	bool g_packedLights{ false };
	std::atomic_uint64_t g_proxiesMade{ 0 };

	// prop -> proxy, small open-addressed map; cleared on load.
	constexpr std::uint32_t kProxySlots = 1024;
	std::array<std::atomic_uintptr_t, kProxySlots> g_proxyProps{};
	std::array<ProxyFadeNode*, kProxySlots>        g_proxies{};

	void ClearProxies()
	{
		for (std::uint32_t i = 0; i < kProxySlots; ++i) {
			g_proxyProps[i].store(0, std::memory_order_relaxed);
			delete g_proxies[i];
			g_proxies[i] = nullptr;
		}
	}

	std::atomic_uint32_t g_dbgCalls{ 0 }, g_dbgNoProp{ 0 }, g_dbgHasFade{ 0 },
		g_dbgNoRoot{ 0 }, g_dbgNoLights{ 0 }, g_dbgNoHit{ 0 };

	void DumpPackedDebug()
	{
		REX::INFO("packed-lights debug: calls={} noProp={} hasFade={} noRoot={} "
				  "noRegistry={} noIntersect={} proxies={}",
			g_dbgCalls.load(), g_dbgNoProp.load(), g_dbgHasFade.load(),
			g_dbgNoRoot.load(), g_dbgNoLights.load(), g_dbgNoHit.load(),
			g_proxiesMade.load());
	}

	void MaybeGivePackedLights(std::uint8_t* a_geometry)
	{
		g_dbgCalls.fetch_add(1, std::memory_order_relaxed);
		auto* prop = *reinterpret_cast<std::uint8_t**>(a_geometry + 0x138);
		if (!prop) {
			g_dbgNoProp.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		// Only properties with no fade node -- the combined-mesh case.
		if (*reinterpret_cast<std::uintptr_t*>(prop + 0x48) != 0) {
			g_dbgHasFade.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const auto key = reinterpret_cast<std::uintptr_t>(prop);
		auto idx = (key >> 4) & (kProxySlots - 1);
		for (std::uint32_t probe = 0; probe < 8; ++probe) {
			const auto cur = g_proxyProps[idx].load(std::memory_order_relaxed);
			if (cur == key) {
				return;  // already proxied
			}
			if (cur == 0) {
				break;
			}
			idx = (idx + 1) & (kProxySlots - 1);
		}
		if (g_proxyProps[idx].load(std::memory_order_relaxed) != 0) {
			return;  // table full in this neighbourhood
		}

		// Gather lights intersecting the GEOMETRY's world bound from the
		// scene root's light registry, with the engine's own test.
		static const auto rootArray =
			REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalRootID) }.address();
		static const auto rootIndex = reinterpret_cast<const std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kRootIndexID) }.address());
		const auto root =
			*reinterpret_cast<std::uint8_t**>(rootArray + *rootIndex * 8);
		if (!root) {
			g_dbgNoRoot.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		auto** lights = *reinterpret_cast<void***>(root + 0x158);
		const auto lightCount =
			*reinterpret_cast<const std::uint32_t*>(root + 0x168);
		if (!lights || lightCount == 0) {
			g_dbgNoLights.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		auto proxy = std::make_unique<ProxyFadeNode>();
		const auto* bound = reinterpret_cast<const float*>(a_geometry + 0xB0);
		proxy->posX = bound[0];
		proxy->posY = bound[1];

		static REL::Relocation<LightIntersect_t> intersects{
			REL::ID(kLightIntersectID)
		};
		std::uint32_t got = 0;
		for (std::uint32_t i = 0; i < lightCount && got < 16; ++i) {
			auto* light = lights[i];
			if (!light) {
				continue;
			}
			auto* lightObj = *reinterpret_cast<void**>(
				reinterpret_cast<std::uint8_t*>(light) + 0xB8);
			if (!lightObj) {
				continue;
			}
			if (intersects(light, bound, lightObj, 1.0f)) {
				proxy->lights[got++] = light;
			}
		}
		if (got == 0) {
			g_dbgNoHit.fetch_add(1, std::memory_order_relaxed);
			return;  // nothing would change; keep the property untouched
		}
		proxy->lightListData = proxy->lights;
		proxy->lightListCapacity = 16;
		proxy->lightListCount = got;
		static const auto stamp = reinterpret_cast<const std::uint32_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kAccumStampID) }.address());
		proxy->lightListChanged = *stamp;

		// Publish: property gets its light source.
		*reinterpret_cast<std::uintptr_t*>(prop + 0x48) =
			reinterpret_cast<std::uintptr_t>(proxy.get());
		g_proxies[idx] = proxy.release();
		g_proxyProps[idx].store(key, std::memory_order_relaxed);
		const auto n = g_proxiesMade.fetch_add(1, std::memory_order_relaxed);
		if (n == 0 || ((n + 1) & 0xF) == 0) {
			REX::INFO("packed-lights proxy #{} (prop {:#x}, {} light(s))", n + 1,
				key, got);
		}
	}

	constexpr std::uint32_t kSeenSize = 4096;  // power of two
	std::array<std::atomic_uintptr_t, kSeenSize> g_seenNodes{};

	using OnVisible_t = void (*)(void*, void*);
	OnVisible_t          g_originalOnVisible{ nullptr };
	OnVisible_t          g_originalGeomOnVisible{ nullptr };

	void GeomOnVisibleHook(void* a_geometry, void* a_cullingProcess)
	{
		g_originalGeomOnVisible(a_geometry, a_cullingProcess);
		if (!g_packedLights) {
			return;
		}
		{
			static std::atomic_uint32_t lastFrame{ 0xFFFFFFFFu };
			static std::atomic_bool     inWorld{ false };
			const auto f = CurrentFrame();
			if (lastFrame.exchange(static_cast<std::uint32_t>(f),
					std::memory_order_relaxed) != f) {
				const auto* player = RE::PlayerCharacter::GetSingleton();
				inWorld.store(player && player->GetParentCell() != nullptr,
					std::memory_order_relaxed);
			}
			if (!inWorld.load(std::memory_order_relaxed)) {
				return;
			}
		}
		MaybeGivePackedLights(reinterpret_cast<std::uint8_t*>(a_geometry));
	}
	std::atomic_uint64_t g_nodeUpdates{ 0 };

	void FadeNodeOnVisibleHook(void* a_node, void* a_cullingProcess)
	{
		g_originalOnVisible(a_node, a_cullingProcess);

		// Self-contained world gate (no harness arming): the hook receives
		// only nodes the engine is actively processing, and the attach is
		// the engine's own load-safe routine, so a cheap frame-cached
		// player-in-cell check suffices.
		{
			static std::atomic_uint32_t lastFrame{ 0xFFFFFFFFu };
			static std::atomic_bool     inWorld{ false };
			const auto f = CurrentFrame();
			if (lastFrame.exchange(static_cast<std::uint32_t>(f),
					std::memory_order_relaxed) != f) {
				const auto* player = RE::PlayerCharacter::GetSingleton();
				inWorld.store(player && player->GetParentCell() != nullptr,
					std::memory_order_relaxed);
			}
			if (!inWorld.load(std::memory_order_relaxed)) {
				return;
			}
		}
		static REL::Relocation<bool (*)()> isActive{ REL::ID(kIsPreCullingActiveID) };
		if (!isActive()) {
			return;  // previs off: the engine already runs this via the batch
		}
		static const auto previsBatch = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kPrevisBatchID) }.address());
		auto* context = *reinterpret_cast<void**>(previsBatch + kBatchCamContext);
		if (!context) {
			return;
		}
		// Skip nodes the engine itself attached (its latch, bit 51) -- and
		// do NOT write that latch: earlier builds that wrote it blocked the
		// previs-off path's own repair, which made tpc a no-op and poisoned
		// the in-run validation metric.
		const auto flags = *reinterpret_cast<const std::uint64_t*>(
			reinterpret_cast<std::uint8_t*>(a_node) + 0x108);
		if ((flags >> 51) & 1) {
			return;
		}

		// One-shot per node per world, tracked in our own table.
		{
			const auto key = reinterpret_cast<std::uintptr_t>(a_node);
			auto idx = (key >> 4) & (kSeenSize - 1);
			for (std::uint32_t probe = 0; probe < 8; ++probe) {
				const auto cur = g_seenNodes[idx].load(std::memory_order_relaxed);
				if (cur == key) {
					return;
				}
				if (cur == 0) {
					g_seenNodes[idx].store(key, std::memory_order_relaxed);
					break;
				}
				idx = (idx + 1) & (kSeenSize - 1);
			}
		}

		static const auto rootArray =
			REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalRootID) }.address();
		static const auto rootIndex = reinterpret_cast<const std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kRootIndexID) }.address());
		const auto root = *reinterpret_cast<void**>(rootArray + *rootIndex * 8);
		if (!root) {
			return;
		}

		// Reproduce the tpc repair exactly: the toggle fixes the glass by
		// running the per-node update (REL::ID(2316460)) with its light
		// guard passing.  Calling the attach directly (with or without a
		// pass-cache bump) measured broken both times -- d450's surrounding
		// property/fade work is part of the repair.  So: set the node's
		// "needs light update" flag (bit 33, the guard's first clause) and
		// run the engine's own update with the batch camera context.
		*reinterpret_cast<std::uint64_t*>(
			reinterpret_cast<std::uint8_t*>(a_node) + 0x108) |= 1ull << 33;
		static REL::Relocation<NodeUpdate_t> update{ REL::ID(kNodeUpdateID) };
		update(a_node, context, 1);
		(void)root;

		const auto n = g_nodeUpdates.fetch_add(1, std::memory_order_relaxed);
		if (n == 0 || ((n + 1) & 0x1F) == 0) {
			REX::INFO("node update #{} (node {:#x})", n + 1,
				reinterpret_cast<std::uintptr_t>(a_node));
		}
	}

	// kAttachLights: refresh fadeNode light lists from the previs batch's
	// own light entries.  Runs in the prep hook, i.e. inside the previs
	// branch, after the engine's own accumulate.
	void AttachBatchLights()
	{
		static const auto previsBatch = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kPrevisBatchID) }.address());
		static const auto root = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalRootID) }.address());
		static REL::Relocation<AttachLight_t> attach{ REL::ID(kAttachLightID) };

		auto* buffer = *reinterpret_cast<std::uint8_t**>(previsBatch + 0xD0);
		if (!buffer) {
			return;
		}
		const auto count = std::min<std::uint32_t>(
			*reinterpret_cast<const std::uint32_t*>(buffer + 0x3A68), 64);
		const auto* entries =
			reinterpret_cast<std::uint8_t* const*>(buffer + 0x2060);
		std::uint32_t attached = 0;
		for (std::uint32_t i = 0; i < count && attached < 24; ++i) {
			auto* obj = entries[i];
			if (!obj) {
				continue;
			}
			// The previs append never routes these through GetLight, and
			// NiPointLight's +0x40 virtual returns null (measured: zero
			// lights found that way) -- identify lights by exact vtable.
			static const auto niPointLightVtbl =
				REL::Relocation<std::uintptr_t>{ REL::ID(496353) }.address();
			if (*reinterpret_cast<const std::uintptr_t*>(obj) != niPointLightVtbl) {
				continue;
			}
			// The engine wrapper (2317474) forwards a payload from the
			// light's +0x30 virtual only when NiAVObject flag bit 14 is set,
			// and the recursion dereferences it unconditionally (calling it
			// with the payload null crashed at +0x11).  Only attach lights
			// the engine itself would.
			const auto flags =
				*reinterpret_cast<const std::uint64_t*>(obj + 0x108);
			if (((flags >> 14) & 1) == 0) {
				continue;
			}
			const auto vtbl = *reinterpret_cast<std::uintptr_t*>(obj);
			const auto getPayload =
				*reinterpret_cast<void* (**)(std::uint8_t*)>(vtbl + 0x30);
			if (!getPayload(obj)) {
				continue;
			}
			attach(obj, root, 1);
			++attached;
		}
		if (attached) {
			g_lightsAccumulated.fetch_add(attached, std::memory_order_relaxed);
		}
	}

	void NotePrevisPrep(std::uint8_t* a_batch, void* a_object, void* a_bound,
		std::uint64_t a_flags)
	{
		const auto frame = CurrentFrame();
		if (g_previsPreppedFrame.exchange(frame, std::memory_order_relaxed) == ~0ull) {
			REX::INFO("previs prep first seen on frame {} (root {:#x})", frame,
				reinterpret_cast<std::uintptr_t>(a_object));
		}
		g_originalPrevisAccumulate(a_batch, a_object, a_bound, a_flags);

		// v2 diagnostics: the scene-root light registry (root+0x158 array,
		// +0x168 count) is what the fade-in attach scans; whether previs-on
		// leaves it empty is the deciding question for the root-cause fix.
		if (g_nodeUpdateDiag) {
			static const auto rootArray =
				REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalRootID) }.address();
			static const auto rootIndex = reinterpret_cast<const std::uint8_t*>(
				REL::Relocation<std::uintptr_t>{ REL::ID(kRootIndexID) }.address());
			const auto idx = *rootIndex;
			const auto root = *reinterpret_cast<std::uint8_t**>(rootArray + idx * 8);
			if (root) {
				const auto count =
					*reinterpret_cast<const std::uint32_t*>(root + 0x168);
				static std::atomic_uint32_t lastLogged{ 0xFFFFFFFFu };
				if (lastLogged.exchange(count, std::memory_order_relaxed) != count) {
					static REL::Relocation<bool (*)()> isActive{
						REL::ID(kIsPreCullingActiveID)
					};
					REX::INFO("root[{}]={:#x} lightRegistry count={} (previs={})",
						idx, reinterpret_cast<std::uintptr_t>(root), count,
						isActive());
				}
			}
		}

		if (!g_lightScoped && !g_attachLights) {
			return;
		}
		// Only touch a live world.  A parent-cell check is NOT enough: the
		// player gets a cell early in save deserialisation while the scene
		// is still half-built (walking it then crashed on a garbage vtable).
		// g_worldReady is armed explicitly once the world has settled and
		// dropped on kPreLoadGame.
		if (!g_worldReady.load(std::memory_order_relaxed)) {
			return;
		}
		if (g_attachLights) {
			const auto before = g_lightsAccumulated.load(std::memory_order_relaxed);
			AttachBatchLights();
			const auto added =
				g_lightsAccumulated.load(std::memory_order_relaxed) - before;
			static std::atomic_bool logged{ false };
			if (added && !logged.exchange(true)) {
				REX::INFO("attach-lights: first frame attached {} light(s)", added);
			}
			return;
		}

		static const auto traversalBatch = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalBatchID) }.address());
		static const auto root = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kTraversalRootID) }.address());
		int budget = 256;
		const auto before = g_lightsAccumulated.load(std::memory_order_relaxed);
		AccumulateLights(traversalBatch, root, 0, budget);
		const auto added = g_lightsAccumulated.load(std::memory_order_relaxed) - before;
		static std::atomic_bool loggedOnce{ false };
		if (added && !loggedOnce.exchange(true)) {
			REX::INFO("light-scoped: first frame accumulated {} light node(s)", added);
		}
	}

	// While previs is active, objects the child loop would file into the
	// param_2 batch -- which is never submitted with previs on -- go into the
	// previs batch instead, through the engine's own accumulate.  A second
	// submit of the stranded batch is NOT an option: the submit's chunk walk
	// terminates only on exact pointer equality with its end sentinel, so a
	// batch in any state it does not expect loops forever (measured; froze
	// the game).  Appending to the batch that is already submitted keeps one
	// submit of one batch and every invariant the walk depends on.
	void RedirectChildAccumulate(std::uint8_t* a_batch, void* a_object,
		void* a_bound, std::uint64_t a_flags)
	{
		// The globals ARE the batches; the call site passes their addresses.
		static const auto param2 = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kParam2BatchID) }.address());
		static const auto previs = reinterpret_cast<std::uint8_t*>(
			REL::Relocation<std::uintptr_t>{ REL::ID(kPrevisBatchID) }.address());
		static REL::Relocation<bool (*)()> isActive{ REL::ID(kIsPreCullingActiveID) };

		// Redirect only on frames where the engine has already prepared the
		// previs batch (2318292 calls 2318289 -- where that happens -- before
		// this child loop, on the same thread).  During loading screens previs
		// reads as active but the previs branch never runs, so an ungated
		// redirect appends to a batch that is never reset and never submitted,
		// growing it every frame until the game locks up.  Measured; this gate
		// is what stops it.
		if (g_divert && g_worldReady.load(std::memory_order_relaxed) &&
			a_batch == param2 && isActive() &&
			g_previsPreppedFrame.load(std::memory_order_relaxed) == CurrentFrame()) {
			a_batch = previs;
			const auto n = g_redirected.fetch_add(1, std::memory_order_relaxed);
			if (n == 0) {
				REX::INFO("first redirect on frame {} (object {:#x})", CurrentFrame(),
					reinterpret_cast<std::uintptr_t>(a_object));
			} else if ((n & 0xFFF) == 0) {
				REX::INFO("redirects: {}", n);
			}
		}
		g_originalChildAccumulate(a_batch, a_object, a_bound, a_flags);
	}

	Accumulate_t          g_original{ nullptr };
	Consume_t             g_originalConsume{ nullptr };
	std::atomic_uint64_t  g_hits{ 0 };
	bool                  g_installed{ false };

	void ConsumePrevisBatch(std::uint8_t* a_batch, void* a_buffer, void* a_arg)
	{
		if (a_batch) {
			a_batch[kLightingPathFlag] = 1;
			g_hits.fetch_add(1, std::memory_order_relaxed);
		}
		g_originalConsume(a_batch, a_buffer, a_arg);
	}

	void AccumulatePrevisBatch(std::uint8_t* a_batch, void* a_node, void* a_end,
		std::uint64_t a_flags)
	{
		if (a_batch) {
			// The traversal batch always has this set; the previs batch is
			// rebuilt each frame without it.  Setting it here -- immediately
			// before the accumulation that reads it -- is what puts previs
			// geometry back on the same lighting path.
			a_batch[kLightingPathFlag] = 1;
			g_hits.fetch_add(1, std::memory_order_relaxed);
		}

		g_original(a_batch, a_node, a_end, a_flags);
	}
}

namespace PrevisLightingFix
{
	bool Installed() noexcept { return g_installed; }

	void SetWorldReady(bool a_ready) noexcept
	{
		g_worldReady.store(a_ready, std::memory_order_relaxed);
		if (!a_ready) {
			for (auto& slot : g_seenNodes) {
				slot.store(0, std::memory_order_relaxed);
			}
			// Proxies reference dying scene objects; drop them with the
			// world.  (The properties pointing at them die too.)
			ClearProxies();
		}
	}

	std::uint64_t HitCount() noexcept
	{
		if (g_packedLights) {
			DumpPackedDebug();
			return g_proxiesMade.load(std::memory_order_relaxed);
		}
		const auto hits = g_hits.load(std::memory_order_relaxed);
		if (hits) {
			return hits;
		}
		const auto lights = g_lightsAccumulated.load(std::memory_order_relaxed);
		if (lights) {
			return lights;
		}
		const auto updates = g_nodeUpdates.load(std::memory_order_relaxed);
		return updates ? updates : g_redirected.load(std::memory_order_relaxed);
	}

	bool Install(Mode a_mode)
	{
		if (g_installed || a_mode == Mode::kOff) {
			return g_installed;
		}

		if (a_mode == Mode::kLightRecords) {
			const REL::Relocation<std::uintptr_t> append{ REL::ID(kAppendFnID) };
			const auto site = append.address() + kLightSkipBranch;
			if (std::memcmp(reinterpret_cast<const void*>(site), kLightSkipBytes,
					sizeof(kLightSkipBytes)) != 0) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is not the "
						   "expected jne; refusing to patch",
					kAppendFnID, kLightSkipBranch);
				return false;
			}

			constexpr std::uint8_t nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
			DWORD old{};
			if (!VirtualProtect(reinterpret_cast<void*>(site), sizeof(nops),
					PAGE_EXECUTE_READWRITE, &old)) {
				REX::ERROR("previs lighting fix: VirtualProtect failed");
				return false;
			}
			std::memcpy(reinterpret_cast<void*>(site), nops, sizeof(nops));
			VirtualProtect(reinterpret_cast<void*>(site), sizeof(nops), old, &old);
			FlushInstructionCache(GetCurrentProcess(),
				reinterpret_cast<void*>(site), sizeof(nops));

			g_installed = true;
			REX::INFO("previs lighting fix installed (light records) at "
					  "REL::ID({})+{:#x}",
				kAppendFnID, kLightSkipBranch);
			return true;
		}

		if (a_mode == Mode::kPackedLights) {
			// Patch the shared BSGeometry OnVisible slot on the classes the
			// combined meshes use (plain tri shapes; VisibilityProbe verified
			// they share one implementation).
			static constexpr std::uint64_t kGeomVtbls[] = {
				183326,  // BSTriShape
				248868,  // BSSubIndexTriShape
				95842,   // BSMultiStreamInstanceTriShape
			};
			for (const auto id : kGeomVtbls) {
				const REL::Relocation<std::uintptr_t> vt{ REL::ID(id) };
				auto* slot =
					reinterpret_cast<std::uintptr_t*>(vt.address() + kOnVisibleSlot);
				DWORD old{};
				if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
					REX::ERROR("packed-lights: VirtualProtect failed ({})", id);
					return false;
				}
				if (!g_originalGeomOnVisible) {
					g_originalGeomOnVisible =
						reinterpret_cast<OnVisible_t>(*slot);
				}
				*slot = reinterpret_cast<std::uintptr_t>(&GeomOnVisibleHook);
				VirtualProtect(slot, sizeof(*slot), old, &old);
			}
			g_packedLights = true;
			g_installed = true;
			REX::INFO("previs lighting fix installed (packed-lights)");
			return true;
		}

		if (a_mode == Mode::kNodeUpdate) {
			// Prep hook purely for the registry diagnostics.
			const REL::Relocation<std::uintptr_t> worldDraw2{ REL::ID(kDrawWorldID) };
			const auto prepSite2 = worldDraw2.address() + kAccumulateCallOffset;
			if (*reinterpret_cast<const std::uint8_t*>(prepSite2) == 0xE8) {
				auto& tramp = REL::GetTrampoline();
				g_originalPrevisAccumulate = reinterpret_cast<Accumulate_t>(
					tramp.write_call<5>(prepSite2,
						reinterpret_cast<std::uintptr_t>(&NotePrevisPrep)));
				g_nodeUpdateDiag = g_originalPrevisAccumulate != nullptr;
			}

			const REL::Relocation<std::uintptr_t> vtbl{ REL::ID(kBSFadeNodeVtblID) };
			auto* slot = reinterpret_cast<std::uintptr_t*>(
				vtbl.address() + kOnVisibleSlot);

			DWORD old{};
			if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
				REX::ERROR("previs lighting fix: VirtualProtect failed on vtable");
				return false;
			}
			g_originalOnVisible = reinterpret_cast<OnVisible_t>(*slot);
			*slot = reinterpret_cast<std::uintptr_t>(&FadeNodeOnVisibleHook);
			VirtualProtect(slot, sizeof(*slot), old, &old);

			g_installed = true;
			REX::INFO("previs lighting fix installed (node-update) on "
					  "BSFadeNode::OnVisible");
			return true;
		}

		if (a_mode == Mode::kAttachLights) {
			const REL::Relocation<std::uintptr_t> worldDraw{ REL::ID(kDrawWorldID) };
			const auto prepSite = worldDraw.address() + kAccumulateCallOffset;
			if (*reinterpret_cast<const std::uint8_t*>(prepSite) != 0xE8) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is not a call",
					kDrawWorldID, kAccumulateCallOffset);
				return false;
			}
			auto& trampoline = REL::GetTrampoline();
			g_originalPrevisAccumulate = reinterpret_cast<Accumulate_t>(
				trampoline.write_call<5>(prepSite,
					reinterpret_cast<std::uintptr_t>(&NotePrevisPrep)));
			if (!g_originalPrevisAccumulate) {
				REX::ERROR("previs lighting fix: trampoline refused the prep hook");
				return false;
			}
			g_attachLights = true;
			g_installed = true;
			REX::INFO("previs lighting fix installed (attach-lights)");
			return true;
		}

		if (a_mode == Mode::kLightScoped) {
			if (!Install(Mode::kLightRecords)) {
				return false;
			}
			g_installed = false;  // kLightRecords set it; not done yet

			const REL::Relocation<std::uintptr_t> worldDraw{ REL::ID(kDrawWorldID) };
			const auto prepSite = worldDraw.address() + kAccumulateCallOffset;
			if (*reinterpret_cast<const std::uint8_t*>(prepSite) != 0xE8) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is not a call",
					kDrawWorldID, kAccumulateCallOffset);
				return false;
			}
			auto& trampoline = REL::GetTrampoline();
			g_originalPrevisAccumulate = reinterpret_cast<Accumulate_t>(
				trampoline.write_call<5>(prepSite,
					reinterpret_cast<std::uintptr_t>(&NotePrevisPrep)));
			if (!g_originalPrevisAccumulate) {
				REX::ERROR("previs lighting fix: trampoline refused the prep hook");
				return false;
			}
			g_lightScoped = true;
			g_installed = true;
			REX::INFO("previs lighting fix installed (light-scoped); pair with "
					  "PrevisFixSites = 16");
			return true;
		}

		if (a_mode == Mode::kRedirect || a_mode == Mode::kWrapPrepOnly ||
			a_mode == Mode::kWrapChildOnly || a_mode == Mode::kWrapBoth) {
			g_divert = a_mode == Mode::kRedirect;
			const bool wantPrep = a_mode != Mode::kWrapChildOnly;
			const bool wantChild = a_mode != Mode::kWrapPrepOnly;

			const REL::Relocation<std::uintptr_t> drawWorld{ REL::ID(kDrawWorldMainID) };
			const auto site = drawWorld.address() + kChildAccumulateCall;
			const REL::Relocation<std::uintptr_t> worldDraw{ REL::ID(kDrawWorldID) };
			const auto prepSite = worldDraw.address() + kAccumulateCallOffset;
			if (wantChild && *reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is not a call",
					kDrawWorldMainID, kChildAccumulateCall);
				return false;
			}
			if (wantPrep && *reinterpret_cast<const std::uint8_t*>(prepSite) != 0xE8) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is not a call",
					kDrawWorldID, kAccumulateCallOffset);
				return false;
			}

			auto& trampoline = REL::GetTrampoline();
			if (wantPrep) {
				g_originalPrevisAccumulate = reinterpret_cast<Accumulate_t>(
					trampoline.write_call<5>(prepSite,
						reinterpret_cast<std::uintptr_t>(&NotePrevisPrep)));
				if (!g_originalPrevisAccumulate) {
					REX::ERROR("previs lighting fix: trampoline refused the prep hook");
					return false;
				}
			}
			if (wantChild) {
				g_originalChildAccumulate = reinterpret_cast<Accumulate_t>(
					trampoline.write_call<5>(site,
						reinterpret_cast<std::uintptr_t>(&RedirectChildAccumulate)));
				if (!g_originalChildAccumulate) {
					REX::ERROR("previs lighting fix: trampoline refused the child hook");
					return false;
				}
			}

			g_installed = true;
			REX::INFO("previs lighting fix installed: divert={} prepHook={} childHook={}",
				g_divert, wantPrep, wantChild);
			return true;
		}

		if (a_mode == Mode::kNoSkip) {
			const REL::Relocation<std::uintptr_t> drawWorld{ REL::ID(kDrawWorldMainID) };
			auto* target =
				reinterpret_cast<std::uint8_t*>(drawWorld.address() + kSkipJumpOffset);

			if (std::memcmp(target, kSkipJump, sizeof(kSkipJump)) != 0) {
				REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is {:02X} {:02X}, "
						   "expected 75 16 (jne); refusing to patch",
					kDrawWorldMainID, kSkipJumpOffset, target[0], target[1]);
				return false;
			}

			DWORD old{};
			if (!VirtualProtect(target, sizeof(kNops), PAGE_EXECUTE_READWRITE, &old)) {
				REX::ERROR("previs lighting fix: VirtualProtect failed");
				return false;
			}
			std::memcpy(target, kNops, sizeof(kNops));
			VirtualProtect(target, sizeof(kNops), old, &old);
			FlushInstructionCache(GetCurrentProcess(), target, sizeof(kNops));

			g_installed = true;
			REX::INFO("previs lighting fix installed (no-skip) at REL::ID({})+{:#x} ({:#x})",
				kDrawWorldMainID, kSkipJumpOffset,
				reinterpret_cast<std::uintptr_t>(target));
			return true;
		}

		const auto id = (a_mode == Mode::kConsumer) ? kConsumerCallerID : kDrawWorldID;
		const auto diff =
			(a_mode == Mode::kConsumer) ? kConsumerCallOffset : kAccumulateCallOffset;

		const REL::Relocation<std::uintptr_t> owner{ REL::ID(id) };
		const auto site = owner.address() + diff;

		// Refuse to patch anything that is not the call we mapped, so a
		// different runtime or a shifted address library fails loudly instead
		// of corrupting the render path.
		const auto opcode = *reinterpret_cast<const std::uint8_t*>(site);
		if (opcode != 0xE8) {
			REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is {:#04x}, not a call; "
					   "refusing to install",
				id, diff, opcode);
			return false;
		}

		auto& trampoline = REL::GetTrampoline();
		const auto detour = (a_mode == Mode::kConsumer)
								? reinterpret_cast<std::uintptr_t>(&ConsumePrevisBatch)
								: reinterpret_cast<std::uintptr_t>(&AccumulatePrevisBatch);
		const auto original = trampoline.write_call<5>(site, detour);

		if (!original) {
			REX::ERROR("previs lighting fix: trampoline refused the call detour");
			return false;
		}

		if (a_mode == Mode::kConsumer) {
			g_originalConsume = reinterpret_cast<Consume_t>(original);
		} else {
			g_original = reinterpret_cast<Accumulate_t>(original);
		}

		g_installed = true;
		REX::INFO("previs lighting fix installed ({}) at REL::ID({})+{:#x} ({:#x})",
			a_mode == Mode::kConsumer ? "consumer" : "accumulate", id, diff, site);
		return true;
	}
}
