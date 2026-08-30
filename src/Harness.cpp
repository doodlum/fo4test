#include "PCH.h"

#include "Harness.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include "Capture.h"
#include "Dump.h"
#include "Input.h"
#include "PrevisFix.h"
#include "PrevisLightingFix.h"
#include "SubmitProbe.h"
#include "VisibilityProbe.h"
#include "Settings.h"

#include <format>
#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	struct Job
	{
		std::function<void()> fn;
		std::promise<void>    done;
	};

	// Runs a_fn on the game thread and blocks until it has finished.  The job
	// is heap-owned and captured by value so a timeout (game shutting down,
	// task queue drained) cannot leave the delegate holding dangling
	// references to our stack.
	bool RunOnGameThread(std::function<void()> a_fn, std::chrono::milliseconds a_timeout = 30s)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			return false;
		}

		auto job = std::make_shared<Job>();
		job->fn = std::move(a_fn);
		auto future = job->done.get_future();

		tasks->AddTask([job]() {
			job->fn();
			job->done.set_value();
		});

		return future.wait_for(a_timeout) == std::future_status::ready;
	}

	void RunCommand(std::string a_command)
	{
		REX::INFO("console: {}", a_command);
		RunOnGameThread([command = std::move(a_command)]() {
			RE::Console::ExecuteCommand(command.c_str());
		});
	}

	// Polls a_predicate on the game thread until it is true or the deadline
	// passes.  Returns whether the predicate ever came back true.
	bool WaitFor(
		std::string_view a_what, std::function<bool()> a_predicate,
		std::chrono::milliseconds a_timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			auto hit = std::make_shared<std::atomic_bool>(false);
			// Copy the predicate into the delegate rather than capturing it by
			// reference: on the timeout path RunOnGameThread returns while the
			// task is still queued, and a reference to our stack would dangle
			// if it ran afterwards.
			if (!RunOnGameThread([hit, a_predicate]() { hit->store(a_predicate()); })) {
				REX::WARN("game thread stopped answering while waiting for {}", a_what);
				return false;
			}
			if (hit->load()) {
				return true;
			}
			std::this_thread::sleep_for(250ms);
		}

		REX::WARN("timed out after {}ms waiting for {}", a_timeout.count(), a_what);
		return false;
	}

	[[nodiscard]] bool IsLoadingMenuOpen()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen<RE::LoadingMenu>();
	}

	// Newest save whose file name contains a_substring (case-insensitive),
	// as the extensionless name the console `load` command takes.
	[[nodiscard]] std::string ResolveSaveName(std::string_view a_substring)
	{
		wchar_t*   buffer{ nullptr };
		const auto result = REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents,
			REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));
		const std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(
			buffer, REX::W32::CoTaskMemFree);
		if (!owned || result != 0) {
			return {};
		}
		std::filesystem::path dir = owned.get();
		dir /= "My Games";
		dir /= F4SE::GetSaveFolderName();
		dir /= "Saves";
		auto lower = [](std::string s) {
			for (auto& c : s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return s;
		};
		const auto needle = lower(std::string{ a_substring });

		std::string best;
		std::filesystem::file_time_type bestTime{};
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator{ dir, ec }) {
			if (!entry.is_regular_file(ec) ||
				entry.path().extension() != ".fos") {
				continue;
			}
			const auto stem = entry.path().stem().string();
			if (lower(stem).find(needle) == std::string::npos) {
				continue;
			}
			const auto time = entry.last_write_time(ec);
			if (best.empty() || time > bestTime) {
				best = stem;
				bestTime = time;
			}
		}
		return best;
	}

	[[nodiscard]] bool IsInWorld()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player && player->GetParentCell() != nullptr;
	}

	[[nodiscard]] bool IsConsoleOpen()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen(RE::Console::MENU_NAME);
	}

	void SetConsoleVisible(bool a_show)
	{
		RunOnGameThread([a_show]() {
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::Console::MENU_NAME,
					a_show ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide);
			}
		});
	}

	// --- MainMenu layout on Fallout 4 1.11.240 ---------------------------
	//
	// CommonLibF4 declares StartMenuBase as 0x228 bytes, so it puts MainMenu's
	// own members at 0x230 onwards.  The real object is 0x20 bytes bigger: the
	// constructor (reached from VTABLE::MainMenu, REL::ID(643428)) installs its
	// second base's vtable at +0x248, and initialises
	//
	//     mov dword ptr [rsi+0x290], r12d   ; r12 = -1  -> queuedLoadIndex
	//     mov dword ptr [rsi+0x298], 0x10000           -> allowSkip = 1
	//
	// which are exactly the header's 0x270 (queuedLoadIndex, inits to -1) and
	// 0x27A (allowSkip, inits to true, byte 2 of that dword) shifted up 0x20.
	// Two independent initialised values pin both the shift and the field order.
	//
	// Writing queueContinueGame at the header's 0x276 lands in the middle of
	// another member and does nothing -- which is what the first attempt did,
	// logging exitCondition=538521264 and queuedLoadIndex=-43998920.
	inline constexpr std::ptrdiff_t kMainMenuMemberDelta = 0x20;
	inline constexpr std::ptrdiff_t kQueuedLoadIndexOffset = 0x270 + kMainMenuMemberDelta;
	inline constexpr std::ptrdiff_t kQueueContinueGameOffset = 0x276 + kMainMenuMemberDelta;

	// Confirms the pointer really is a MainMenu before we write a raw offset
	// into it: the vtable slot must hold VTABLE::MainMenu, resolved through the
	// address library rather than hardcoded.
	[[nodiscard]] bool LooksLikeMainMenu(const void* a_menu)
	{
		if (!a_menu) {
			return false;
		}
		const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_menu);
		const auto expected = RE::VTABLE::MainMenu[0].address();
		if (vtbl != expected) {
			REX::ERROR("not a MainMenu: vtable {:#x} != VTABLE::MainMenu {:#x}", vtbl, expected);
			return false;
		}
		// queuedLoadIndex is -1 on a freshly built main menu; a second,
		// independent check that the 0x20 shift still holds on this build.
		const auto queued = *reinterpret_cast<const std::int32_t*>(
			reinterpret_cast<const std::uint8_t*>(a_menu) + kQueuedLoadIndexOffset);
		if (queued != -1) {
			REX::WARN("queuedLoadIndex at +{:#x} is {}, expected -1 -- MainMenu layout "
					  "may have shifted again on this runtime", kQueuedLoadIndexOffset, queued);
		}
		return true;
	}

	// Dumps the menu's own view of itself, using the offsets verified against
	// the constructor.  Tells "the menu has no Continue" apart from "the menu
	// has not become interactive yet", which look identical from outside.
	void LogMainMenuState(std::string_view a_when)
	{
		RunOnGameThread([when = std::string{ a_when }]() {
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			REX::INFO("[{}] MainMenu open={} LoadingMenu open={}", when,
				ui->GetMenuOpen<RE::MainMenu>(), ui->GetMenuOpen<RE::LoadingMenu>());

			const auto menu = ui->GetMenu<RE::MainMenu>();
			if (!menu) {
				return;
			}
			auto* raw = reinterpret_cast<const std::uint8_t*>(menu.get());
			if (!LooksLikeMainMenu(raw)) {
				return;
			}
			const auto b = [&](std::ptrdiff_t o) { return static_cast<int>(raw[o]); };
			REX::INFO("[{}] exitCondition={} queuedLoadIndex={} | choseContinue={} "
					  "queueStartNewGame={} queueContinueGame={} creditScreen={} "
					  "userEngaged={} mainBinkShown={} allowSkip={} debounce={}",
				when,
				*reinterpret_cast<const std::int32_t*>(raw + 0x250),
				*reinterpret_cast<const std::int32_t*>(raw + kQueuedLoadIndexOffset),
				b(0x294), b(0x295), b(0x296), b(0x297),
				b(0x298), b(0x299), b(0x29a), b(0x29b));
		});
	}

	// The engine's frame counter.  Sampling it over a wall-clock window gives
	// a render-cost figure without hooking Present, which matters here: the
	// whole point of previs is performance, so a "fix" that silently turns it
	// off has to be detectable.  `tfc 1` has already frozen the simulation, so
	// what this measures is rendering.
	[[nodiscard]] double MeasureFps(std::chrono::milliseconds a_window)
	{
		static REL::Relocation<std::uint32_t*> frameCounter{ REL::ID(4784456) };

		auto read = [&]() {
			auto v = std::make_shared<std::uint32_t>(0);
			RunOnGameThread([v]() { *v = *frameCounter; });
			return *v;
		};

		const auto startFrames = read();
		const auto startTime = std::chrono::steady_clock::now();
		std::this_thread::sleep_for(a_window);
		const auto endFrames = read();
		const auto endTime = std::chrono::steady_clock::now();

		const auto seconds =
			std::chrono::duration<double>(endTime - startTime).count();
		if (seconds <= 0.0 || endFrames < startFrames) {
			return 0.0;
		}
		return static_cast<double>(endFrames - startFrames) / seconds;
	}

	struct WorldState
	{
		std::uint32_t cellFormID{ 0 };
		float         x{ 0.0f };
		float         y{ 0.0f };
		float         z{ 0.0f };
	};

	[[nodiscard]] WorldState ReadWorldState()
	{
		auto state = std::make_shared<WorldState>();
		RunOnGameThread([state]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto position = player->GetPosition();
			state->x = position.x;
			state->y = position.y;
			state->z = position.z;
			if (const auto* cell = player->GetParentCell()) {
				state->cellFormID = cell->GetFormID();
			}
		});
		return *state;
	}

	// Waits out one loading screen.  Fallout 4 will not always have raised it
	// by the time we start looking, so a missed appearance is tolerated -- but
	// only if the player ends up somewhere, which the caller checks.  A
	// loading screen that appears and never leaves is fatal.
	bool WaitOutLoad(std::string_view a_what, std::chrono::milliseconds a_timeout)
	{
		const bool appeared = WaitFor(
			std::format("the {} loading screen to appear", a_what), IsLoadingMenuOpen, 45s);
		if (!appeared) {
			REX::WARN("no loading screen appeared for {}", a_what);
			return false;
		}
		return WaitFor(
			std::format("the {} loading screen to finish", a_what),
			[]() { return !IsLoadingMenuOpen(); }, a_timeout);
	}

	// Minimal hand-rolled JSON so the harness has no serialisation
	// dependency; the file is small and every value is a number or a
	// backslash-free string.
	[[nodiscard]] std::string Quote(std::string_view a_value)
	{
		std::string out;
		out.reserve(a_value.size() + 2);
		out.push_back('"');
		for (const char c : a_value) {
			if (c == '"' || c == '\\') {
				out.push_back('\\');
			}
			out.push_back(c);
		}
		out.push_back('"');
		return out;
	}

	struct Report
	{
		bool          ok{ false };
		std::string   failure{};
		std::string   cell{};
		std::uint32_t cellFormID{ 0 };
		std::uint32_t cellFormIDBeforeCoc{ 0 };
		float         x{ 0.0f };
		float         y{ 0.0f };
		float         z{ 0.0f };
		std::string   toggleCommand{};
		std::string   before{};
		std::string   after{};
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::uint64_t onVisiblePrevisOn{ 0 };
		std::uint64_t onVisiblePrevisOff{ 0 };
		bool          lightingFixInstalled{ false };
		std::uint64_t lightingFixHits{ 0 };
		double        fpsPrevisOn{ 0.0 };
		double        fpsPrevisOff{ 0.0 };
		std::string   previsFixSites{};
	};

	void WriteReport(const std::filesystem::path& a_dir, const Report& a_report)
	{
		const auto    path = a_dir / "result.json";
		std::ofstream file{ path, std::ios::trunc };
		if (!file) {
			REX::ERROR("could not write {}", path.string());
			return;
		}

		file << "{\n"
			 << "  \"ok\": " << (a_report.ok ? "true" : "false") << ",\n"
			 << "  \"failure\": " << Quote(a_report.failure) << ",\n"
			 << "  \"cell\": " << Quote(a_report.cell) << ",\n"
			 << "  \"cellFormID\": " << a_report.cellFormID << ",\n"
			 << "  \"cellFormIDBeforeCoc\": " << a_report.cellFormIDBeforeCoc << ",\n"
			 << "  \"playerX\": " << a_report.x << ",\n"
			 << "  \"playerY\": " << a_report.y << ",\n"
			 << "  \"playerZ\": " << a_report.z << ",\n"
			 << "  \"toggleCommand\": " << Quote(a_report.toggleCommand) << ",\n"
			 << "  \"before\": " << Quote(a_report.before) << ",\n"
			 << "  \"after\": " << Quote(a_report.after) << ",\n"
			 << "  \"width\": " << a_report.width << ",\n"
			 << "  \"height\": " << a_report.height << ",\n"
			 << "  \"fpsPrevisOn\": " << a_report.fpsPrevisOn << ",\n"
			 << "  \"fpsPrevisOff\": " << a_report.fpsPrevisOff << ",\n"
			 << "  \"previsFixSites\": " << Quote(a_report.previsFixSites) << ",\n"
			 << "  \"lightingFixInstalled\": "
			 << (a_report.lightingFixInstalled ? "true" : "false") << ",\n"
			 << "  \"lightingFixHits\": " << a_report.lightingFixHits << ",\n"
			 << "  \"onVisiblePrevisOn\": " << a_report.onVisiblePrevisOn << ",\n"
			 << "  \"onVisiblePrevisOff\": " << a_report.onVisiblePrevisOff << "\n"
			 << "}\n";

		REX::INFO("wrote {}", path.string());
	}

	// Returns the capture's filename on success, empty on failure.
	[[nodiscard]] std::string CaptureTo(
		const std::filesystem::path& a_dir, std::string_view a_name, Report& a_report)
	{
		const auto path = a_dir / a_name;
		auto       result = std::make_shared<Capture::Result>();
		RunOnGameThread([result, path]() { *result = Capture::ToBMP(path); });

		if (!result->ok) {
			REX::ERROR("capture {} failed: {}", a_name, result->error);
			a_report.failure = "capture " + std::string{ a_name } + " failed: " + result->error;
			return {};
		}

		a_report.width = result->width;
		a_report.height = result->height;
		REX::INFO("captured {} ({}x{})", a_name, result->width, result->height);
		return std::string{ a_name };
	}

	void Run()
	{
		const auto& settings = Settings::GetSingleton();

		const auto dir = settings.ResolveOutputDir();
		if (dir.empty()) {
			REX::ERROR("no writable output directory; aborting the run");
			return;
		}
		REX::INFO("output directory: {}", dir.string());

		Report report;
		report.cell = settings.cell;
		report.toggleCommand = settings.toggleCommand;

		bool finished = false;
		const auto finish = [&](std::string_view a_failure) {
			if (finished) {
				return;
			}
			finished = true;
			if (!a_failure.empty()) {
				report.ok = false;
				if (report.failure.empty()) {
					report.failure = a_failure;
				}
				REX::ERROR("harness failed: {}", report.failure);
			}
			WriteReport(dir, report);
			if (settings.quitWhenDone) {
				REX::INFO("quitting");
				RunCommand("qqq");
			}
		};

		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.mainMenuDelay });

		// Console::ExecuteCommand routes through the Console menu, so at the
		// main menu -- where that menu has never been created -- commands are
		// swallowed without an error.  The harness then photographs the menu's
		// animated background twice and reports a 99% "difference".  Showing
		// the console first makes `coc` behave exactly as it does when typed by
		// hand: it starts a new game in the target cell.
		if (settings.loadSaveFirst) {
			// Everything that pokes the menu's state from outside fails here:
			//
			//   BGSSaveLoadManager kLoadMostRecentSave -- currentPlayerID is 0 and
			//     mostRecentSaveGame is null at the menu, so it has nothing to load
			//   kLoadGame with queuedEntryToLoad set -- never consumed
			//   MainMenu::queueContinueGame (+0x296, offset verified against the
			//     constructor) plus the Scaleform invoke at REL::ID(2249309) --
			//     the movie ignores it until the menu has been engaged
			//
			// So press the key.  CONTINUE is the default selection when a save
			// exists, and this drives the same code the shipped game does.
			// Wait for the menu to actually exist rather than trusting a delay.
			if (!WaitFor("the main menu to open", []() {
					const auto* ui = RE::UI::GetSingleton();
					return ui && ui->GetMenuOpen<RE::MainMenu>();
				}, 90s)) {
				finish("the main menu never opened");
				return;
			}
			LogMainMenuState("menu open");

			// Run the Continue branch directly.  Neither the queued flag nor
			// synthetic input gets the menu to do it: the flag is only read by the
			// menu's message handler, and key presses reach the game (mainBinkShown
			// flips) but the list never activates.  The branch itself is small and
			// fully visible, at REL::ID(2249301)+0x53a:
			//
			//     mov  byte ptr [rdi+0x294], 1     ; choseContinue = true
			//     mov  byte ptr [rdi+0x296], 0     ; queueContinueGame = false
			//     mov  rcx, <BGSSaveLoadManager>
			//     call 0x1410856c0                 ; REL::ID(2249692) -- any save?
			//     test al, al
			//     je   <no-save path>
			//     mov  dword ptr [rdi+0x250], 2    ; exitCondition = kContinue
			//     call 0x140c14240                 ; REL::ID(2228370)
			//     call 0x140c37720                 ; Main::DoBeforeNewOrLoad
			//     mov  byte ptr [rip+...], 1       ; REL::ID(2698031)
			//
			// Both calls take no arguments (verified from their prologues).
			//
			// When SaveFile names a specific save, skip the Continue branch
			// (which always takes the newest) and issue a console `load`
			// instead -- at the main menu ExecuteCommand handles script
			// commands, the same route `coc` uses for new games.
			if (!settings.saveFile.empty()) {
				const auto resolved = ResolveSaveName(settings.saveFile);
				if (resolved.empty()) {
					finish("no save matches SaveFile substring '" +
						   settings.saveFile + "'");
					return;
				}
				if (settings.openConsole) {
					SetConsoleVisible(true);
					WaitFor("the console to open", IsConsoleOpen, 15s);
				}
				RunCommand("load " + resolved);
				if (settings.openConsole) {
					SetConsoleVisible(false);
					WaitFor("the console to close",
						[]() { return !IsConsoleOpen(); }, 10s);
				}
				// A save from another character or an older game version pops
				// a confirmation dialog ("relies on content that is no longer
				// present"); accept it, or the load never starts.
				const auto deadline = std::chrono::steady_clock::now() +
					std::chrono::milliseconds{ settings.loadTimeout };
				bool inWorld = false;
				while (std::chrono::steady_clock::now() < deadline) {
					if (WaitFor("the player to be in a cell", IsInWorld, 3s)) {
						inWorld = true;
						break;
					}
					auto boxOpen = std::make_shared<std::atomic_bool>(false);
					RunOnGameThread([boxOpen]() {
						const auto* ui = RE::UI::GetSingleton();
						boxOpen->store(ui && ui->GetMenuOpen("MessageBoxMenu"));
					});
					if (boxOpen->load()) {
						REX::INFO("message box during load; accepting it");
						Input::FocusGameWindow();
						Input::TapKey(0x0D);  // VK_RETURN
					}
				}
				if (!inWorld) {
					finish("`load " + resolved + "` never put the player in a cell");
					return;
				}
			} else {
			auto started = std::make_shared<std::atomic_bool>(false);
			RunOnGameThread([started]() {
				auto* ui = RE::UI::GetSingleton();
				const auto menu = ui ? ui->GetMenu<RE::MainMenu>() : nullptr;
				if (!menu) {
					REX::ERROR("MainMenu not on the stack");
					return;
				}
				auto* raw = reinterpret_cast<std::uint8_t*>(menu.get());
				if (!LooksLikeMainMenu(raw)) {
					return;
				}

				using canContinue_t = bool (*)(void*);
				static REL::Relocation<canContinue_t> canContinue{ REL::ID(2249692) };
				auto* saveLoad = RE::BGSSaveLoadManager::GetSingleton();
				if (!saveLoad || !canContinue(saveLoad)) {
					REX::ERROR("the engine reports no save available to continue");
					return;
				}

				raw[0x294] = 1;                                        // choseContinue
				raw[kQueueContinueGameOffset] = 0;                     // queueContinueGame
				*reinterpret_cast<std::int32_t*>(raw + 0x250) = 2;      // kContinue

				using void_t = void (*)();
				static REL::Relocation<void_t> prepare{ REL::ID(2228370) };
				static REL::Relocation<void_t> doBeforeNewOrLoad{ REL::ID(2228951) };
				static REL::Relocation<std::uint8_t*> loadPending{ REL::ID(2698031) };

				REX::INFO("running the Continue branch (exitCondition=kContinue)");
				prepare();
				doBeforeNewOrLoad();
				*loadPending = 1;
				started->store(true);
			});

			if (!started->load()) {
				finish("could not run the Continue branch; see the log");
				return;
			}

			LogMainMenuState("after continue");

			// Wait to actually be in the world.  The loading screen is a transient
			// -- the previous attempt watched for it, never saw one, and declared
			// failure while the game was busy loading.
			if (!WaitFor("the player to be in a cell", IsInWorld,
					std::chrono::milliseconds{ settings.loadTimeout })) {
				LogMainMenuState("no load");
				finish("the Continue branch ran but the player never reached a cell");
				return;
			}
			}

			// In the world; let the scene settle before reading state.
			std::this_thread::sleep_for(std::chrono::milliseconds{ settings.loadSettleDelay });

			const auto loadedState = ReadWorldState();
			report.cellFormIDBeforeCoc = loadedState.cellFormID;
			REX::INFO("game loaded; player is in cell {:08X} at ({:.1f}, {:.1f}, {:.1f})",
				loadedState.cellFormID, loadedState.x, loadedState.y, loadedState.z);
			if (loadedState.cellFormID == 0) {
				finish("player has no parent cell after loading; still at the menu");
				return;
			}
		}

		// `coc` is only for reaching a cell the save is not already in.  When the
		// save IS the test scenario -- camera already framing the subject -- a coc
		// would teleport the player away from the shot, so an empty Cell skips it.
		if (!settings.cell.empty()) {
			// Issued immediately after the load's fade-in, a coc can be
			// swallowed without an error (measured: 10ms after load, no
			// loading screen ever appeared for a valid cell).  Let the world
			// settle first.
			std::this_thread::sleep_for(
				std::chrono::milliseconds{ settings.loadSettleDelay });

			// The open-console dance exists for the MAIN MENU, where commands
			// are otherwise swallowed.  In-game, ExecuteCommand works with the
			// console closed (tm/tfc/tpc all run that way), and an open
			// console menu pauses the world -- under which a queued cell
			// change never fires (measured: valid coc, no loading screen,
			// twice).  So only do the dance when not yet in a cell.
			const bool inGame = IsInWorld();
			if (settings.openConsole && !inGame) {
				REX::INFO("opening the console");
				SetConsoleVisible(true);
				WaitFor("the console to open", IsConsoleOpen, 15s);
			}

			RunCommand("coc " + settings.cell);

			if (settings.openConsole && !inGame) {
				SetConsoleVisible(false);
				WaitFor("the console to close", []() { return !IsConsoleOpen(); }, 10s);
			}

			const bool cocLoaded =
				WaitOutLoad("coc", std::chrono::milliseconds{ settings.loadTimeout });

			if (!cocLoaded) {
				finish("`coc " + settings.cell + "` did not trigger a load -- bad cell "
					   "editor ID, or the console rejected the command");
				return;
			}
		} else {
			REX::INFO("no Cell configured; using the save's own location");
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.loadSettleDelay });

		const auto world = ReadWorldState();
		report.cellFormID = world.cellFormID;
		report.x = world.x;
		report.y = world.y;
		report.z = world.z;
		REX::INFO("in cell {:08X} at ({:.1f}, {:.1f}, {:.1f})", world.cellFormID, world.x,
			world.y, world.z);

		// A null parent cell means we are looking at a menu, not a world.
		// Capturing here is how the first version of this harness produced a
		// confident, entirely meaningless result.
		if (world.cellFormID == 0) {
			finish("player has no parent cell after coc; refusing to capture a menu");
			return;
		}
		if (!settings.cell.empty() && settings.loadSaveFirst &&
			world.cellFormID == report.cellFormIDBeforeCoc) {
			finish("coc did not change cell -- still in " +
				   std::format("{:08X}", world.cellFormID));
			return;
		}

		for (const auto& command : settings.setupCommands) {
			RunCommand(command);
		}

		// An explicit pose makes two runs on different machines comparable;
		// it is issued as console commands so we do not have to fight the
		// havok/actor state a direct SetPosition would land in.
		if (settings.usePose) {
			RunCommand(std::format("player.setpos x {}", settings.posX));
			RunCommand(std::format("player.setpos y {}", settings.posY));
			RunCommand(std::format("player.setpos z {}", settings.posZ));
			RunCommand(std::format("player.setangle x {}", settings.angX));
			RunCommand(std::format("player.setangle z {}", settings.angZ));
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.settleDelay });

		// The world is loaded and settled; only now is it safe to let the
		// redirect divert (see PrevisLightingFix.cpp on why arming any
		// earlier breaks the load itself).
		if (PrevisLightingFix::Installed()) {
			PrevisLightingFix::SetWorldReady(true);
			REX::INFO("redirect diversion armed post-load");
			std::this_thread::sleep_for(std::chrono::milliseconds{ 2000 });
		}

		report.previsFixSites = settings.previsFixSites;
		report.lightingFixInstalled = PrevisLightingFix::Installed();
		report.lightingFixHits = PrevisLightingFix::HitCount();
		REX::INFO("previs lighting fix: installed={} hits={}", report.lightingFixInstalled,
			report.lightingFixHits);

		VisibilityProbe::Reset();
		VisibilityProbe::RequestSnapshot();
		SubmitProbe::Reset();
		report.fpsPrevisOn = MeasureFps(std::chrono::milliseconds{ settings.fpsWindow });
		RunOnGameThread([dir]() {
			Dump::SubmittedBatches(dir / "submitted.txt", "previs ON");
			Dump::ObjectFlags(dir / "objectflags.txt", "previs ON");
		});
		report.onVisiblePrevisOn = VisibilityProbe::Count();
		REX::INFO("BSGeometry::OnVisible calls with previs ON: {}",
			report.onVisiblePrevisOn);
		RunOnGameThread([dir]() {
			Dump::CullingProcesses(dir / "processes.txt", "previs ON");
		});
		REX::INFO("fps with previs ON (fix sites '{}'): {:.1f}", settings.previsFixSites,
			report.fpsPrevisOn);

		// The engine rewrites the batch each frame, so hold the flag set for a
		// couple of seconds rather than poking it once.
		if (settings.forcePrevisBatchFlag) {
			REX::INFO("forcing the previs batch flag for 2s before capture");
			const auto until = std::chrono::steady_clock::now() + 2s;
			while (std::chrono::steady_clock::now() < until) {
				RunOnGameThread([]() { PrevisFix::ForcePrevisBatchFlag(); });
				std::this_thread::sleep_for(8ms);
			}
		}

		RunOnGameThread([dir]() {
			Dump::Accumulators(dir / "accumulators.txt", "previs ON");
		});

		report.before = CaptureTo(dir, "01_previs_on.bmp", report);
		if (report.before.empty()) {
			finish("");
			return;
		}

		RunCommand(settings.toggleCommand);
		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.settleDelay });

		VisibilityProbe::Reset();
		VisibilityProbe::RequestSnapshot();
		SubmitProbe::Reset();
		report.fpsPrevisOff = MeasureFps(std::chrono::milliseconds{ settings.fpsWindow });
		RunOnGameThread([dir]() {
			Dump::SubmittedBatches(dir / "submitted.txt", "previs OFF");
			Dump::ObjectFlags(dir / "objectflags.txt", "previs OFF");
		});
		report.onVisiblePrevisOff = VisibilityProbe::Count();
		RunOnGameThread([dir]() {
			Dump::CullingProcesses(dir / "processes.txt", "previs OFF");
		});
		REX::INFO("BSGeometry::OnVisible calls with previs OFF: {}  (previs culls {:.1f}%)",
			report.onVisiblePrevisOff,
			report.onVisiblePrevisOff > 0
				? (1.0 - static_cast<double>(report.onVisiblePrevisOn) /
						  static_cast<double>(report.onVisiblePrevisOff)) * 100.0
				: 0.0);
		REX::INFO("fps with previs OFF: {:.1f}  (previs benefit {:+.1f}%)",
			report.fpsPrevisOff,
			report.fpsPrevisOff > 0.0
				? (report.fpsPrevisOn - report.fpsPrevisOff) * 100.0 / report.fpsPrevisOff
				: 0.0);

		RunOnGameThread([dir]() {
			Dump::Accumulators(dir / "accumulators.txt", "previs OFF");
		});

		report.after = CaptureTo(dir, "02_previs_off.bmp", report);
		if (report.after.empty()) {
			finish("");
			return;
		}

		report.ok = true;
		REX::INFO("harness finished; captures are in {}", dir.string());
		finish("");
	}
}

namespace Harness
{
	void Start()
	{
		static std::atomic_bool started{ false };
		if (started.exchange(true)) {
			return;
		}

		if (!Settings::GetSingleton().enabled) {
			REX::INFO("harness disabled by fo4test.ini");
			return;
		}

		std::thread{ []() {
			try {
				Run();
			} catch (const std::exception& e) {
				REX::ERROR("harness threw: {}", e.what());
			} catch (...) {
				REX::ERROR("harness threw an unknown exception");
			}
		} }.detach();
	}
}
