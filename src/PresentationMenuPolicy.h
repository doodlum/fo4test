#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <RE/Fallout.h>

namespace fo4cs::PresentationMenuPolicy
{
	// VATS, HUD, dialogue, and Workshop remain gameplay surfaces.
	inline constexpr std::array kFrameGenerationBlockingMenus{
		std::string_view{ "MainMenu" },           std::string_view{ "LoadingMenu" },
		std::string_view{ "FaderMenu" },          std::string_view{ "PauseMenu" },
		std::string_view{ "PipboyMenu" },         std::string_view{ "TerminalMenu" },
		std::string_view{ "ExamineMenu" },        std::string_view{ "ExamineConfirmMenu" },
		std::string_view{ "ContainerMenu" },      std::string_view{ "BarterMenu" },
		std::string_view{ "LockpickingMenu" },    std::string_view{ "MessageBoxMenu" },
		std::string_view{ "SitWaitMenu" },        std::string_view{ "HolotapeMenu" },
		std::string_view{ "PipboyHolotapeMenu" }, std::string_view{ "TerminalHolotapeMenu" },
		std::string_view{ "PowerArmorModMenu" }
	};

	// This is intentionally a strict subset of the broader FrameGen block list.
	// Main/loading/fader screens retain their specialized presentation policy.
	inline constexpr std::array kNativePresentationMenus{
		std::string_view{ "PipboyMenu" },         std::string_view{ "ContainerMenu" },
		std::string_view{ "BarterMenu" },         std::string_view{ "ExamineMenu" },
		std::string_view{ "ExamineConfirmMenu" }, std::string_view{ "TerminalMenu" },
		std::string_view{ "LockpickingMenu" },    std::string_view{ "PowerArmorModMenu" },
		std::string_view{ "PauseMenu" },          std::string_view{ "MessageBoxMenu" },
		std::string_view{ "SitWaitMenu" },        std::string_view{ "HolotapeMenu" },
		std::string_view{ "PipboyHolotapeMenu" }, std::string_view{ "TerminalHolotapeMenu" }
	};

	inline constexpr std::uint32_t kFrameGenerationPostMenuSettlePresents = 120;

	constexpr bool NativePresentationMenusBlockFrameGeneration()
	{
		for (const auto nativeMenu : kNativePresentationMenus) {
			bool found = false;
			for (const auto blockedMenu : kFrameGenerationBlockingMenus) {
				if (nativeMenu == blockedMenu) {
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
		return true;
	}

	static_assert(NativePresentationMenusBlockFrameGeneration());

	template <std::size_t N>
	std::optional<std::string_view> FindOpenMenu(::RE::UI* a_ui, const std::array<std::string_view, N>& a_menus)
	{
		if (!a_ui)
			return std::nullopt;

		for (const auto menu : a_menus) {
			if (a_ui->GetMenuOpen(menu.data()))
				return menu;
		}
		return std::nullopt;
	}

	inline std::optional<std::string_view> GetOpenFrameGenerationBlockingMenu()
	{
		return FindOpenMenu(::RE::UI::GetSingleton(), kFrameGenerationBlockingMenus);
	}

	inline std::optional<std::string_view> GetOpenNativePresentationMenu()
	{
		return FindOpenMenu(::RE::UI::GetSingleton(), kNativePresentationMenus);
	}
}
