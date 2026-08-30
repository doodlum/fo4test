#include "PCH.h"

#include "Dump.h"

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
