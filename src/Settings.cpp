#include "PCH.h"

#include "Settings.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <charconv>
#include <map>
#include <memory>
#include <sstream>

namespace
{
	// Documents/My Games/<save folder>/F4SE -- the same root F4SE::Init puts
	// the log in, and the one place we can always write to (the game itself
	// usually lives under Program Files).
	[[nodiscard]] std::filesystem::path F4SEUserDirectory()
	{
		wchar_t*   buffer{ nullptr };
		const auto result = REX::W32::SHGetKnownFolderPath(
			REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));

		const std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(
			buffer, REX::W32::CoTaskMemFree);

		if (!owned || result != 0) {
			return {};
		}

		const auto saveFolder = F4SE::GetSaveFolderName();
		if (saveFolder.empty()) {
			return {};
		}

		std::filesystem::path path = owned.get();
		path /= "My Games";
		path /= saveFolder;
		path /= "F4SE";
		return path;
	}

	[[nodiscard]] std::string Trim(std::string_view a_value)
	{
		constexpr auto whitespace = " \t\r\n"sv;
		const auto     first = a_value.find_first_not_of(whitespace);
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_value.find_last_not_of(whitespace);
		return std::string{ a_value.substr(first, last - first + 1) };
	}

	// The INI is flat key=value; sections are accepted and ignored so the file
	// can carry a [Harness] header for readability.
	[[nodiscard]] std::map<std::string, std::string> ReadIni(const std::filesystem::path& a_path)
	{
		std::map<std::string, std::string> out;

		std::ifstream file{ a_path };
		if (!file) {
			return out;
		}

		std::string line;
		while (std::getline(file, line)) {
			const auto trimmed = Trim(line);
			if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' ||
				trimmed.front() == '[') {
				continue;
			}

			const auto eq = trimmed.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			auto key = Trim(std::string_view{ trimmed }.substr(0, eq));
			auto value = Trim(std::string_view{ trimmed }.substr(eq + 1));
			if (!key.empty()) {
				// Case-insensitive keys so the INI is forgiving.
				for (auto& c : key) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				out.insert_or_assign(std::move(key), std::move(value));
			}
		}

		return out;
	}

	[[nodiscard]] const std::string* Find(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key)
	{
		const auto it = a_ini.find(std::string{ a_key });
		return it != a_ini.end() ? std::addressof(it->second) : nullptr;
	}

	void ReadBool(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key, bool& a_out)
	{
		const auto* raw = Find(a_ini, a_key);
		if (!raw || raw->empty()) {
			return;
		}
		const auto& v = *raw;
		a_out = (v == "1" || v == "true" || v == "True" || v == "TRUE" ||
				 v == "yes" || v == "Yes" || v == "on" || v == "On");
	}

	void ReadUInt(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key,
		std::uint32_t& a_out)
	{
		const auto* raw = Find(a_ini, a_key);
		if (!raw || raw->empty()) {
			return;
		}
		std::uint32_t parsed{};
		const auto [ptr, ec] = std::from_chars(raw->data(), raw->data() + raw->size(), parsed);
		if (ec == std::errc{}) {
			a_out = parsed;
		} else {
			REX::WARN("ini: could not parse {}={} as an integer; keeping {}", a_key, *raw, a_out);
		}
	}

	void ReadFloat(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key, float& a_out)
	{
		const auto* raw = Find(a_ini, a_key);
		if (!raw || raw->empty()) {
			return;
		}
		// from_chars for floats is available on MSVC; fall back to reporting.
		float      parsed{};
		const auto [ptr, ec] = std::from_chars(raw->data(), raw->data() + raw->size(), parsed);
		if (ec == std::errc{}) {
			a_out = parsed;
		} else {
			REX::WARN("ini: could not parse {}={} as a float; keeping {}", a_key, *raw, a_out);
		}
	}

	void ReadString(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key,
		std::string& a_out)
	{
		const auto* raw = Find(a_ini, a_key);
		if (raw && !raw->empty()) {
			a_out = *raw;
		}
	}

	// Pipe-separated so a command list can contain spaces without quoting.
	void ReadCommands(
		const std::map<std::string, std::string>& a_ini, std::string_view a_key,
		std::vector<std::string>& a_out)
	{
		const auto* raw = Find(a_ini, a_key);
		if (!raw) {
			return;
		}

		std::vector<std::string> parsed;
		std::stringstream        stream{ *raw };
		std::string              item;
		while (std::getline(stream, item, '|')) {
			auto trimmed = Trim(item);
			if (!trimmed.empty()) {
				parsed.push_back(std::move(trimmed));
			}
		}

		// An explicitly empty value means "run no setup commands", which is a
		// legitimate configuration, so assign unconditionally.
		a_out = std::move(parsed);
	}
}

Settings& Settings::GetSingleton() noexcept
{
	static Settings singleton;
	return singleton;
}

void Settings::Load()
{
	const auto path = std::filesystem::path{ "Data/F4SE/Plugins/fo4test.ini" };

	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		REX::INFO("no {} found; using built-in defaults", path.string());
		return;
	}

	const auto ini = ReadIni(path);
	if (ini.empty()) {
		REX::WARN("{} was found but had no readable keys; using defaults", path.string());
		return;
	}

	ReadBool(ini, "enabled", enabled);
	ReadBool(ini, "loadsavefirst", loadSaveFirst);
	ReadString(ini, "cell", cell);
	ReadCommands(ini, "setupcommands", setupCommands);
	ReadString(ini, "togglecommand", toggleCommand);

	ReadBool(ini, "usepose", usePose);
	ReadFloat(ini, "posx", posX);
	ReadFloat(ini, "posy", posY);
	ReadFloat(ini, "posz", posZ);
	ReadFloat(ini, "angx", angX);
	ReadFloat(ini, "angz", angZ);

	ReadUInt(ini, "mainmenudelay", mainMenuDelay);
	ReadUInt(ini, "loadtimeout", loadTimeout);
	ReadUInt(ini, "loadsettledelay", loadSettleDelay);
	ReadUInt(ini, "settledelay", settleDelay);

	ReadString(ini, "outputdir", outputDir);
	ReadBool(ini, "quitwhendone", quitWhenDone);

	REX::INFO("loaded {}", path.string());
}

std::filesystem::path Settings::ResolveOutputDir() const
{
	std::filesystem::path dir;
	if (!outputDir.empty()) {
		dir = outputDir;
	} else {
		const auto root = F4SEUserDirectory();
		if (root.empty()) {
			REX::ERROR("could not resolve the F4SE user directory");
			return {};
		}
		dir = root / "fo4test";
	}

	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec) {
		REX::ERROR("could not create output directory {}: {}", dir.string(), ec.message());
		return {};
	}

	return dir;
}
