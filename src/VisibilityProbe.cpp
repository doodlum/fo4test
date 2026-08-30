#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#ifdef ERROR
#	undef ERROR
#endif

#include "PCH.h"

#include "VisibilityProbe.h"

#include <map>
#include <mutex>

namespace
{
	// NiAVObject::OnVisible(NiCullingProcess*) -- vtable slot +0x1C8.
	constexpr std::ptrdiff_t kOnVisibleSlot = 0x1C8;

	using OnVisible_t = void (*)(void*, void*);

	OnVisible_t          g_original{ nullptr };
	std::atomic_uint64_t g_count{ 0 };
	bool                 g_installed{ false };

	std::mutex                              g_mutex;
	std::map<std::uintptr_t, std::uint64_t> g_processes;

	// Bounded so a runaway frame cannot eat memory; the scene has ~18k calls
	// over a few thousand distinct objects.
	constexpr std::size_t kMaxRecords = 40000;

	struct Rec
	{
		std::uint64_t flags;
		std::uint64_t calls;
	};

	std::map<std::uintptr_t, Rec> g_records;

	std::atomic_bool               g_snapshotWanted{ false };
	VisibilityProbe::Snapshot      g_snapshot{};

	// NiAVObject's 64-bit flags word; BSFadeNode::OnVisible reads and writes
	// it at this offset, so it is correct for this runtime.
	constexpr std::ptrdiff_t kObjectFlags = 0x108;

	void OnVisibleProbe(void* a_this, void* a_cullingProcess)
	{
		g_count.fetch_add(1, std::memory_order_relaxed);
		{
			// Bounded: a frame only ever uses a handful of culling processes,
			// so the map stays tiny and the lock is uncontended in practice.
			const std::scoped_lock lock{ g_mutex };
			++g_processes[reinterpret_cast<std::uintptr_t>(a_cullingProcess)];
		}
		{
			const std::scoped_lock lock{ g_mutex };
			if (g_records.size() < kMaxRecords) {
				auto& rec = g_records[reinterpret_cast<std::uintptr_t>(a_this)];
				rec.flags = *reinterpret_cast<const std::uint64_t*>(
					reinterpret_cast<const std::uint8_t*>(a_this) + kObjectFlags);
				++rec.calls;
			}
		}

		if (g_snapshotWanted.exchange(false)) {
			const std::scoped_lock lock{ g_mutex };
			g_snapshot.valid = true;
			g_snapshot.process = reinterpret_cast<std::uintptr_t>(a_cullingProcess);
			g_snapshot.geometry = reinterpret_cast<std::uintptr_t>(a_this);
			g_snapshot.geometryFlags =
				*reinterpret_cast<const std::uint64_t*>(
					reinterpret_cast<const std::uint8_t*>(a_this) + kObjectFlags);
			std::memcpy(g_snapshot.bytes.data(), a_cullingProcess, g_snapshot.bytes.size());
		}

		g_original(a_this, a_cullingProcess);
	}

	// Every geometry class that draws shares one OnVisible implementation, so
	// the slot is patched in each of their vtables.
	constexpr std::array kGeometryVTables{
		RE::VTABLE::BSTriShape,
		RE::VTABLE::BSSubIndexTriShape,
		RE::VTABLE::BSDynamicTriShape,
		RE::VTABLE::BSGeometry,
	};
}

namespace VisibilityProbe
{
	std::uint64_t Count() noexcept { return g_count.load(std::memory_order_relaxed); }

	void Reset() noexcept
	{
		g_count.store(0, std::memory_order_relaxed);
		const std::scoped_lock lock{ g_mutex };
		g_processes.clear();
		g_records.clear();
	}

	std::vector<Record> Records()
	{
		const std::scoped_lock lock{ g_mutex };
		std::vector<Record> out;
		out.reserve(g_records.size());
		for (const auto& [geometry, rec] : g_records) {
			out.push_back({ geometry, rec.flags, rec.calls });
		}
		return out;
	}

	void RequestSnapshot() noexcept { g_snapshotWanted.store(true); }

	Snapshot LastSnapshot()
	{
		const std::scoped_lock lock{ g_mutex };
		return g_snapshot;
	}

	std::vector<Observed> Processes()
	{
		const std::scoped_lock lock{ g_mutex };
		std::vector<Observed> out;
		out.reserve(g_processes.size());
		for (const auto& [process, calls] : g_processes) {
			out.push_back({ process, calls });
		}
		return out;
	}

	bool Install()
	{
		if (g_installed) {
			return true;
		}

		std::size_t patched = 0;
		for (const auto& vtable : kGeometryVTables) {
			auto* slot = reinterpret_cast<std::uintptr_t*>(
				vtable[0].address() + kOnVisibleSlot);

			// Every one of these classes should point at the same shared
			// implementation; if one does not, the slot mapping is wrong for
			// this runtime and writing would corrupt dispatch.
			if (!g_original) {
				g_original = reinterpret_cast<OnVisible_t>(*slot);
			} else if (reinterpret_cast<OnVisible_t>(*slot) != g_original) {
				REX::WARN("visibility probe: vtable slot {:#x} holds {:#x}, expected {:#x}; "
						  "skipping this one",
					kOnVisibleSlot, *slot, reinterpret_cast<std::uintptr_t>(g_original));
				continue;
			}

			DWORD old{};
			if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
				continue;
			}
			*slot = reinterpret_cast<std::uintptr_t>(&OnVisibleProbe);
			VirtualProtect(slot, sizeof(*slot), old, &old);
			++patched;
		}

		if (patched == 0) {
			REX::ERROR("visibility probe: patched no vtables");
			return false;
		}

		g_installed = true;
		REX::INFO("visibility probe: hooked OnVisible in {} vtable(s); original {:#x}",
			patched, reinterpret_cast<std::uintptr_t>(g_original));
		return true;
	}
}
