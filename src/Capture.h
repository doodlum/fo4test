#pragma once

namespace Capture
{
	struct Result
	{
		bool          ok{ false };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::string   error{};
	};

	// Copies the current swap chain back buffer to a 32-bit BGRA .bmp at
	// a_path.  MUST be called on the game thread (use F4SE's task interface):
	// it touches the immediate context, which is not free-threaded.
	[[nodiscard]] Result ToBMP(const std::filesystem::path& a_path);
}
