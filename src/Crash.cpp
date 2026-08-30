#include <windows.h>

#include "PCH.h"

#include "Crash.h"

#include <atomic>

namespace
{
	std::atomic_int g_logged{ 0 };

	[[nodiscard]] bool IsFatal(DWORD a_code) noexcept
	{
		switch (a_code) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_PRIV_INSTRUCTION:
		case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN / __fastfail
			return true;
		default:
			return false;
		}
	}

	LONG WINAPI OnException(EXCEPTION_POINTERS* a_info)
	{
		const auto* record = a_info ? a_info->ExceptionRecord : nullptr;
		// The engine handles some exceptions internally; only fatal codes are
		// interesting, and only the first few in case something crashes in a
		// loop.
		if (record && IsFatal(record->ExceptionCode) &&
			g_logged.fetch_add(1, std::memory_order_relaxed) < 4) {
			const auto rip = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
			const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
			if (rip >= base && rip - base < 0x10000000) {
				REX::CRITICAL("fatal exception {:#x} at Fallout4.exe+{:#x}",
					record->ExceptionCode, rip - base);
			} else {
				REX::CRITICAL("fatal exception {:#x} at {:#x} (outside Fallout4.exe)",
					record->ExceptionCode, rip);
			}
			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
				record->NumberParameters >= 2) {
				REX::CRITICAL("  access violation {} address {:#x}",
					record->ExceptionInformation[0] ? "writing" : "reading",
					record->ExceptionInformation[1]);
			}
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

namespace Crash
{
	void Install()
	{
		// First in the vectored chain so nothing swallows the record before
		// it is logged; CONTINUE_SEARCH keeps dispatch untouched.
		AddVectoredExceptionHandler(1, OnException);
		REX::INFO("crash logger installed");
	}
}
