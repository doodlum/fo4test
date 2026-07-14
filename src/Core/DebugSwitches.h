#pragma once

#include "SimpleIni.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace CommunityShaders::DebugSwitches
{
	enum class Source
	{
		kNone,
		kDebugIni
	};

	struct SwitchState
	{
		bool enabled = false;
		Source source = Source::kNone;
	};

	struct UIntState
	{
		std::uint32_t value = 0;
		Source source = Source::kNone;
		bool present = false;
		bool valid = false;
	};

	inline constexpr const char* kSwitchSection = "Switches";

	[[nodiscard]] inline const char* SourceName(Source a_source) noexcept
	{
		switch (a_source) {
		case Source::kDebugIni:
			return "debug-ini";
		default:
			return "none";
		}
	}

	[[nodiscard]] inline bool IsTruthyValue(std::string_view a_value) noexcept
	{
		return a_value == "1" ||
		       a_value == "true" ||
		       a_value == "TRUE" ||
		       a_value == "on" ||
		       a_value == "ON";
	}

	[[nodiscard]] inline std::filesystem::path GetContainingModulePath()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetContainingModulePath),
				&module) ||
			!module) {
			return {};
		}

		std::vector<wchar_t> buffer(MAX_PATH);
		while (true) {
			const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) {
				return {};
			}
			if (length < static_cast<DWORD>(buffer.size() - 1)) {
				return std::filesystem::path(std::wstring(buffer.data(), length));
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	[[nodiscard]] inline std::filesystem::path GetDebugIniPath()
	{
		const auto modulePath = GetContainingModulePath();
		if (!modulePath.empty()) {
			return modulePath.parent_path() / "CommunityShaders" / "Debug.ini";
		}

		return std::filesystem::path("Data\\F4SE\\Plugins\\CommunityShaders") / "Debug.ini";
	}

	[[nodiscard]] inline const CSimpleIniA& GetDebugIni()
	{
		static CSimpleIniA ini;
		static std::once_flag loadFlag;
		std::call_once(loadFlag, [] {
			ini.SetUnicode();
			const auto path = GetDebugIniPath();
			std::error_code ec;
			if (std::filesystem::exists(path, ec)) {
				const auto rc = ini.LoadFile(path.string().c_str());
				logger::info(
					"[DebugSwitches] Debug switch source: {} (load rc={})",
					path.string(),
					static_cast<int>(rc));
			} else {
				logger::info(
					"[DebugSwitches] Debug switch source {} not present; debug switches default OFF",
					path.string());
			}
		});
		return ini;
	}

	[[nodiscard]] inline const char* ReadRawValue(const char* a_name)
	{
		return GetDebugIni().GetValue(kSwitchSection, a_name, nullptr);
	}

	[[nodiscard]] inline SwitchState ReadSwitch(const char* a_name)
	{
		const char* value = ReadRawValue(a_name);
		if (!value) {
			return {};
		}

		return {
			IsTruthyValue(value),
			Source::kDebugIni
		};
	}

	[[nodiscard]] inline bool ReadSwitchEnabled(const char* a_name)
	{
		return ReadSwitch(a_name).enabled;
	}

	[[nodiscard]] inline bool ParseUIntValue(const char* a_value, std::uint32_t& a_result)
	{
		if (!a_value || *a_value == '\0') {
			return false;
		}

		const char* end = a_value;
		while (*end != '\0') {
			++end;
		}

		const auto parsed = std::from_chars(a_value, end, a_result, 10);
		return parsed.ec == std::errc{} && parsed.ptr == end;
	}

	[[nodiscard]] inline UIntState ReadUInt(const char* a_name)
	{
		const char* value = ReadRawValue(a_name);
		if (!value) {
			return {};
		}

		UIntState state{};
		state.source = Source::kDebugIni;
		state.present = true;
		state.valid = ParseUIntValue(value, state.value);
		return state;
	}

	[[nodiscard]] inline std::uint32_t ReadUIntClamped(
		const char* a_name,
		std::uint32_t a_defaultValue,
		std::uint32_t a_minValue,
		std::uint32_t a_maxValue)
	{
		const auto state = ReadUInt(a_name);
		if (!state.present || !state.valid) {
			return a_defaultValue;
		}
		return std::clamp(state.value, a_minValue, a_maxValue);
	}
}
