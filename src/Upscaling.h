#pragma once

#include "Buffer.h"

#include "SimpleIni.h"

class Upscaling
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	struct Settings
	{
		bool frameGenerationMode = 1;
		bool frameLimitMode = 1;
		bool frameGenerationForceEnable = 0;
	};

	Settings settings;

	bool isWindowed = false;
	bool lowRefreshRate = false;

	bool fidelityFXMissing = false;
	bool highFPSPhysicsFixLoaded = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;
	
	Texture2D* HUDLessBufferShared;
	Texture2D* depthBufferShared;
	Texture2D* motionVectorBufferShared;
	
	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12;
	winrt::com_ptr<ID3D12Resource> depthBufferShared12;
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12;

	ID3D11ComputeShader* copyDepthToSharedBufferCS;

	bool setupBuffers = false;
	bool useFrameGenerationThisFrame = false;

	void LoadSettings();

	void PostPostLoad();

	void CreateFrameGenerationResources();
	void CopyBuffersToSharedResources();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_useFrameGeneration);

	void GameFrameLimiter();

	static double GetRefreshRate(HWND a_window);

	void PostDisplay();

	struct SetUseDynamicResolutionViewportAsDefaultViewport
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
		{
			if (!a_true)
				GetSingleton()->PostDisplay();
			func(This, a_true);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct WindowSizeChanged
	{
		static void thunk(RE::BSGraphics::Renderer*, unsigned int)
		{
			
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
#if defined(FALLOUT_POST_NG)
		stl::detour_thunk<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(2277194));
		stl::detour_thunk<WindowSizeChanged>(REL::ID(2276824));
#else
		stl::detour_thunk<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(676851));
		stl::detour_thunk<WindowSizeChanged>(REL::ID(212827));
#endif

		logger::info("[Upscaling] Installed hooks");
	}
};
