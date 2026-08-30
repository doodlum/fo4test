#pragma once

// Harness configuration, read once at plugin load from
// Data/F4SE/Plugins/fo4test.ini.  Every field has a working default, so a
// missing INI still produces the FourLeafFishpacking02 previs run.
struct Settings
{
	// Run the harness automatically on startup.  Set to false to load the
	// plugin without driving the game (useful when attaching a debugger).
	bool enabled{ true };

	// Fallout 4 has no console at the main menu, so `coc` issued there is
	// silently swallowed.  Load the most recent save first to get into a real
	// game, then coc from there.  Turn this off only if something else is
	// already putting you in-game.
	bool loadSaveFirst{ true };

	// Cell editor ID passed to `coc`.
	std::string cell{ "FourLeafFishpacking02" };

	// Commands run once the coc load has finished, before the first capture.
	// `tm` hides the HUD and `tfc 1` freezes the game and detaches the camera,
	// which is what makes the two captures differ ONLY by the previs toggle.
	std::vector<std::string> setupCommands{ "tm", "tfc 1" };

	// The command under test, run between the two captures.
	std::string toggleCommand{ "tpc" };

	// Optional absolute camera pose, applied after setupCommands.  Leave
	// usePose false to shoot from wherever `coc` drops the player.
	bool  usePose{ false };
	float posX{ 0.0f };
	float posY{ 0.0f };
	float posZ{ 0.0f };
	float angX{ 0.0f };
	float angZ{ 0.0f };

	// Wall-clock waits, in milliseconds.  These are deliberately generous:
	// the harness is not trying to be fast, it is trying to be repeatable on
	// a cold shader cache.
	std::uint32_t mainMenuDelay{ 8000 };   // main menu -> coc
	std::uint32_t loadTimeout{ 180000 };   // give up waiting for the load
	std::uint32_t loadSettleDelay{ 8000 }; // load done -> setup commands
	std::uint32_t settleDelay{ 4000 };     // setup/toggle -> capture

	// Where the .bmp captures and result.json land.  Empty means
	// Documents/My Games/Fallout4/F4SE/fo4test.
	std::string outputDir{};

	// Quit the game with `qqq` once both captures are on disk, so an
	// unattended run terminates by itself.
	bool quitWhenDone{ true };

	[[nodiscard]] static Settings& GetSingleton() noexcept;

	// Reads the INI if present.  Never throws; unparseable values keep their
	// default and are reported in the log.
	void Load();

	// Resolves outputDir, creating it if needed.  Returns an empty path when
	// the directory could not be created.
	[[nodiscard]] std::filesystem::path ResolveOutputDir() const;
};
