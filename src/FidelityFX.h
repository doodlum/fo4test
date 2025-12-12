#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "Buffer.h"

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	FfxFsr3Context fsrContext;

	Texture2D* colorOpaqueOnlyTexture;
	Texture2D* reactiveMaskTexture;

	void CreateFSRResources();
	void DestroyFSRResources();

	void CopyOpaqueTexture();
	void GenerateReactiveMask();

	void Upscale(Texture2D* a_color, float2 a_jitter, float2 a_renderSize, float a_sharpness);
private:
	// FSR scratch buffer - needs to be freed in DestroyFSRResources
	void* fsrScratchBuffer = nullptr;
};
