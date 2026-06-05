#pragma once

#include <RE/FO4Runtime.h>

namespace RE::BSGraphics {}
namespace fo4cs::RE
{
	[[nodiscard]] inline ::RE::BSGraphics::State* GetGraphicsState()
	{
		return ::RE::FO4Runtime::GetGraphicsState();
	}

	[[nodiscard]] inline ::RE::BSGraphics::RenderTargetManager* GetRenderTargetManager()
	{
		return ::RE::FO4Runtime::GetRenderTargetManager();
	}

	[[nodiscard]] inline ID3D11SamplerState** GetSamplerStateArray()
	{
		return ::RE::FO4Runtime::GetSamplerStateArray();
	}

	[[nodiscard]] inline bool* GetTAAEnableFlag()
	{
		return ::RE::FO4Runtime::GetTAAEnableFlag();
	}
}
