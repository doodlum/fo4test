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

	bool highFPSPhysicsFixLoaded = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;
	
	Texture2D* HUDLessBufferShared[2];
	Texture2D* depthBufferShared[2];
	Texture2D* motionVectorBufferShared[2];
	
	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> depthBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12[2];

	ID3D11ComputeShader* copyDepthToSharedBufferCS;
	ID3D11ComputeShader* generateSharedBuffersCS;

	bool setupBuffers = false;

	void LoadSettings();

	void PostPostLoad();

	void CreateFrameGenerationResources();
	void PreAlpha();
	void PostAlpha();
	void CopyBuffersToSharedResources();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_useFrameGeneration);

	void GameFrameLimiter();

	static double GetRefreshRate(HWND a_window);

	void PostDisplay();

	void Reset();

	static void InstallHooks();
};
