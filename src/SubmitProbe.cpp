#include "PCH.h"

#include "SubmitProbe.h"

#include <mutex>

namespace
{
	// NiObjectNET::name -- a BSFixedString, i.e. a pointer straight at the
	// character data.  Copied defensively: an object mid-teardown can have a
	// null or unreadable pointer, and this runs inside the render path.
	void CopyName(const void* a_object, char* a_out, std::size_t a_size)
	{
		a_out[0] = '\0';
		if (!a_object) {
			return;
		}
		const auto* str = *reinterpret_cast<const char* const*>(
			reinterpret_cast<const std::uint8_t*>(a_object) + 0x10);
		if (!str) {
			return;
		}
		std::size_t i = 0;
		for (; i + 1 < a_size; ++i) {
			const char c = str[i];
			if (c == '\0') {
				break;
			}
			// Anything unprintable means the offset guess is wrong for this
			// object; bail rather than write junk into the dump.
			if (c < 0x20 || c > 0x7E) {
				a_out[0] = '\0';
				return;
			}
			a_out[i] = c;
		}
		a_out[i] = '\0';
	}
	constexpr std::uint64_t  kDrawWorldID = 2318292;
	constexpr std::ptrdiff_t kPrevisSubmit = 0x4aa;   // lea rcx, 0x142F98090
	constexpr std::ptrdiff_t kNoPrevisSubmit = 0x4d2; // lea rcx, 0x142F97C40
	constexpr std::ptrdiff_t kTraversalSubmit = 0x4fa; // lea rcx, 0x142F97DB0 -- always run

	using Submit_t = void (*)(std::uint8_t*, void*);

	Submit_t   g_originalPrevis{ nullptr };
	Submit_t   g_originalNoPrevis{ nullptr };
	Submit_t   g_originalTraversal{ nullptr };
	bool       g_installed{ false };
	std::mutex g_mutex;

	SubmitProbe::Snapshot g_snapshots[3]{};

	void Capture(std::size_t a_index, std::uint8_t* a_batch)
	{
		if (!a_batch) {
			return;
		}
		const std::scoped_lock lock{ g_mutex };
		auto& snap = g_snapshots[a_index];
		++snap.calls;
		// Keep the first capture of the window; later frames in the same
		// window are the same state and rewriting churns the lock.
		if (!snap.valid) {
			snap.valid = true;
			snap.batch = reinterpret_cast<std::uintptr_t>(a_batch);
			std::memcpy(snap.bytes.data(), a_batch, snap.bytes.size());

			auto* buffer = *reinterpret_cast<std::uint8_t**>(a_batch + 0xD0);
			snap.buffer = reinterpret_cast<std::uintptr_t>(buffer);
			if (auto* lightBuf = *reinterpret_cast<std::uint8_t**>(a_batch + 0x150)) {
				snap.lightBuffer = reinterpret_cast<std::uintptr_t>(lightBuf);
				snap.lightCount =
					*reinterpret_cast<const std::uint32_t*>(lightBuf + 0x3A68);
			}
			if (buffer) {
				snap.count = *reinterpret_cast<const std::uint32_t*>(buffer + 0x3A68);
				snap.tailFlag = buffer[0x3A6F];
				snap.captured = std::min<std::uint32_t>(
					snap.count, static_cast<std::uint32_t>(SubmitProbe::kMaxEntries));
				const auto* objects = reinterpret_cast<const std::uintptr_t*>(buffer + 0x2060);
				const auto* flags = reinterpret_cast<const std::uint16_t*>(buffer + 0x3060);
				for (std::uint32_t i = 0; i < snap.captured; ++i) {
					snap.objects[i] = objects[i];
					snap.entryFlags[i] = flags[i];
					CopyName(reinterpret_cast<const void*>(objects[i]),
						snap.names[i].data(), snap.names[i].size());
					// The name field came back empty for every entry, so these
					// are not NiObjectNET-derived.  The vtable pointer
					// identifies the class without depending on any member
					// offset -- IDs_VTABLE.h maps it back to a name offline.
					snap.vtables[i] = objects[i]
						? *reinterpret_cast<const std::uintptr_t*>(objects[i])
						: 0;
					if (objects[i]) {
						const auto* o = reinterpret_cast<const std::uint8_t*>(objects[i]);
						std::memcpy(snap.bounds[i].data(), o + 0xB0, sizeof(float) * 4);
						snap.objFlags[i] =
							*reinterpret_cast<const std::uint64_t*>(o + 0x108);
						snap.affected[i] =
							*reinterpret_cast<const std::uint32_t*>(o + 0x1D0);
					}
				}
			}
		}
	}

	void SubmitPrevis(std::uint8_t* a_batch, void* a_arg)
	{
		Capture(0, a_batch);
		g_originalPrevis(a_batch, a_arg);
	}

	void SubmitNoPrevis(std::uint8_t* a_batch, void* a_arg)
	{
		Capture(1, a_batch);
		g_originalNoPrevis(a_batch, a_arg);
	}

	// Submitted on both paths, so whatever the previs branch drops may simply
	// arrive here instead.  Without this the two branch batches cannot be
	// compared honestly.
	void SubmitTraversal(std::uint8_t* a_batch, void* a_arg)
	{
		Capture(2, a_batch);
		g_originalTraversal(a_batch, a_arg);
	}
}

namespace SubmitProbe
{
	Snapshot Get(std::size_t a_index)
	{
		const std::scoped_lock lock{ g_mutex };
		return a_index < 3 ? g_snapshots[a_index] : Snapshot{};
	}

	void Reset() noexcept
	{
		const std::scoped_lock lock{ g_mutex };
		g_snapshots[0] = {};
		g_snapshots[1] = {};
		g_snapshots[2] = {};
	}

	bool Install()
	{
		if (g_installed) {
			return true;
		}

		const REL::Relocation<std::uintptr_t> drawWorld{ REL::ID(kDrawWorldID) };
		auto& trampoline = REL::GetTrampoline();

		const auto hook = [&](std::ptrdiff_t a_offset, void* a_detour, Submit_t& a_out) {
			const auto site = drawWorld.address() + a_offset;
			if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
				REX::ERROR("submit probe: REL::ID({})+{:#x} is not a call", kDrawWorldID,
					a_offset);
				return false;
			}
			a_out = reinterpret_cast<Submit_t>(
				trampoline.write_call<5>(site, reinterpret_cast<std::uintptr_t>(a_detour)));
			return a_out != nullptr;
		};

		const bool a = hook(kPrevisSubmit, &SubmitPrevis, g_originalPrevis);
		const bool b = hook(kNoPrevisSubmit, &SubmitNoPrevis, g_originalNoPrevis);
		const bool c = hook(kTraversalSubmit, &SubmitTraversal, g_originalTraversal);
		if (!a || !b || !c) {
			return false;
		}

		g_installed = true;
		REX::INFO("submit probe installed at REL::ID({})+{:#x} and +{:#x}", kDrawWorldID,
			kPrevisSubmit, kNoPrevisSubmit);
		return true;
	}
}
