#include "PCH.h"

#include "PrevisLightingFix.h"

namespace
{
	// REL::ID(2318289) is the world-draw function that chooses between the
	// scene-graph traversal and the precomputed previs list.  +0x192 is the
	// `call` that accumulates the previs batch; rcx holds that batch.
	constexpr std::uint64_t  kDrawWorldID = 2318289;
	constexpr std::ptrdiff_t kAccumulateCallOffset = 0x192;

	// Control byte selecting the per-object lighting path, and the batch
	// field the "set" path forwards.
	constexpr std::ptrdiff_t kLightingPathFlag = 0x169;

	// The sole call into the function that branches on the flag.
	constexpr std::uint64_t  kConsumerCallerID = 2275918;
	constexpr std::ptrdiff_t kConsumerCallOffset = 0x2e2;

	using Accumulate_t = void (*)(std::uint8_t*, void*, void*, std::uint64_t);
	using Consume_t = void (*)(std::uint8_t*, void*, void*);

	Accumulate_t          g_original{ nullptr };
	Consume_t             g_originalConsume{ nullptr };
	std::atomic_uint64_t  g_hits{ 0 };
	bool                  g_installed{ false };

	void ConsumePrevisBatch(std::uint8_t* a_batch, void* a_buffer, void* a_arg)
	{
		if (a_batch) {
			a_batch[kLightingPathFlag] = 1;
			g_hits.fetch_add(1, std::memory_order_relaxed);
		}
		g_originalConsume(a_batch, a_buffer, a_arg);
	}

	void AccumulatePrevisBatch(std::uint8_t* a_batch, void* a_node, void* a_end,
		std::uint64_t a_flags)
	{
		if (a_batch) {
			// The traversal batch always has this set; the previs batch is
			// rebuilt each frame without it.  Setting it here -- immediately
			// before the accumulation that reads it -- is what puts previs
			// geometry back on the same lighting path.
			a_batch[kLightingPathFlag] = 1;
			g_hits.fetch_add(1, std::memory_order_relaxed);
		}

		g_original(a_batch, a_node, a_end, a_flags);
	}
}

namespace PrevisLightingFix
{
	bool Installed() noexcept { return g_installed; }

	std::uint64_t HitCount() noexcept { return g_hits.load(std::memory_order_relaxed); }

	bool Install(Mode a_mode)
	{
		if (g_installed || a_mode == Mode::kOff) {
			return g_installed;
		}

		const auto id = (a_mode == Mode::kConsumer) ? kConsumerCallerID : kDrawWorldID;
		const auto diff =
			(a_mode == Mode::kConsumer) ? kConsumerCallOffset : kAccumulateCallOffset;

		const REL::Relocation<std::uintptr_t> owner{ REL::ID(id) };
		const auto site = owner.address() + diff;

		// Refuse to patch anything that is not the call we mapped, so a
		// different runtime or a shifted address library fails loudly instead
		// of corrupting the render path.
		const auto opcode = *reinterpret_cast<const std::uint8_t*>(site);
		if (opcode != 0xE8) {
			REX::ERROR("previs lighting fix: REL::ID({})+{:#x} is {:#04x}, not a call; "
					   "refusing to install",
				id, diff, opcode);
			return false;
		}

		auto& trampoline = REL::GetTrampoline();
		const auto detour = (a_mode == Mode::kConsumer)
								? reinterpret_cast<std::uintptr_t>(&ConsumePrevisBatch)
								: reinterpret_cast<std::uintptr_t>(&AccumulatePrevisBatch);
		const auto original = trampoline.write_call<5>(site, detour);

		if (!original) {
			REX::ERROR("previs lighting fix: trampoline refused the call detour");
			return false;
		}

		if (a_mode == Mode::kConsumer) {
			g_originalConsume = reinterpret_cast<Consume_t>(original);
		} else {
			g_original = reinterpret_cast<Accumulate_t>(original);
		}

		g_installed = true;
		REX::INFO("previs lighting fix installed ({}) at REL::ID({})+{:#x} ({:#x})",
			a_mode == Mode::kConsumer ? "consumer" : "accumulate", id, diff, site);
		return true;
	}
}
