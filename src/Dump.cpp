#include "PCH.h"

#include "Dump.h"

#include "SubmitProbe.h"
#include "VisibilityProbe.h"

#include <format>
#include <iomanip>
#include <sstream>

namespace
{
	// The two globals are 0x2E0 apart in .data, so that is the most either can
	// occupy without overlapping the other.
	constexpr std::size_t kSize = 0x2E0;

	struct Global
	{
		const char*   name;
		std::uint64_t id;
	};

	constexpr Global kGlobals[] = {
		{ "param_2 (0x142f97c40)", 4784648 },
		{ "param_3 traversal (0x142f97db0)", 4784655 },
		{ "param_4 previs    (0x142f98090)", 4784667 },
	};

	void HexDump(std::ostream& a_os, const std::uint8_t* a_data, std::size_t a_size)
	{
		for (std::size_t off = 0; off < a_size; off += 16) {
			a_os << std::format("  +{:04X}  ", off);
			for (std::size_t i = 0; i < 16; ++i) {
				if (off + i < a_size) {
					a_os << std::format("{:02X} ", a_data[off + i]);
				} else {
					a_os << "   ";
				}
				if (i == 7) {
					a_os << ' ';
				}
			}
			a_os << '\n';
		}
	}
}

namespace Dump
{
	bool ObjectFlags(const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::ofstream file{ a_path, std::ios::app };
		if (!file) {
			return false;
		}

		const auto records = VisibilityProbe::Records();
		file << "===== " << a_label << " ===== " << records.size() << " objects\n";
		for (const auto& r : records) {
			file << std::format("{:#x} {:#018x} {}\n", r.geometry, r.flags, r.calls);
		}
		file << '\n';
		REX::INFO("dumped {} object flag records ({})", records.size(), a_label);
		return true;
	}

	bool NearGeometry(const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::ofstream file{ a_path, std::ios::app };
		if (!file) {
			return false;
		}
		const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		const auto caps = VisibilityProbe::NearCaptures();
		file << "===== " << a_label << " ===== " << caps.size() << " capture(s)\n";
		for (const auto& c : caps) {
			const auto vt = *reinterpret_cast<const std::uintptr_t*>(c.geoBytes.data());
			file << std::format(
				"\n-- geo {:#x} d={:.1f} vtrva {:#x} prop0={:#x} prop1={:#x}\n",
				c.geometry, c.distance,
				(vt > base && vt - base < 0x10000000) ? vt - base : 0, c.prop0,
				c.prop1);
			file << "geo bytes:\n";
			HexDump(file, c.geoBytes.data(), c.geoBytes.size());
			if (c.prop0) {
				file << "prop0 bytes:\n";
				HexDump(file, c.prop0Bytes.data(), c.prop0Bytes.size());
			}
			if (c.prop1) {
				file << "prop1 bytes:\n";
				HexDump(file, c.prop1Bytes.data(), c.prop1Bytes.size());
			}
			file << std::format("prop1Valid={} fadeNode={:#x} fadeValid={}\n",
				c.prop1Valid, c.fadeNode, c.fadeValid);
			if (c.fadeValid) {
				file << "fadeNode bytes:\n";
				HexDump(file, c.fadeBytes.data(), c.fadeBytes.size());
			}
			file << std::format("passCount={}\n", c.passCount);
			for (std::uint32_t pi = 0; pi < c.passCount; ++pi) {
				file << std::format("pass[{}] bytes:\n", pi);
				HexDump(file, c.passBytes[pi].data(), 0x40);
			}
		}
		file << '\n';
		REX::INFO("dumped {} near-geometry capture(s) ({})", caps.size(), a_label);
		return true;
	}

	bool SubmittedBatches(const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::ofstream file{ a_path, std::ios::app };
		if (!file) {
			return false;
		}

		file << "===== " << a_label << " =====\n";
		static constexpr const char* kNames[3] = { "previs-side batch",
			"previs-off-side batch", "traversal batch" };

		for (std::size_t i = 0; i < 3; ++i) {
			const auto snap = SubmitProbe::Get(i);
			file << std::format(
				"\n-- {} valid={} batch={:#x} calls={} buffer={:#x} count={} "
				"tailFlag={:#04x} captured={} lightBuf={:#x} lightCount={}\n",
				kNames[i], snap.valid, snap.batch, snap.calls, snap.buffer, snap.count,
				snap.tailFlag, snap.captured, snap.lightBuffer, snap.lightCount);
			if (snap.valid) {
				const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
				for (std::uint32_t e = 0; e < snap.captured; ++e) {
					// Report the vtable module-relative so it can be mapped
					// back to a class through IDs_VTABLE.h despite ASLR.
					const auto vt = snap.vtables[e];
					const auto vtRva =
						(vt > base && vt - base < 0x10000000) ? vt - base : 0;
					file << std::format(
						"   entry {:4} obj {:#x} flags {:#06x} vtrva {:#x} "
						"pos {:.1f},{:.1f},{:.1f} r {:.1f} objflags {:#018x} "
						"affected {}\n",
						e, snap.objects[e], snap.entryFlags[e], vtRva, snap.bounds[e][0],
						snap.bounds[e][1], snap.bounds[e][2], snap.bounds[e][3],
						snap.objFlags[e], snap.affected[e]);
				}
			}
		}

		file << '\n';
		return true;
	}

	bool CullingProcesses(const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::ofstream file{ a_path, std::ios::app };
		if (!file) {
			return false;
		}

		const auto snapshot = VisibilityProbe::LastSnapshot();
		if (snapshot.valid) {
			file << std::format(
				"-- in-hook snapshot: process {:#x} geometry {:#x} flags {:#018x}\n",
				snapshot.process, snapshot.geometry, snapshot.geometryFlags);
			HexDump(file, snapshot.bytes.data(), snapshot.bytes.size());
			file << '\n';
		}

		const auto observed = VisibilityProbe::Processes();
		const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();

		file << "===== " << a_label << " =====\n";
		file << observed.size() << " distinct culling process(es)\n";

		for (const auto& o : observed) {
			file << std::format("\n-- process {:#x}  ({} calls)", o.process, o.calls);
			if (o.process > base && o.process - base < 0x10000000) {
				file << std::format("  module+{:#x}", o.process - base);
			}
			file << '\n';
			HexDump(file, reinterpret_cast<const std::uint8_t*>(o.process), 0x170);
		}

		file << '\n';
		REX::INFO("dumped {} culling process(es) ({})", observed.size(), a_label);
		return true;
	}

	bool Accumulators(const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::ofstream file{ a_path, std::ios::app };
		if (!file) {
			REX::ERROR("could not open {}", a_path.string());
			return false;
		}

		file << "===== " << a_label << " =====\n";

		for (const auto& g : kGlobals) {
			const REL::Relocation<std::uintptr_t> reloc{ REL::ID(g.id) };
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(reloc.address());

			file << "\n-- " << g.name << "  REL::ID(" << g.id << ")  @ "
				 << std::format("{:#x}", reloc.address()) << '\n';

			// The first qword of a polymorphic object is its vtable; printing
			// it as a module-relative value makes the two comparable across
			// runs despite ASLR.
			std::uint64_t vtbl{};
			std::memcpy(&vtbl, bytes, sizeof(vtbl));
			const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
			if (vtbl > base && vtbl - base < 0x10000000) {
				file << std::format("   vtable rva {:#x}\n", vtbl - base);
			} else {
				file << std::format("   first qword {:#x}\n", vtbl);
			}

			HexDump(file, bytes, kSize);
		}

		file << '\n';
		REX::INFO("dumped accumulators ({}) to {}", a_label, a_path.string());
		return true;
	}
}
