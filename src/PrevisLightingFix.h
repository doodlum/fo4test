#pragma once

// The fix.
//
// Fallout 4's world draw keeps two culling batches and picks between them:
// one filled by walking the scene graph, one filled from the precomputed
// previs list.  Inside REL::ID(2318289):
//
//     call IsPreCullingActive          ; REL::ID(2317355) @ 0x1421ae520
//     test al, al
//     je   <traversal path>
//     mov  rcx, rbp                    ; rbp = the previs batch
//     call FUN_1421ae5d0
//     mov  rcx, rbp
//     call FUN_1417e0030               ; +0x192 -- accumulate into the batch
//
// Each batch carries a control byte at +0x169.  Dumping both live shows:
//
//                    traversal   previs
//     previs ON          01        00     <- broken glass
//     previs OFF         01        01     <- correct glass
//
// The previs batch is rebuilt every frame and comes out with the byte clear.
// A consumer (REL::ID(2275926)) branches directly on it:
//
//     if (batch[0x169] == 0) FUN_14217d760();                      // no args
//     else                   FUN_14217d450(obj, batch[0x160], 1);  // object +
//                                                                  // batch state
//
// So geometry accumulated through the previs batch never reaches the call
// that receives the object and the batch's state pointer, and its lighting
// is set up from defaults instead.  Opaque surfaces mostly survive that;
// transparent ones, which depend on that per-object state, come out with the
// wrong tint.
//
// The fix sets the byte on the previs batch immediately before the
// accumulation that consumes it, leaving previs itself fully enabled -- the
// precomputed list is still what drives visibility, so the performance win is
// kept.
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
	enum class Mode
	{
		kOff,
		kAccumulate,
		kConsumer,
	};

	// Installs the hook.  Requires F4SE::Init to have allocated a trampoline.
	bool Install(Mode a_mode);

	[[nodiscard]] bool Installed() noexcept;

	// How many times the hook has run, for the harness to confirm the fixed
	// path is actually being exercised rather than silently bypassed.
	[[nodiscard]] std::uint64_t HitCount() noexcept;
}
