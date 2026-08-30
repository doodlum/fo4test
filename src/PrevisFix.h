#pragma once

// Neutralises individual call sites of BSPreCulledObjects' "is pre-culling
// active" predicate (0x1421ae520), so the engine takes its previs-OFF path at
// exactly those sites while previs itself stays enabled.
//
// Each site is a 5-byte `call rel32` whose result is immediately consumed by
// `test al, al`.  Replacing it with `xor eax, eax` + 3 nops makes the
// predicate read false there and nowhere else -- which is what lets the cause
// be bisected across 38 sites with a handful of automated runs instead of
// guessed at.
namespace PrevisFix
{
	// a_spec: "none", "all", a range like "9-19", or a list like "20,23,28".
	// Returns the number of sites patched.
	std::size_t Apply(std::string_view a_spec);

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
