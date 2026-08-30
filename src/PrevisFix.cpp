#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#ifdef ERROR
#	undef ERROR
#endif

#include "PCH.h"

#include "PrevisFix.h"

#include <charconv>
#include <set>

namespace
{
	struct Site
	{
		std::uint64_t  id;      // address library ID of the containing function
		std::ptrdiff_t offset;  // byte offset of the `call rel32` within it
	};

	// Every direct call to 0x1421ae520 ("is pre-culling active"), located by
	// scanning .text for E8 rel32 targeting it and attributing each hit to the
	// address library entry it falls inside.  Offsets are within the function,
	// which the goal permits; the function itself is always an ID.
	constexpr Site kSites[] = {
		{ 2201227, 0x15 },   // [ 0] 0x14051d0c5
		{ 2205639, 0x4b },   // [ 1] 0x1405fa47b  -- inside the tpc handler, message only
		{ 2208823, 0x232 },  // [ 2] 0x1406b8582
		{ 2213386, 0x0f },   // [ 3] 0x1407c30df
		{ 2213392, 0xf8 },   // [ 4] 0x1407c36d8
		{ 2213610, 0x13 },   // [ 5] 0x1407d7a13
		{ 2228917, 0x6c9 },  // [ 6] 0x140c34099  -- Main::OnIdle region
		{ 2228949, 0x47 },   // [ 7] 0x140c37667
		{ 2232922, 0x1b1 },  // [ 8] 0x140d5a821
		{ 2275908, 0x29 },   // [ 9] 0x1417debb9
		{ 2275909, 0x19 },   // [10] 0x1417dee49
		{ 2275918, 0x29c },  // [11] 0x1417e050c
		{ 2275918, 0x38e },  // [12] 0x1417e05fe
		{ 2275918, 0x469 },  // [13] 0x1417e06d9
		{ 2275918, 0x5aa },  // [14] 0x1417e081a
		{ 2275945, 0x128 },  // [15] 0x1417e2958
		{ 2275946, 0x128 },  // [16] 0x1417e2ce8
		{ 2275962, 0x31d },  // [17] 0x1417e3d4d
		{ 2275963, 0xef },   // [18] 0x1417e3eaf
		{ 2275964, 0x139 },  // [19] 0x1417e4039
		{ 2316459, 0x333 },  // [20] 0x14217d303  -- BSFadeNode::OnVisible
		{ 2317367, 0x3f },   // [21] 0x1421b1c3f
		{ 2318286, 0x2b },   // [22] 0x1421f0aeb
		{ 2318289, 0x165 },  // [23] 0x1421f1175
		{ 2318289, 0x19f },  // [24] 0x1421f11af
		{ 2318289, 0x1c5 },  // [25] 0x1421f11d5
		{ 2318289, 0xb48 },  // [26] 0x1421f1b58
		{ 2318291, 0x07 },   // [27] 0x1421f21f7
		{ 2318292, 0xbc },   // [28] 0x1421f241c
		{ 2318292, 0x1d0 },  // [29] 0x1421f2530
		{ 2318292, 0x20c },  // [30] 0x1421f256c
		{ 2318292, 0x377 },  // [31] 0x1421f26d7
		{ 2318292, 0x3cb },  // [32] 0x1421f272b
		{ 2318292, 0x485 },  // [33] 0x1421f27e5
		{ 2318292, 0x532 },  // [34] 0x1421f2892
		{ 2318321, 0x75 },   // [35] 0x1421f9d05
		{ 2318321, 0x2b0 },  // [36] 0x1421f9f40
		{ 2319338, 0xb9 },   // [37] 0x1422504b9
	};

	constexpr std::size_t kSiteCount = std::size(kSites);

	// xor eax, eax ; nop ; nop ; nop  -- same length as the call it replaces,
	// and leaves al = 0 for the `test al, al` that always follows.
	constexpr std::uint8_t kFalse[5] = { 0x31, 0xC0, 0x90, 0x90, 0x90 };

	[[nodiscard]] std::set<std::size_t> ParseSpec(std::string_view a_spec)
	{
		std::set<std::size_t> out;
		if (a_spec.empty() || a_spec == "none") {
			return out;
		}
		if (a_spec == "all") {
			for (std::size_t i = 0; i < kSiteCount; ++i) {
				out.insert(i);
			}
			return out;
		}

		const auto number = [](std::string_view s, std::size_t& v) {
			const auto* first = s.data();
			const auto* last = s.data() + s.size();
			return std::from_chars(first, last, v).ec == std::errc{};
		};

		std::size_t pos = 0;
		while (pos <= a_spec.size()) {
			const auto comma = a_spec.find(',', pos);
			auto token = a_spec.substr(pos, comma == std::string_view::npos
											   ? std::string_view::npos
											   : comma - pos);
			pos = (comma == std::string_view::npos) ? a_spec.size() + 1 : comma + 1;

			while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
			while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
			if (token.empty()) {
				continue;
			}

			const auto dash = token.find('-');
			if (dash == std::string_view::npos) {
				std::size_t v{};
				if (number(token, v) && v < kSiteCount) {
					out.insert(v);
				}
			} else {
				std::size_t lo{}, hi{};
				if (number(token.substr(0, dash), lo) &&
					number(token.substr(dash + 1), hi)) {
					for (auto i = lo; i <= hi && i < kSiteCount; ++i) {
						out.insert(i);
					}
				}
			}
		}
		return out;
	}

	bool PatchSite(std::size_t a_index)
	{
		const auto& site = kSites[a_index];
		const REL::Relocation<std::uintptr_t> fn{ REL::ID(site.id) };
		auto* target = reinterpret_cast<std::uint8_t*>(fn.address() + site.offset);

		// Refuse to write unless it really is the call we mapped.  A shifted
		// address library or a different runtime would otherwise corrupt code.
		if (target[0] != 0xE8) {
			REX::ERROR("site[{}] REL::ID({})+{:#x} is {:#04x}, not a call (0xE8); skipping",
				a_index, site.id, site.offset, target[0]);
			return false;
		}

		DWORD old{};
		if (!VirtualProtect(target, sizeof(kFalse), PAGE_EXECUTE_READWRITE, &old)) {
			REX::ERROR("site[{}]: VirtualProtect failed", a_index);
			return false;
		}
		std::memcpy(target, kFalse, sizeof(kFalse));
		VirtualProtect(target, sizeof(kFalse), old, &old);
		FlushInstructionCache(GetCurrentProcess(), target, sizeof(kFalse));

		REX::INFO("site[{:2}] patched: REL::ID({})+{:#x} @ {:#x}", a_index, site.id,
			site.offset, reinterpret_cast<std::uintptr_t>(target));
		return true;
	}
}

namespace PrevisFix
{
	void ForcePrevisBatchFlag()
	{
		// The previs-side culling batch, from the r9 argument of the world
		// draw's call into FUN_1421f1010.
		static REL::Relocation<std::uint8_t*> previsBatch{ REL::ID(4784667) };
		constexpr std::ptrdiff_t kFlagOffset = 0x169;

		auto* flag = previsBatch.get() + kFlagOffset;
		if (*flag != 1) {
			*flag = 1;
		}
	}

	std::size_t SiteCount() noexcept { return kSiteCount; }

	std::size_t Apply(std::string_view a_spec)
	{
		const auto wanted = ParseSpec(a_spec);
		if (wanted.empty()) {
			REX::INFO("previs fix: no sites selected ({} available)", kSiteCount);
			return 0;
		}

		std::size_t done = 0;
		for (const auto i : wanted) {
			if (PatchSite(i)) {
				++done;
			}
		}
		REX::INFO("previs fix: patched {}/{} selected site(s) from spec '{}'", done,
			wanted.size(), a_spec);
		return done;
	}
}
