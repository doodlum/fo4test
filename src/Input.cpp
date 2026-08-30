// windows.h defines an ERROR macro that would eat REX::ERROR; keep GDI out
// and undefine defensively, same as Capture.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#ifdef ERROR
#	undef ERROR
#endif

#include "PCH.h"

#include "Input.h"

namespace Input
{
	bool FocusGameWindow()
	{
		auto* window = RE::BSGraphics::GetCurrentRendererWindow();
		if (!window || !window->hwnd) {
			REX::ERROR("no renderer window; cannot focus for synthetic input");
			return false;
		}

		auto hwnd = reinterpret_cast<HWND>(window->hwnd);

		// A background process cannot just take focus.  Attaching to the
		// foreground window's input queue first is the standard way around
		// that, and we are inside the game process anyway.
		const DWORD self = GetCurrentThreadId();
		const DWORD fore = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
		if (fore && fore != self) {
			AttachThreadInput(self, fore, TRUE);
		}

		ShowWindow(hwnd, SW_SHOW);
		SetForegroundWindow(hwnd);
		SetActiveWindow(hwnd);
		SetFocus(hwnd);

		if (fore && fore != self) {
			AttachThreadInput(self, fore, FALSE);
		}

		return GetForegroundWindow() == hwnd;
	}

	void TapKey(std::uint16_t a_vk)
	{
		INPUT in[2]{};

		in[0].type = INPUT_KEYBOARD;
		in[0].ki.wVk = a_vk;
		in[0].ki.wScan = static_cast<WORD>(MapVirtualKeyW(a_vk, MAPVK_VK_TO_VSC));

		in[1] = in[0];
		in[1].ki.dwFlags = KEYEVENTF_KEYUP;

		SendInput(1, &in[0], sizeof(INPUT));
		Sleep(60);
		SendInput(1, &in[1], sizeof(INPUT));
	}
}
