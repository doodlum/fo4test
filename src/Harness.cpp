#include "PCH.h"

#include "Harness.h"

#include "Capture.h"
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
			if (!RunOnGameThread([hit, &a_predicate]() { hit->store(a_predicate()); })) {
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
		float         x{ 0.0f };
		float         y{ 0.0f };
		float         z{ 0.0f };
		std::string   toggleCommand{};
		std::string   before{};
		std::string   after{};
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};

	void WriteReport(const std::filesystem::path& a_dir, const Report& a_report)
	{
		const auto path = a_dir / "result.json";
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
			 << "  \"playerX\": " << a_report.x << ",\n"
			 << "  \"playerY\": " << a_report.y << ",\n"
			 << "  \"playerZ\": " << a_report.z << ",\n"
			 << "  \"toggleCommand\": " << Quote(a_report.toggleCommand) << ",\n"
			 << "  \"before\": " << Quote(a_report.before) << ",\n"
			 << "  \"after\": " << Quote(a_report.after) << ",\n"
			 << "  \"width\": " << a_report.width << ",\n"
			 << "  \"height\": " << a_report.height << "\n"
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

		const auto finish = [&](std::string_view a_failure) {
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

		// The main menu needs a moment past kGameDataReady before it will
		// accept console input.
		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.mainMenuDelay });

		RunCommand("coc " + settings.cell);

		// coc from the main menu goes through a full load.  Wait for the
		// loading screen to appear, then for it to go away again.  A missing
		// appearance is not fatal on a fast machine, so only the disappearance
		// is treated as required.
		WaitFor("the loading screen to appear", IsLoadingMenuOpen, 30s);
		if (!WaitFor(
				"the loading screen to finish", []() { return !IsLoadingMenuOpen(); },
				std::chrono::milliseconds{ settings.loadTimeout })) {
			finish("the coc load never finished");
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.loadSettleDelay });

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

		const auto world = ReadWorldState();
		report.cellFormID = world.cellFormID;
		report.x = world.x;
		report.y = world.y;
		report.z = world.z;
		REX::INFO("in cell {:08X} at ({:.1f}, {:.1f}, {:.1f})", world.cellFormID, world.x,
			world.y, world.z);

		report.before = CaptureTo(dir, "01_previs_on.bmp", report);
		if (report.before.empty()) {
			finish("");
			return;
		}

		RunCommand(settings.toggleCommand);
		std::this_thread::sleep_for(std::chrono::milliseconds{ settings.settleDelay });

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
