#pragma once

// Counts BSGeometry::OnVisible calls, to answer a question the pixel diff
// cannot: does the previs path put geometry through the same per-object
// visibility/lighting entry point as the scene-graph traversal, or does it
// bypass it?
//
// OnVisible is vtable slot +0x1C8 (confirmed because BSFadeNode's own +0x1C8
// is BSFadeNode::OnVisible, REL::ID(2316459)).  BSTriShape,
// BSSubIndexTriShape, BSDynamicTriShape and BSGeometry all share one
// implementation at 0x1416d5720, so patching those vtable slots catches the
// geometry that actually draws.
//
// If the count collapses with previs ON, previs-accumulated geometry never
// reaches the per-object setup -- which would explain why only transparent
// surfaces, which depend on that state, come out wrong.
namespace VisibilityProbe
{
	bool Install();

	[[nodiscard]] std::uint64_t Count() noexcept;
	void                        Reset() noexcept;

	// OnVisible's second argument is the culling process driving the pass.
	// The geometry and the call count are the same with previs on or off, so
	// whatever differs has to arrive through this pointer.  Recording which
	// distinct ones are used, and how often, identifies the objects actually
	// worth dumping -- as opposed to the globals the world draw happens to
	// pass, which turned out not to be the ones in play.
	struct Observed
	{
		std::uintptr_t process;
		std::uint64_t  calls;
	};

	[[nodiscard]] std::vector<Observed> Processes();

	// Dumping the culling process from a task shows its idle state between
	// frames, which is identical with previs on or off.  To see the state the
	// geometry is actually processed with, the snapshot has to be taken inside
	// the hook.  RequestSnapshot arms it; the next OnVisible call copies the
	// process, and also the geometry's own flags word, into a buffer.
	void RequestSnapshot() noexcept;

	// Mode-10 experiment: apply the previs-off shader-property flag deltas
	// (set bits 8/11, clear bit 4) to every geometry passing OnVisible.
	void SetForceFlags(bool a_on) noexcept;

	struct Snapshot
	{
		bool                       valid;
		std::uintptr_t             process;
		std::uintptr_t             geometry;
		std::uint64_t              geometryFlags;  // NiAVObject flags at +0x108
		std::array<std::uint8_t, 0x170> bytes;
	};

	[[nodiscard]] Snapshot LastSnapshot();

	// Deep state capture for the geometry nearest a reference point (the
	// glass, when the point is the camera).  For each of the N nearest
	// geometries seen after arming, the hook copies the BSGeometry object and
	// both of its NiProperty targets (properties[2] at +0x130/+0x138) so the
	// broken and correct frames can be byte-diffed offline.  Everything is
	// copied inside the hook, on the thread that owns the objects.
	void ArmNearCapture(float a_x, float a_y, float a_z) noexcept;

	struct NearGeometry
	{
		std::uintptr_t                  geometry{ 0 };
		float                           distance{ 0.0f };
		std::uintptr_t                  prop0{ 0 };
		std::uintptr_t                  prop1{ 0 };
		std::uintptr_t                  pass0{ 0 };
		std::array<std::uint8_t, 0x160> geoBytes{};
		// First BSRenderPass of prop1's renderPassList (+0x38), raw.
		std::array<std::uint8_t, 0x68>  pass0Bytes{};
		std::array<std::uint8_t, 0x1C0> prop0Bytes{};
		std::array<std::uint8_t, 0x1C0> prop1Bytes{};
	};

	[[nodiscard]] std::vector<NearGeometry> NearCaptures();

	// Per-object record: every geometry that reaches OnVisible in the window,
	// with its NiAVObject flags at +0x108.  The geometry set and call count
	// are identical with previs on or off, and the culling process and the
	// submitted batch are equivalent too -- so if anything about an individual
	// object differs, it shows up here.
	struct Record
	{
		std::uintptr_t geometry;
		std::uint64_t  flags;
		std::uint64_t  calls;
	};

	[[nodiscard]] std::vector<Record> Records();
}
