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

	// Dumps the culling processes the visibility probe actually observed being
	// passed to OnVisible, rather than the globals the world draw passes.
	bool CullingProcesses(const std::filesystem::path& a_path, std::string_view a_label);

	// Dumps the batch captured at submit time on each side of the previs
	// branch, which is where the two paths finally diverge.
	bool SubmittedBatches(const std::filesystem::path& a_path, std::string_view a_label);

	// One line per geometry seen by OnVisible, with its flags, so the two
	// previs states can be diffed object by object.
	bool ObjectFlags(const std::filesystem::path& a_path, std::string_view a_label);
}
