#pragma once

// Snapshots the culling batch at the moment it is submitted for rendering.
//
// DrawWorld (REL::ID(2318292)) submits one of two batches depending on previs:
//
//     call IsPreCullingActive
//     je   <off>
//     lea  rcx, 0x142F98090      ; previs batch      +0x4aa
//     call FUN_1417e08c0
//     jmp  merge
//   <off>:
//     lea  rcx, 0x142F97C40      ; param_2 batch     +0x4d2
//     call FUN_1417e08c0
//
// Both go to the same submit function, and by this point the visibility stage
// is known to be identical -- same geometry, same OnVisible count, same
// culling process, byte-identical state.  So whatever differs has to be in
// these two batches at submit time.
//
// Dumping them from a task shows their idle state between frames, which is
// useless; these hooks capture the bytes as they are handed to the submit.
namespace SubmitProbe
{
	bool Install();

	// The batch header is only a handle; the accumulated geometry lives in a
	// buffer at batch+0xD0, laid out (per FUN_1417e0030 / FUN_1417e15d0) as:
	//
	//     +0x2060  object pointers
	//     +0x3060  two flag bytes per entry
	//     +0x3A68  entry count
	//     +0x3A6F  copied from batch+0x16A
	//
	// The headers compare equal between the two previs paths, so if anything
	// differs it is in here.
	inline constexpr std::size_t kMaxEntries = 512;

	struct Snapshot
	{
		bool                            valid;
		std::uintptr_t                  batch;
		std::uint64_t                   calls;
		std::array<std::uint8_t, 0x170> bytes;

		std::uintptr_t                            buffer;
		std::uint32_t                             count;
		std::uint8_t                              tailFlag;  // buffer +0x3A6F
		std::uint32_t                             captured;
		std::array<std::uintptr_t, kMaxEntries>   objects;

		// The light sub-batch page at batch+0x150 (the geometry page is
		// +0xD0), and each entry object's NiLight affected-node count at
		// +0x1D0 -- zero here with previs ON is what starves the light
		// records the transparent shader needs.
		std::uintptr_t                            lightBuffer{ 0 };
		std::uint32_t                             lightCount{ 0 };
		std::array<std::uint32_t, kMaxEntries>    affected;
		std::array<std::uint16_t, kMaxEntries>    entryFlags;

		// NiObjectNET::name (BSFixedString at +0x10) for each entry, so the
		// objects previs drops can be identified rather than just counted.
		static constexpr std::size_t                             kNameLen = 64;
		std::array<std::array<char, kNameLen>, kMaxEntries>      names;

		// First qword of each entry: its vtable, used to name the class.
		std::array<std::uintptr_t, kMaxEntries>                  vtables;

		// NiAVObject::worldBound at +0xB0 (centre xyz + radius).  The +0x108
		// flags word is verified correct for this build, so this tail of the
		// layout can be trusted -- and a world position identifies an object
		// far more usefully than a name, given the camera position is known.
		std::array<std::array<float, 4>, kMaxEntries>            bounds;
		std::array<std::uint64_t, kMaxEntries>                   objFlags;
	};

	// Index 0 = the previs-side submit, 1 = the previs-off side.
	[[nodiscard]] Snapshot Get(std::size_t a_index);
	void                   Reset() noexcept;
}
