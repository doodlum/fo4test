#pragma once

#include <RE/FO4Runtime.h>

namespace fo4cs::RE
{
	[[nodiscard]] inline float GetCameraNear()
	{
		return ::RE::FO4Runtime::GetCameraNear();
	}

	[[nodiscard]] inline float GetCameraFar()
	{
		return ::RE::FO4Runtime::GetCameraFar();
	}
}
