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

	// Near-geometry capture state.  Guarded by g_mutex like everything else.
	constexpr std::size_t kNearMax = 8;
	bool                  g_nearArmed{ false };
	float                 g_nearPoint[3]{};
	std::vector<VisibilityProbe::NearGeometry> g_near;

	[[nodiscard]] bool PointsIntoModule(std::uintptr_t a_ptr)
	{
		static const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		return a_ptr > base && a_ptr - base < 0x10000000;
	}

	void TryNearCapture(std::uintptr_t a_geometry)
	{
		if (!g_nearArmed || g_near.size() >= kNearMax) {
			return;
		}
		for (const auto& existing : g_near) {
			if (existing.geometry == a_geometry) {
				return;
			}
		}
		const auto* geo = reinterpret_cast<const std::uint8_t*>(a_geometry);
		const auto* bound = reinterpret_cast<const float*>(geo + 0xB0);
		const float dx = bound[0] - g_nearPoint[0];
		const float dy = bound[1] - g_nearPoint[1];
		const float dz = bound[2] - g_nearPoint[2];
		const float d2 = dx * dx + dy * dy + dz * dz;
		if (d2 > 60.0f * 60.0f) {
			return;
		}

		VisibilityProbe::NearGeometry rec;
		rec.geometry = a_geometry;
		rec.distance = std::sqrt(d2);
		std::memcpy(rec.geoBytes.data(), geo, rec.geoBytes.size());
		for (int slot = 0; slot < 2; ++slot) {
			const auto prop = *reinterpret_cast<const std::uintptr_t*>(
				geo + 0x130 + slot * 8);
			auto& dst = slot == 0 ? rec.prop0Bytes : rec.prop1Bytes;
			(slot == 0 ? rec.prop0 : rec.prop1) = prop;
			if (prop && PointsIntoModule(*reinterpret_cast<const std::uintptr_t*>(prop))) {
				std::memcpy(dst.data(), reinterpret_cast<const void*>(prop), dst.size());
				if (slot == 1) {
					// prop1's render pass list head; a pass is a plain struct
					// whose first field is a BSShader* (vtabled), which makes
					// it validatable the same way.
					const auto pass = *reinterpret_cast<const std::uintptr_t*>(
						prop + 0x38);
					if (pass) {
						const auto shader =
							*reinterpret_cast<const std::uintptr_t*>(pass);
						if (shader &&
							PointsIntoModule(
								*reinterpret_cast<const std::uintptr_t*>(shader))) {
							rec.pass0 = pass;
							std::memcpy(rec.pass0Bytes.data(),
								reinterpret_cast<const void*>(pass),
								rec.pass0Bytes.size());
						}
					}
				}
			}
		}
		g_near.push_back(std::move(rec));
	}
	VisibilityProbe::Snapshot      g_snapshot{};

	// NiAVObject's 64-bit flags word; BSFadeNode::OnVisible reads and writes
	// it at this offset, so it is correct for this runtime.
	constexpr std::ptrdiff_t kObjectFlags = 0x108;

	// Mode-10 experiment: force the shader-property flag bits the previs-off
	// path sets (see neargeo.txt diff -- OFF = bits 8 and 11 set, bit 4
	// clear, everything else in the 0x1C0-byte property identical).
	std::atomic_bool g_forceFlags{ false };

	void MaybeForceFlags(void* a_geometry)
	{
		if (!g_forceFlags.load(std::memory_order_relaxed)) {
			return;
		}
		const auto* geo = reinterpret_cast<const std::uint8_t*>(a_geometry);
		auto* prop = *reinterpret_cast<std::uint8_t* const*>(geo + 0x138);
		if (!prop) {
			return;
		}
		auto& flags = *reinterpret_cast<std::uint64_t*>(prop + 0x30);
		flags = (flags | 0x900ull) & ~0x10ull;
	}

	void OnVisibleProbe(void* a_this, void* a_cullingProcess)
	{
		MaybeForceFlags(a_this);
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

		TryNearCapture(reinterpret_cast<std::uintptr_t>(a_this));
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

	void SetForceFlags(bool a_on) noexcept
	{
		g_forceFlags.store(a_on, std::memory_order_relaxed);
	}

	void ArmNearCapture(float a_x, float a_y, float a_z) noexcept
	{
		const std::scoped_lock lock{ g_mutex };
		g_nearPoint[0] = a_x;
		g_nearPoint[1] = a_y;
		g_nearPoint[2] = a_z;
		g_near.clear();
		g_nearArmed = true;
	}

	std::vector<NearGeometry> NearCaptures()
	{
		const std::scoped_lock lock{ g_mutex };
		g_nearArmed = false;
		return g_near;
	}

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
