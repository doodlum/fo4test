#pragma once

// Runtime dumps of the two culling/accumulator globals that the world draw
// picks between.
//
// FUN_1421f1010 (REL::ID(2318289)) feeds the precomputed previs list into one
// of them and the scene-graph traversal into the other:
//
//     if (IsPreCullingActive())  FUN_1417e0030(param_4, <previs root>, ...)
//     if (!IsPreCullingActive()) FUN_1417e0030(param_3, <scene node>, ...)
//
// If the previs-side object is set up differently from the traversal-side one,
// that asymmetry is a candidate cause for transparent geometry being lit
// wrongly.  Dumping both and diffing them is cheaper than reading every
// consumer.
namespace Dump
{
	// Writes a hex dump of both globals to a_path.  Game thread only.
	bool Accumulators(const std::filesystem::path& a_path, std::string_view a_label);
}
