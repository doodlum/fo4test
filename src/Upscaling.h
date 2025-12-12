#pragma once

#include "Util.h"
#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

#include <array>
#include <memory>
#include <winrt/base.h>

const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61 };

/**
 * @class Upscaling
 * @brief Main upscaling manager that handles FSR3 and DLSS upscaling for Fallout 4
 *
 * This class manages all aspects of upscaling including:
 * - Dynamic render target scaling
 * - Sampler state mipmap bias adjustment
 * - Depth buffer management
 * - Integration with FSR3 and DLSS backends
 */
class Upscaling : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	// ========================================
	// Singleton & Initialization
	// ========================================

	/**
	 * @brief Get the singleton instance
	 * @return Pointer to the global Upscaling instance
	 */
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	/**
	 * @brief Initialize the upscaling system after game data is loaded
	 *
	 * Registers event sinks and loads initial settings from INI file
	 */
	void OnDataLoaded();

	/**
	 * @brief Install all game engine hooks required for upscaling
	 *
	 * Patches render pipeline, TAA shaders, dynamic resolution, and other
	 * game systems to integrate upscaling functionality
	 */
	static void InstallHooks();

	// ========================================
	// Settings & Configuration
	// ========================================

	/**
	 * @enum UpscaleMethod
	 * @brief Available upscaling methods
	 */
	enum class UpscaleMethod
	{
		kDisabled,  ///< No upscaling, native TAA
		kFSR,       ///< AMD FidelityFX Super Resolution 3
		kDLSS       ///< NVIDIA Deep Learning Super Sampling
	};

	/**
	 * @struct Settings
	 * @brief User-configurable upscaling settings
	 */
	struct Settings
	{
		uint upscaleMethodPreference = (uint)UpscaleMethod::kDLSS;  ///< Preferred upscaling method
		float sharpness = 0.0f;                                      ///< Sharpening amount (0.0-1.0)
		uint qualityMode = 1;  ///< Quality mode: 0=Native AA, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance
	};

	Settings settings;

	/**
	 * @brief Load settings from MCM configuration file
	 *
	 * Reads Data/MCM/Settings/Upscaling.ini and updates the settings struct
	 */
	void LoadSettings();

	/**
	 * @brief Determine which upscaling method should be used
	 * @param a_checkMenu If true, disable upscaling when certain menus are open
	 * @return The active upscaling method
	 *
	 * Falls back to FSR if DLSS is not available but preferred
	 */
	UpscaleMethod GetUpscaleMethod(bool a_checkMenu);

	/**
	 * @brief Process menu open/close events
	 * @param a_event The menu event
	 * @param a_source Event source (unused)
	 * @return Event control flag
	 *
	 * Reloads settings when pause menu is closed
	 */
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

	// ========================================
	// Main Upscaling Operations
	// ========================================

	/**
	 * @brief Update camera jitter for temporal anti-aliasing
	 *
	 * Calculates per-frame jitter offsets, updates sampler states, render targets,
	 * and manages resource creation/destruction based on active upscaling method
	 */
	void UpdateJitter();

	/**
	 * @brief Perform upscaling operation
	 *
	 * Executes the active upscaling method (FSR3 or DLSS) to upscale the
	 * rendered image from render resolution to display resolution
	 */
	void Upscale();

	/**
	 * @brief Check and manage upscaling resources
	 *
	 * Creates or destroys upscaling resources when switching between
	 * different upscaling methods
	 */
	void CheckResources();

	float2 jitter = { 0, 0 };  ///< Current frame's camera jitter offset

	// ========================================
	// Render Target Management
	// ========================================

	/**
	 * @brief Update all render targets for new resolution scaling
	 * @param a_currentWidthRatio Width scale factor (e.g., 0.67 for balanced mode)
	 * @param a_currentHeightRatio Height scale factor
	 *
	 * Recreates proxy render targets when resolution changes
	 */
	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);

	/**
	 * @brief Override game render targets with scaled proxy targets
	 *
	 * Temporarily replaces game render targets with lower resolution proxies
	 * during main rendering pass
	 */
	void OverrideRenderTargets();

	/**
	 * @brief Restore original render targets
	 *
	 * Restores full resolution render targets after scaled rendering is complete
	 */
	void ResetRenderTargets();

	/**
	 * @brief Update a single render target
	 * @param index Render target index
	 * @param a_currentWidthRatio Width scale factor
	 * @param a_currentHeightRatio Height scale factor
	 */
	void UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio);

	/**
	 * @brief Override a single render target
	 * @param index Render target index
	 */
	void OverrideRenderTarget(int index);

	/**
	 * @brief Reset a single render target
	 * @param index Render target index
	 */
	void ResetRenderTarget(int index);

	RE::BSGraphics::RenderTarget originalRenderTargets[101];      ///< Original full-resolution render targets
	RE::BSGraphics::RenderTarget proxyRenderTargets[101];         ///< Scaled proxy render targets
	RE::BSGraphics::RenderTargetProperties originalRenderTargetData[100];  ///< Original RT properties

	// ========================================
	// Sampler State Management
	// ========================================

	/**
	 * @brief Update sampler states with mipmap LOD bias
	 * @param a_currentMipBias Mipmap bias to apply (negative = sharper textures)
	 *
	 * Creates modified sampler states with adjusted mip bias to compensate
	 * for lower render resolution
	 */
	void UpdateSamplerStates(float a_currentMipBias);

	/**
	 * @brief Override game sampler states with biased versions
	 *
	 * Applies sampler states with negative LOD bias during rendering
	 */
	void OverrideSamplerStates();

	/**
	 * @brief Restore original sampler states
	 */
	void ResetSamplerStates();

	std::array<winrt::com_ptr<ID3D11SamplerState>, 320> originalSamplerStates;  ///< Original game sampler states
	std::array<winrt::com_ptr<ID3D11SamplerState>, 320> biasedSamplerStates;    ///< Modified sampler states with LOD bias

	// ========================================
	// Depth Management
	// ========================================

	/**
	 * @brief Override depth buffer with upscaled version
	 *
	 * Replaces depth buffer SRV with full-resolution depth for correct
	 * depth testing in post-processing effects
	 */
	void OverrideDepth();

	/**
	 * @brief Restore original depth buffer
	 */
	void ResetDepth();

	/**
	 * @brief Copy and upscale depth buffer
	 *
	 * Copies depth from render resolution to display resolution using
	 * a compute shader for effects that need full-resolution depth
	 */
	void CopyDepth();

	winrt::com_ptr<ID3D11ShaderResourceView> originalDepthView;  ///< Original depth buffer SRV
	std::unique_ptr<Texture2D> depthOverrideTexture;             ///< Full-resolution depth override texture

	// ========================================
	// Shader Management
	// ========================================

	/**
	 * @brief Patch screen-space reflections shader
	 *
	 * Injects custom SSR shader that properly handles scaled render targets
	 */
	void PatchSSRShader();

	/**
	 * @brief Get or compile RCAS (Robust Contrast Adaptive Sharpening) shader
	 * @return Compiled compute shader
	 *
	 * RCAS is used for post-upscaling sharpening
	 */
	ID3D11ComputeShader* GetRCAS();

	/**
	 * @brief Get or compile motion vector dilation shader
	 * @return Compiled compute shader
	 *
	 * Dilates motion vectors for better temporal stability in DLSS
	 */
	ID3D11ComputeShader* GetDilateMotionVectorCS();

	/**
	 * @brief Get or compile depth override shader
	 * @return Compiled compute shader
	 *
	 * Copies depth buffer from render to display resolution
	 */
	ID3D11ComputeShader* GetOverrideDepthCS();

	/**
	 * @brief Get or compile custom SSR raytracing pixel shader
	 * @return Compiled pixel shader
	 */
	ID3D11PixelShader* GetBSImagespaceShaderSSLRRaytracing();

	/**
	 * @brief Get constant buffer for upscaling parameters
	 * @return Pointer to constant buffer
	 *
	 * Contains screen size, render size, and camera data
	 */
	ConstantBuffer* GetUpscalingCB();

	/**
	 * @brief Update and bind upscaling constant buffer
	 * @param a_context D3D11 device context
	 * @param a_screenSize Display resolution
	 * @param a_renderSize Render resolution
	 *
	 * Helper function to fill and bind the upscaling CB to slot 0
	 * Automatically reads camera parameters from the game engine
	 */
	void UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize);

	// ========================================
	// Resource Management
	// ========================================

	/**
	 * @brief Create upscaling-specific resources
	 *
	 * Creates textures needed for DLSS (dilated motion vectors)
	 */
	void CreateUpscalingResources();

	/**
	 * @brief Destroy upscaling-specific resources
	 */
	void DestroyUpscalingResources();

	std::unique_ptr<Texture2D> upscalingTexture;           ///< Intermediate upscaling texture
	std::unique_ptr<Texture2D> dilatedMotionVectorTexture; ///< Dilated motion vectors for DLSS

	/**
	 * @struct UpscalingCB
	 * @brief Constant buffer structure for upscaling shaders
	 */
	struct UpscalingCB
	{
		uint ScreenSize[2];    ///< Display resolution (width, height)
		uint RenderSize[2];    ///< Render resolution (width, height)
		float4 CameraData;     ///< Camera parameters (far, near, far-near, far*near)
	};

private:
	// ========================================
	// Shader Resources (Private)
	// ========================================

	winrt::com_ptr<ID3D11ComputeShader> rcas;                        ///< RCAS sharpening shader
	winrt::com_ptr<ID3D11ComputeShader> dilateMotionVectorCS;        ///< Motion vector dilation shader
	winrt::com_ptr<ID3D11ComputeShader> overrideDepthCS;             ///< Depth upscaling shader
	winrt::com_ptr<ID3D11PixelShader> BSImagespaceShaderSSLRRaytracing;  ///< Custom SSR shader
};
