#pragma once

// The bisection instrument that located the bug, and the superseded first
// fix.  The shipping fix is PrevisLightingFix::Mode::kNodeUpdate (mode 12):
// a one-shot per-node light attach; see PrevisLightingFix.h.  Kept intact:
// the 38-site table remains the map of every previs gate in the draw path,
// and the interior-aware detour configuration below still works as a
// fallback.
//
// Fallout 4 gates roughly forty draw-path decisions on BSPreCulledObjects'
// "is pre-culling active" predicate (0x1421ae520, REL::ID(2317322)).  With
// previs enabled, eight of those gates also skip the work that binds lighting
// state to visible geometry, which is why transparent objects (the chem-lab
// glassware in the repro) render with cold, default environment lighting:
// their light<->geometry association is simply never built.
//
// Neutralising exactly those eight gates -- and only those -- makes the
// lighting pipeline take its previs-OFF path while previs culling itself
// stays enabled.  Measured on the chem-lab save (1280x720, deltas are strong
// pixels vs an unpatched previs-OFF reference):
//
//     vanilla previs ON       ~52,600 px wrong    267 fps
//     fixed   previs ON            13 px          266 fps   <- the fix
//     fixed   previs OFF (tpc)      -             241 fps   (previs still +10%)
//
// The eight sites (default spec "23,25,32-37"):
//
//     23  2318289+0x165  world-draw selector: previs branch gate; patched,
//                        the scene-graph traversal accumulates instead of
//                        the previs list
//     25  2318289+0x1c5  the selector's second gate; patched, the previs-only
//                        continuation is skipped coherently with 23
//     32  2318292+0x3cb  DrawWorld: runs the previs-off-only portal/room
//                        block
//     33  2318292+0x485  DrawWorld: prepares and submits the scene-graph
//                        batch (REL::ID(4784648)) -- the batch that carries
//                        the light-association entries
//     34  2318292+0x532  DrawWorld: second submit-region gate, keeps the
//                        submit path coherent with 33
//     35  2318321+0x75   frame driver: skips the previs-only render-list
//                        block that 23/25/33 replace
//     36  2318321+0x2b0  frame driver: runs the camera-position lighting
//                        pre-pass (0x1421f0e90) previs otherwise skips; this
//                        alone removes most of the broad frame difference
//     37  2319338+0xb9   inside the portal/occlusion routine the previs-off
//                        path drives (0x142250400)
//
// Every subset of the eight was measured worse on both image and fps (partial
// patching leaves previs- and non-previs-style work running side by side),
// and every superset tried was slower.  Previs itself remains active: its
// data still loads, the unpatched gates (cell pre-culling, OnVisible checks,
// the previs batch enqueue) still run, and the fixed build keeps a measured
// fps advantage over its own previs-OFF state.
//
// Each site is a 5-byte `call rel32` whose result is immediately consumed by
// `test al, al`.  Replacing it with `xor eax, eax` + 3 nops makes the
// predicate read false there and nowhere else.  All addresses are
// REL::ID + in-function offsets, so the plugin follows game updates through
// the address library.
namespace PrevisFix
{
	// a_spec: "none", "all", a range like "9-19", or a list like "20,23,28".
	// a_conditional: true routes each site through the interior-aware
	// predicate (the shipping fix -- previs-off lighting in interiors only,
	// where it is free); false hard-forces previs-off at the site (the
	// bisection instrument, and measurably too expensive in exteriors).
	// Returns the number of sites handled.
	std::size_t Apply(std::string_view a_spec, bool a_conditional);

	// Benchmark kill-switch for the conditional detours: false = vanilla
	// behaviour at every detoured gate.  No effect on hard-patched sites.
	void SetFixActive(bool a_active) noexcept;

	// How many sites the table knows about.
	[[nodiscard]] std::size_t SiteCount() noexcept;

	// The world draw keeps two culling batches: one fed by scene-graph
	// traversal (REL::ID(4784655)) and one fed by the precomputed previs list
	// (REL::ID(4784667)).  Dumping both showed a single control byte at +0x169
	// that the traversal batch always has set, while the previs batch has it
	// set only when previs is OFF:
	//
	//                    traversal   previs
	//     previs ON          01        00     <- the buggy frame
	//     previs OFF         01        01     <- the reference frame
	//
	// FUN_1417e0030 propagates a byte from this control region into every
	// batch it accumulates, so a cleared flag here follows the geometry into
	// rendering.  Forcing it set is the narrowest available intervention that
	// leaves previs itself running.
	void ForcePrevisBatchFlag();
}
