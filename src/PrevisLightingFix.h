#pragma once

// Previs and lighting.
//
// Fallout 4's world draw keeps several culling batches.  Inside the main draw,
// REL::ID(2318292), a child loop fills one batch by walking the scene graph,
// and a previs branch fills another from the precomputed visibility list.
// Whichever the previs check selects is the one submitted:
//
//     call IsPreCullingActive
//     test al, al
//     je   <traversal path>
//     lea  rcx, <previs batch>      ; +0x4aa -- submitted when previs is ON
//     ...
//     lea  rcx, <scene-graph batch> ; +0x4d2 -- submitted when previs is OFF
//
// This is not just a culling pass; it is where objects are associated with the
// lights that affect them.  Dumping what each side actually submits, from the
// chem-bench repro:
//
//                    entries   BSFadeNode   NiPointLight
//     previs OFF        359         358              0
//     previs ON          23          11             11
//
// So the two paths carry different *kinds* of thing, and the previs side
// carries 15x less of it.  Sorting both by distance from the camera, 38
// objects within 300 units are present with previs off and absent with previs
// on -- including the nearest at d=9.2, which is the chem bench holding the
// glassware.  No flag bit distinguishes the dropped objects from the kept
// ones, so this is not a filter being applied: the previs list simply does not
// contain them.
//
// An object missing from this pass never has its lighting refreshed, so it
// still renders (previs culling is per-frame and correct) but is lit from
// stale or default state.  Opaque surfaces mostly survive that; transparent
// ones, whose appearance depends on the lights bound here, come out wrong.
//
// The fix redirects accumulation, not submission.  The child loop's per-object
// accumulate call is detoured; while previs is active, objects bound for the
// param_2 batch go into the previs batch instead.  The engine still prepares
// and submits exactly one batch per path, through its own code, in its own
// order -- only the batch's contents change, appended via the engine's own
// accumulate so every internal invariant holds.
//
// Approaches measured and abandoned, in case anyone is tempted:
//   * submitting the stranded param_2 batch a second time: the submit's chunk
//     walk terminates only on exact pointer equality with an end sentinel, so
//     a batch in any state it does not expect loops forever.  Froze the game.
//   * patching the previs branch to swap batches outright: submits an empty,
//     unprepared batch.  Froze the game.
//   * forcing the batch's +0x169 control byte, hooking the flag consumer,
//     removing the child-skip: all ran stably and changed nothing, because
//     the problem is which objects are in the batch, not how it is flagged.
namespace PrevisLightingFix
{
	// Where to force the flag.
	//
	//   kAccumulate -- at REL::ID(2318289)+0x192, the call that accumulates the
	//                  previs batch.  Measured ineffective: the hook runs
	//                  (1202 hits) but the engine rebuilds the batch before the
	//                  consumer reads it, and the frame is unchanged.
	//   kConsumer   -- at REL::ID(2275918)+0x2e2, the sole call into the
	//                  function that branches on the flag.  Forces the same
	//                  lighting path the traversal batch gets, at the point of
	//                  use, so a rebuild cannot undo it.
	//   kNoSkip     -- REL::ID(2318292)+0x315.  DrawWorld walks a node's
	//                  children and, for any child whose NiAVObject flags at
	//                  +0x108 have bit 40 set, does:
	//
	//                      test bpl, bpl        ; previs active?
	//                      jne  <next child>    ; yes -> drop the child
	//                      lea  rcx, <traversal batch>
	//                      call FUN_1417e0030   ; no  -> accumulate it
	//
	//                  So with previs on those children are dropped from this
	//                  accumulation entirely.  Neutralising the 2-byte jne
	//                  accumulates them as the previs-off path does, while the
	//                  precomputed visibility list is still consumed normally
	//                  -- previs keeps working, the dropped objects come back.
	//   kRedirect   -- falsified: ran stably once gated correctly, but the
	//                  diverted objects did not change the frame.  Kept for
	//                  future investigation.  Requires PrevisFixSites = 30, which lets
	//                  DrawWorld's child loop run even with previs on.  The
	//                  loop files objects with objFlags bit 40 clear into the
	//                  param_2 batch -- which is never submitted while previs
	//                  is on, so those objects (the chem bench among them)
	//                  never reach the light-association pass.  This detours
	//                  the loop's accumulate call and, while previs is
	//                  active, redirects param_2-bound objects into the
	//                  previs batch instead.  One batch, one submit, appended
	//                  through the engine's own accumulate; previs itself
	//                  stays fully enabled.
		enum class Mode
	{
		kOff,
		kAccumulate,
		kConsumer,
		kNoSkip,
		kRedirect,
		// Diagnostic variants of kRedirect that install the same chaining
		// hooks but never divert anything, isolating "hooking these sites
		// breaks the game" from "diverting objects breaks the game".
		kWrapPrepOnly,
		kWrapChildOnly,
		kWrapBoth,
		// NOP the append's previs-gated skip of its light-record block, so
		// light<->geometry association records are written even while previs
		// is active.  Needs no fix sites, no diversion, and no arming.
		kLightRecords,
		// EXPERIMENTAL, unfinished: a candidate exterior-safe fix.  After
		// the engine's own previs accumulate, walk the scene for
		// light-carrying nodes only and accumulate them into the traversal
		// batch (submitted in both modes), with the append's record branch
		// un-gated (kLightRecords' patch); pair with PrevisFixSites = 16 so
		// the consumer dispatches the light-runs.  Status: walks armed by
		// the harness only (an unarmed walk during save deserialisation hit
		// a garbage vtable and crashed); never image-validated -- the
		// shipped fix is the interior-aware PrevisFixSites detour, and this
		// is kept for future exterior work.
		kLightScoped,
	};

	// Installs the hook.  Requires F4SE::Init to have allocated a trampoline.
	bool Install(Mode a_mode);

	[[nodiscard]] bool Installed() noexcept;

	// kRedirect diverts only while this is true; wire it to F4SE's
	// kPostLoadGame / kNewGame / kPreLoadGame messages.
	void SetWorldReady(bool a_ready) noexcept;

	// How many times the hook has run, for the harness to confirm the fixed
	// path is actually being exercised rather than silently bypassed.
	[[nodiscard]] std::uint64_t HitCount() noexcept;
}
