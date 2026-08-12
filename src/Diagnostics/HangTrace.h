#pragma once

#include "Core/DebugSwitches.h"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

#include <ShlObj_core.h>

namespace fo4cs::Diagnostics
{
	inline std::optional<std::filesystem::path> GetHangTraceDirectory()
	{
		PWSTR documentsPath = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsPath))) {
			return std::nullopt;
		}

		std::filesystem::path path{ documentsPath };
		CoTaskMemFree(documentsPath);

		path /= "My Games/Fallout4/F4SE";
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		if (ec) {
			return std::nullopt;
		}

		return path;
	}

	inline bool IsHangTraceEnabled()
	{
		return CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_HANG_TRACE");
	}

	inline std::optional<std::filesystem::path> GetHangTracePath()
	{
		auto path = GetHangTraceDirectory();
		if (!path) {
			return std::nullopt;
		}

		*path /= "CommunityShaders.hangtrace.log";
		return path;
	}

	inline void WriteHangTraceLine(std::string_view a_stage)
	{
		if (!IsHangTraceEnabled()) {
			return;
		}

		auto path = GetHangTracePath();
		if (!path) {
			return;
		}

		auto file = CreateFileW(
			path->c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}

		SYSTEMTIME now{};
		GetLocalTime(&now);

		char line[512]{};
		const auto count = std::snprintf(
			line,
			sizeof(line),
			"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu tick=%llu tid=%lu %.*s\n",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			now.wMilliseconds,
			static_cast<unsigned long long>(GetTickCount64()),
			static_cast<unsigned long>(GetCurrentThreadId()),
			static_cast<int>(a_stage.size()),
			a_stage.data());
		if (count > 0) {
			DWORD written = 0;
			WriteFile(file, line, static_cast<DWORD>(std::min<int>(count, static_cast<int>(sizeof(line) - 1))), &written, nullptr);
			FlushFileBuffers(file);
		}

		CloseHandle(file);
	}

	inline void ResetHangTrace()
	{
		if (!IsHangTraceEnabled()) {
			return;
		}

		auto path = GetHangTracePath();
		if (path) {
			DeleteFileW(path->c_str());
		}

		WriteHangTraceLine("plugin-load:hangtrace-reset");
	}
}
