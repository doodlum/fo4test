#pragma once

#include "Buffer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

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
		float realFrameRateLimit = 0.0f;
		int frameGenerationBackend = 1;
		int reflexMode = 1;
		bool reflexSleepMode = true;
		int upscaleMethodPreference = 2;
		int qualityMode = 1;
		int dlssPreset = 0;
		bool debugLogging = false;
		int streamlineLogLevel = 0;
		int debugFrameLogCount = 240;
	};

	enum class PluginMode
	{
		kFrameGen,
		kUpscaler,
		kReflex
	};

	enum class UpscaleMethod
	{
		kDisabled = 0,
		kFSR = 1,
		kDLSS = 2
	};

	enum class FrameLimiterPolicy
	{
		kDisabled,
		kExternal,
		kUnlimited,
		kLimited
	};

	static constexpr int kFrameGenerationBackendDLSS = 1;
	static constexpr int kFrameGenerationBackendFSR = 2;

	Settings settings;

	PluginMode pluginMode = PluginMode::kFrameGen;
	bool renderBackendEnabled = false;
	bool highFPSPhysicsFixLoaded = false;
	bool highFPSPhysicsFixIniReadable = false;
	bool highFPSPhysicsFixUntiesSpeed = false;
	bool highFPSPhysicsFixOwnLimiterActive = false;
	float highFPSPhysicsFixInGameLimit = 0.0f;
	float highFPSPhysicsFixExteriorLimit = 0.0f;
	float highFPSPhysicsFixInteriorLimit = 0.0f;
	FrameLimiterPolicy frameLimiterPolicy = FrameLimiterPolicy::kDisabled;
	float resolvedRealFrameRateLimit = 0.0f;
	float frameLimiterConfiguredRealFrameRateLimit = 0.0f;
	bool frameLimiterPolicyInitialized = false;
	bool frameLimiterVSyncHeld = false;
	bool debugTraceCurrentPresent = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;

	Texture2D* HUDLessBufferShared[2];
	Texture2D* uiColorAndAlphaBufferShared[2]{};
	Texture2D* reticleColorAndAlphaBufferShared[2]{};
	Texture2D* depthBufferShared[2];
	Texture2D* motionVectorBufferShared[2];
	Texture2D* upscalerInputShared[2]{};
	Texture2D* upscalerOutputShared[2]{};
	
	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> uiColorAndAlphaBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> depthBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> upscalerInputShared12[2];
	winrt::com_ptr<ID3D12Resource> upscalerOutputShared12[2];

	ID3D11ComputeShader* copyDepthToSharedBufferCS;
	ID3D11ComputeShader* generateSharedBuffersCS;
	ID3D11ComputeShader* buildUIColorAndAlphaCS;
	ID3D11ComputeShader* buildReticleUIColorAndAlphaCS;
	ID3D11ComputeShader* patchHUDLessReticleCS;
	ID3D11ComputeShader* denoiseUIAlphaCS;

	bool setupBuffers = false;
	bool postLoadingSkipUpscale = false;
	bool nativePresentationModeActive = false;
	std::string_view nativePresentationMenu{};
	std::array<bool, 2> hudLessFrameValid{};
	std::array<std::uint64_t, 2> hudLessFrameIDs{};

	void LoadSettings();
	void LoadFrameGenerationSettings();
	void LoadReflexSettings();
	void ApplyRuntimeFallbacks();
	[[nodiscard]] const char* GetDLSSUnavailableReason() const;
	[[nodiscard]] static bool IsPreNGRuntime() noexcept;
	[[nodiscard]] static bool IsStreamlineRuntimeAvailable();

	void PostPostLoad();
	void OnD3D11DeviceCreated(ID3D11Device* a_device, IDXGIAdapter* a_adapter);

	void CreateFrameGenerationResources();
	void PreAlpha();
	bool CaptureHUDLessFrame();
	void PostAlpha();
	void CopyBuffersToSharedResources();
	bool BuildUIColorAndAlphaResource(ID3D11Texture2D* a_finalFrame);
	void DenoiseUIAlphaResource();

	static void TimerSleepQPC(int64_t targetQPC);

	void ResolveFrameLimiterPolicy(bool a_startup);
	void FrameLimiter(std::uint32_t a_syncInterval);

	static double GetRefreshRate(HWND a_window);

	void PostDisplay();

	void Reset();

	UpscaleMethod GetPreferredUpscaleMethod() const;
	bool UsesDLSSUpscaling() const;
	bool UsesFSRUpscaling() const;
	bool UsesDLSSFrameGeneration() const;
	bool UsesFSRFrameGeneration() const;
	bool UsesReflex() const;

	UpscaleMethod GetUpscaleMethod(bool a_checkMenu) const;
	void UpdateUpscaling();
	bool Upscale();
	void CheckResources();

	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);
	void OverrideRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void ResetRenderTargets(const std::vector<int>& a_indicesToCopy = {});
	void UpdateRenderTarget(int a_index, float a_currentWidthRatio, float a_currentHeightRatio);
	void OverrideRenderTarget(int a_index, bool a_doCopy = true);
	void ResetRenderTarget(int a_index, bool a_doCopy = true);

	void UpdateDepth(float a_currentWidthRatio, float a_currentHeightRatio);
	void OverrideDepth(bool a_doCopy = true);
	void ResetDepth();
	void CopyDepth();

	void UpdateSamplerStates(float a_currentMipBias);
	void OverrideSamplerStates();
	void ResetSamplerStates();
	void UpdateGameSettings();
	bool CreateUpscalingResources();
	void DestroyUpscalingResources();
	void PatchSSRShader();

	ID3D11ComputeShader* GetDilateMotionVectorCS();
	ID3D11ComputeShader* GetOverrideLinearDepthCS();
	ID3D11ComputeShader* GetOverrideDepthCS();
	ID3D11VertexShader* GetCopyDepthVS();
	ID3D11PixelShader* GetCopyDepthPS();
	ID3D11DepthStencilState* GetCopyDepthStencilState();
	ID3D11BlendState* GetCopyBlendState();
	ID3D11RasterizerState* GetCopyRasterizerState();
	ID3D11SamplerState* GetCopySamplerState();
	ID3D11PixelShader* GetBSImagespaceShaderSSLRRaytracing();

	struct UpscalingCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		float4 CameraData;
	};

	ConstantBuffer* GetUpscalingCB();
	void UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize);

	float2 jitter = { 0.0f, 0.0f };
	UpscaleMethod upscaleMethodNoMenu = UpscaleMethod::kDisabled;
	UpscaleMethod upscaleMethod = UpscaleMethod::kDisabled;

	RE::BSGraphics::RenderTarget originalRenderTargets[101]{};
	RE::BSGraphics::RenderTarget proxyRenderTargets[101]{};
	RE::BSGraphics::DepthStencilTarget originalDepthStencilTarget{};
	RE::BSGraphics::DepthStencilTarget depthOverrideTarget{};

	std::array<ID3D11SamplerState*, 320> originalSamplerStates{};
	std::array<ID3D11SamplerState*, 320> biasedSamplerStates{};

	std::unique_ptr<Texture2D> upscalingTexture;
	std::unique_ptr<Texture2D> dilatedMotionVectorTexture;

	winrt::com_ptr<ID3D11ComputeShader> dilateMotionVectorCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideLinearDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> overrideDepthCS;
	winrt::com_ptr<ID3D11VertexShader> copyDepthVS;
	winrt::com_ptr<ID3D11PixelShader> copyDepthPS;
	winrt::com_ptr<ID3D11PixelShader> BSImagespaceShaderSSLRRaytracing;
	winrt::com_ptr<ID3D11DepthStencilState> copyDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> copyBlendState;
	winrt::com_ptr<ID3D11RasterizerState> copyRasterizerState;
	winrt::com_ptr<ID3D11SamplerState> copySamplerState;

	static void InstallHooks();
};
