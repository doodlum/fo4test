#include "Upscaling.h"

#include <algorithm>
#include <SimpleIni.h>

extern bool enbLoaded;

/** @brief Hook for updating jitter, dynamic resolution, and resources */
struct BSGraphics_State_UpdateDynamicResolution
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This,
		RE::NiPoint3* a2,
		RE::NiPoint3* a3,
		RE::NiPoint3* a4,
		RE::NiPoint3* a5)
	{
		func(This, a2, a3, a4, a5);
		Upscaling::GetSingleton()->UpdateUpscaling();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to disable TAA when alternative scaling method is active */
struct ImageSpaceEffectTemporalAA_IsActive
{
	static bool thunk(struct ImageSpaceEffectTemporalAA* This)
	{
		return Upscaling::GetSingleton()->upscaleMethod == Upscaling::UpscaleMethod::kDisabled && func(This);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

float originalDynamicHeightRatio = 1.0f;
float originalDynamicWidthRatio = 1.0f;

/** @brief Hook to fix outline thickness in VATs shader*/
struct ImageSpaceEffectVatsTarget_UpdateParams_SetPixelConstant
{
	static void thunk(struct ImageSpaceShaderParam* This, int row, float x, float y, float z, float w)
	{
		func(This, row, x * originalDynamicHeightRatio, y * originalDynamicWidthRatio, z, w);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to fix dynamic resolution and jitter in post processing shaders */
struct DrawWorld_Imagespace_RenderEffectRange
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, uint a2, uint a3, uint a4, uint a5)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		static auto gameViewport = Util::State_GetSingleton();

		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		auto originalOffsetX = gameViewport->offsetX;
		auto originalOffsetY = gameViewport->offsetY;

		// Disable removal of jitter in some passes
		if (upscaling->upscaleMethod != Upscaling::UpscaleMethod::kDisabled){
			gameViewport->offsetX = originalOffsetX;
			gameViewport->offsetY = originalOffsetY;
		}

		originalDynamicHeightRatio = renderTargetManager->dynamicHeightRatio;
		originalDynamicWidthRatio = renderTargetManager->dynamicWidthRatio;

		if (requiresOverride) {

			// HDR shaders
			func(This, 0, 3, 1, 1);
			upscaling->OverrideDepth(true);
			upscaling->OverrideRenderTargets({1, 4, 29, 16});
			renderTargetManager->dynamicHeightRatio = 1.0f;
			renderTargetManager->dynamicWidthRatio = 1.0f;

			// LDR shaders
			func(This, 4, 13, 1, 1);
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets({4});

			renderTargetManager->dynamicHeightRatio = originalDynamicHeightRatio;
			renderTargetManager->dynamicWidthRatio = originalDynamicWidthRatio;
		} else {
			func(This, a2, a3, a4, a5);
		}

		gameViewport->offsetX = originalOffsetX;
		gameViewport->offsetY = originalOffsetY;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to add alternative scaling method */
struct DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
	{
		func(This, a_true);

		auto upscaling = Upscaling::GetSingleton();
		upscaling->Upscale();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for deferred pre-pass rendering with sampler state override */
struct DrawWorld_Render_PreUI_DeferredPrePass
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();
		upscaling->OverrideSamplerStates();
		func(This);
		upscaling->ResetSamplerStates();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for forward rendering pass with sampler state override and reactive mask generation */
struct DrawWorld_Render_PreUI_Forward
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();

		upscaling->OverrideSamplerStates();
		func(This);
		upscaling->ResetSamplerStates();

		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaling->upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->GenerateReactiveMask();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for HBAO to fix dynamic resolution */
struct DrawWorld_Render_PreUI_NVHBAO
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		originalDynamicHeightRatio = renderTargetManager->dynamicHeightRatio;
		originalDynamicWidthRatio = renderTargetManager->dynamicWidthRatio;

		if (requiresOverride) {
			upscaling->OverrideDepth(true);
			upscaling->OverrideRenderTargets({20});
			renderTargetManager->dynamicHeightRatio = 1.0f;
			renderTargetManager->dynamicWidthRatio = 1.0f;
		}

		func(This);

		if (requiresOverride) {
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets({25});
			renderTargetManager->dynamicHeightRatio = originalDynamicHeightRatio;
			renderTargetManager->dynamicWidthRatio = originalDynamicWidthRatio;
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for BSDFComposite with render target and depth override */
struct DrawWorld_DeferredComposite_RenderPassImmediately
{
	static void thunk(RE::BSRenderPass* This, uint a2, bool a3)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		originalDynamicHeightRatio = renderTargetManager->dynamicHeightRatio;
		originalDynamicWidthRatio = renderTargetManager->dynamicWidthRatio;

		if (requiresOverride) {
			upscaling->OverrideDepth(true);
			upscaling->OverrideRenderTargets({20, 25, 57, 24, 23, 58, 59, 3, 9, 60, 61, 28});
			renderTargetManager->dynamicHeightRatio = 1.0f;
			renderTargetManager->dynamicWidthRatio = 1.0f;
		}

		func(This, a2, a3);

		if (requiresOverride) {
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets({4});
			renderTargetManager->dynamicHeightRatio = originalDynamicHeightRatio;
			renderTargetManager->dynamicWidthRatio = originalDynamicWidthRatio;
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for BSImagespaceShaderLensFlare with depth override */
struct BSImagespaceShaderLensFlare_RenderLensFlare
{
	static void thunk(RE::NiCamera* a_camera)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		if (requiresOverride)
			upscaling->OverrideDepth(true);

		func(a_camera);

		if (requiresOverride)
			upscaling->ResetDepth();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for BSImagespaceShaderSSLRRaytracing with replaced shader */
struct BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique
{
	static void thunk(RE::BSShader* This, uint a2, uint a3, uint a4, uint a5)
	{
		func(This, a2, a3, a4, a5);
		Upscaling::GetSingleton()->PatchSSRShader();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for forward alpha rendering with opaque texture copy for reactive mask */
struct ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth
{
	static void thunk(RE::BSShaderAccumulator* This)
	{
		func(This);
		auto upscaling = Upscaling::GetSingleton();
		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaling->upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->CopyOpaqueTexture();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook LoadingMenu to fix jitter scale */
struct LoadingMenu_Render_UpdateTemporalData
{
	static void thunk(RE::BSGraphics::State* This)
	{
		func(This);

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		renderTargetManager->dynamicHeightRatio = 1.0f;
		renderTargetManager->dynamicWidthRatio = 1.0f;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	// Disable TAA shader if using alternative scaling method
	stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

#if defined(FALLOUT_POST_NG)
	// Control jitters, dynamic resolution, sampler states, and render targets
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(REL::ID(2318321).address() + 0x29F);

	// Add alternative scaling method
	stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(2318322).address() + 0xC5);

	// Control sampler states for mipmap bias
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(2318321).address() + 0x2E3);
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(2318321).address() + 0x3A6);

	// Copy opaque texture for FSR reactive mask
	stl::write_thunk_call<ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth>(REL::ID(2318315).address() + 0x4C6);

	// These hooks are not needed when using ENB because dynamic resolution is not supported
	if (!enbLoaded) {
		// Fix dynamic resolution for BSDFComposite
		stl::write_thunk_call<DrawWorld_DeferredComposite_RenderPassImmediately>(REL::ID(2318313).address() + 0x915);

		// Fix dynamic resolution for Lens Flare visibility
		stl::detour_thunk<BSImagespaceShaderLensFlare_RenderLensFlare>(REL::ID(2317547));

		// Fix dynamic resolution for Screenspace Reflections
		stl::write_thunk_call<BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique>(REL::ID(2317302).address() + 0x1C);

		// Fix dynamic resolution for post processing
		stl::write_thunk_call<DrawWorld_Imagespace_RenderEffectRange>(REL::ID(2318322).address() + 0x83);
		
		// Fix VATs line thickness
		stl::write_thunk_call<ImageSpaceEffectVatsTarget_UpdateParams_SetPixelConstant>(REL::ID(2317983).address() + 0x110);

		// Fix jitter in LoadingMenu
		stl::write_thunk_call<LoadingMenu_Render_UpdateTemporalData>(REL::ID(2249225).address() + 0x275);
	}
#else
	// Control jitters, dynamic resolution, sampler states, and render targets
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(REL::ID(984743).address() + 0x14B);

	// Add alternative scaling method
	stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);

	// Control sampler states for mipmap bias
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(984743).address() + 0x17F);
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(984743).address() + 0x1C9);
	
	// Copy opaque texture for FSR reactive mask
	stl::write_thunk_call<ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth>(REL::ID(338205).address() + 0x1DC);

	// These hooks are not needed when using ENB because dynamic resolution is not supported
	if (!enbLoaded) {
		// Fix dynamic resolution for BSDFComposite
		stl::write_thunk_call<DrawWorld_DeferredComposite_RenderPassImmediately>(REL::ID(728427).address() + 0x8DC);

		// Fix dynamic resolution for Lens Flare visibility
		stl::detour_thunk<BSImagespaceShaderLensFlare_RenderLensFlare>(REL::ID(676108));

		// Fix dynamic resolution for Screenspace Reflections
		stl::write_thunk_call<BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique>(REL::ID(779077).address() + 0x1C);
		
		// Fix dynamic resolution for post processing
		stl::write_thunk_call<DrawWorld_Imagespace_RenderEffectRange>(REL::ID(587723).address() + 0x9F);
		
		// Fix dynamic resolution for HBAO
		stl::write_thunk_call<DrawWorld_Render_PreUI_NVHBAO>(REL::ID(984743).address() + 0x1BA);
		
		// Fix VATs line thickness
		stl::write_thunk_call<ImageSpaceEffectVatsTarget_UpdateParams_SetPixelConstant>(REL::ID(1042583).address() + 0xBB);

		// Fix jitter in LoadingMenu
		stl::write_thunk_call<LoadingMenu_Render_UpdateTemporalData>(REL::ID(135719).address() + 0x2BD);
	}
#endif
}

struct SamplerStates
{
	ID3D11SamplerState* a[320];

	static SamplerStates* GetSingleton()
	{
#if defined(FALLOUT_POST_NG)
		static auto samplerStates = reinterpret_cast<SamplerStates*>(REL::ID(2704455).address());
#else
		static auto samplerStates = reinterpret_cast<SamplerStates*>(REL::ID(44312).address());
#endif
		return samplerStates;
	}
};

void Upscaling::LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	ini.LoadFile("Data\\MCM\\Settings\\Upscaling.ini");
	
	settings.upscaleMethodPreference = static_cast<uint>(ini.GetLongValue("Settings", "iUpscaleMethodPreference", 2));
	settings.qualityMode = static_cast<uint>(ini.GetLongValue("Settings", "iQualityMode", 1));
}

void Upscaling::OnDataLoaded()
{
	RE::UI::GetSingleton()->RegisterSink<RE::MenuOpenCloseEvent>(this);
	LoadSettings();
	UpdateGameSettings();
}

RE::BSEventNotifyControl Upscaling::ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// Reload settings when closing MCM menu
	if (a_event.menuName == "PauseMenu") {
		if (!a_event.opening) {
			GetSingleton()->LoadSettings();
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

void Upscaling::UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio)
{
	// Get the game's renderer and save the original render target
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	originalRenderTargets[index] = rendererData->renderTargets[index];

	auto& originalRenderTarget = originalRenderTargets[index];
	auto& proxyRenderTarget = proxyRenderTargets[index];

	// Clean up existing proxy render target resources
	// We manually Release() these because they're game engine structures, not our smart pointers
	if (proxyRenderTarget.uaView)
		proxyRenderTarget.uaView->Release();
	proxyRenderTarget.uaView = nullptr;

	if (proxyRenderTarget.srView)
		proxyRenderTarget.srView->Release();
	proxyRenderTarget.srView = nullptr;

	if (proxyRenderTarget.rtView)
		proxyRenderTarget.rtView->Release();
	proxyRenderTarget.rtView = nullptr;

	if (proxyRenderTarget.texture)
		proxyRenderTarget.texture->Release();
	proxyRenderTarget.texture = nullptr;

	// Do not need to replace render targets at native resolution
	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalRenderTarget.texture)
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTarget.texture)->GetDesc(&textureDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (originalRenderTarget.rtView)
		reinterpret_cast<ID3D11RenderTargetView*>(originalRenderTarget.rtView)->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (originalRenderTarget.srView)
		reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTarget.srView)->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc;
	if (originalRenderTarget.uaView)
		reinterpret_cast<ID3D11UnorderedAccessView*>(originalRenderTarget.uaView)->GetDesc(&uaViewDesc);

	// Scale texture dimensions (e.g., 1920x1080 @ 0.67 = 1280x720)
	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	if (originalRenderTarget.texture)
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxyRenderTarget.texture)));

	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTarget.texture)) {
		if (originalRenderTarget.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, reinterpret_cast<ID3D11RenderTargetView**>(&proxyRenderTarget.rtView)));

		if (originalRenderTarget.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxyRenderTarget.srView)));

		if (originalRenderTarget.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, reinterpret_cast<ID3D11UnorderedAccessView**>(&proxyRenderTarget.uaView)));
	}

#ifndef NDEBUG
	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTarget.texture)) {
		auto name = std::format("RT PROXY {}", index);
		texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto rtView = reinterpret_cast<ID3D11RenderTargetView*>(proxyRenderTarget.rtView)) {
		auto name = std::format("RTV PROXY {}", index);
		rtView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto srView = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTarget.srView)) {
		auto name = std::format("SRV PROXY {}", index);
		srView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto uaView = reinterpret_cast<ID3D11UnorderedAccessView*>(proxyRenderTarget.uaView)) {
		auto name = std::format("UAV PROXY {}", index);
		uaView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}
#endif
}

void Upscaling::UpdateDepth(float a_currentWidthRatio, float a_currentHeightRatio)
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Save the original depth stencil target
	originalDepthStencilTarget = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];
	auto& originalTarget = originalDepthStencilTarget;
	auto& proxyTarget = depthOverrideTarget;

	// Clean up existing proxy depth stencil target resources
	for (int i = 0; i < 4; i++) {
		if (proxyTarget.dsView[i]) {
			proxyTarget.dsView[i]->Release();
			proxyTarget.dsView[i] = nullptr;
		}
		if (proxyTarget.dsViewReadOnlyDepth[i]) {
			proxyTarget.dsViewReadOnlyDepth[i]->Release();
			proxyTarget.dsViewReadOnlyDepth[i] = nullptr;
		}
		if (proxyTarget.dsViewReadOnlyStencil[i]) {
			proxyTarget.dsViewReadOnlyStencil[i]->Release();
			proxyTarget.dsViewReadOnlyStencil[i] = nullptr;
		}
		if (proxyTarget.dsViewReadOnlyDepthStencil[i]) {
			proxyTarget.dsViewReadOnlyDepthStencil[i]->Release();
			proxyTarget.dsViewReadOnlyDepthStencil[i] = nullptr;
		}
	}

	if (proxyTarget.srViewDepth) {
		proxyTarget.srViewDepth->Release();
		proxyTarget.srViewDepth = nullptr;
	}

	if (proxyTarget.srViewStencil) {
		proxyTarget.srViewStencil->Release();
		proxyTarget.srViewStencil = nullptr;
	}

	if (proxyTarget.texture) {
		proxyTarget.texture->Release();
		proxyTarget.texture = nullptr;
	}

	// Do not need to replace depth at native resolution
	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	// Get original texture description
	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalTarget.texture)
		reinterpret_cast<ID3D11Texture2D*>(originalTarget.texture)->GetDesc(&textureDesc);

	// Scale texture dimensions
	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Create texture
	if (originalTarget.texture)
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxyTarget.texture)));

	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyTarget.texture)) {
		for (int i = 0; i < 4; i++) {
			if (originalTarget.dsView[i]) {
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				reinterpret_cast<ID3D11DepthStencilView*>(originalTarget.dsView[i])->GetDesc(&dsvDesc);
				DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &dsvDesc, reinterpret_cast<ID3D11DepthStencilView**>(&proxyTarget.dsView[i])));
			}

			if (originalTarget.dsViewReadOnlyDepth[i]) {
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				reinterpret_cast<ID3D11DepthStencilView*>(originalTarget.dsViewReadOnlyDepth[i])->GetDesc(&dsvDesc);
				DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &dsvDesc, reinterpret_cast<ID3D11DepthStencilView**>(&proxyTarget.dsViewReadOnlyDepth[i])));
			}

			if (originalTarget.dsViewReadOnlyStencil[i]) {
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				reinterpret_cast<ID3D11DepthStencilView*>(originalTarget.dsViewReadOnlyStencil[i])->GetDesc(&dsvDesc);
				DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &dsvDesc, reinterpret_cast<ID3D11DepthStencilView**>(&proxyTarget.dsViewReadOnlyStencil[i])));
			}

			if (originalTarget.dsViewReadOnlyDepthStencil[i]) {
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				reinterpret_cast<ID3D11DepthStencilView*>(originalTarget.dsViewReadOnlyDepthStencil[i])->GetDesc(&dsvDesc);
				DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &dsvDesc, reinterpret_cast<ID3D11DepthStencilView**>(&proxyTarget.dsViewReadOnlyDepthStencil[i])));
			}
		}

		if (originalTarget.srViewDepth) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			reinterpret_cast<ID3D11ShaderResourceView*>(originalTarget.srViewDepth)->GetDesc(&srvDesc);
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srvDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxyTarget.srViewDepth)));
		}

		if (originalTarget.srViewStencil) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			reinterpret_cast<ID3D11ShaderResourceView*>(originalTarget.srViewStencil)->GetDesc(&srvDesc);
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srvDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxyTarget.srViewStencil)));
		}
	}

#ifndef NDEBUG
	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyTarget.texture)) {
		auto name = std::string("DEPTH PROXY");
		texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto dsView = reinterpret_cast<ID3D11DepthStencilView*>(proxyTarget.dsView[0])) {
		auto name = std::string("DSV PROXY");
		dsView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto srView = reinterpret_cast<ID3D11ShaderResourceView*>(proxyTarget.srViewDepth)) {
		auto name = std::string("DEPTH SRV PROXY");
		srView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}
#endif
}

void Upscaling::OverrideRenderTarget(int index, bool a_doCopy)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Replace the game's render target with our scaled proxy version
	rendererData->renderTargets[index] = proxyRenderTargets[index];

	// Optionally perform expensive copy operation
	if (a_doCopy) {
		// Get dimensions of both textures
		D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture)->GetDesc(&srcDesc);
		reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture)->GetDesc(&dstDesc);

		D3D11_BOX srcBox;
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = dstDesc.Width;
		srcBox.bottom = dstDesc.Height;
		srcBox.back = 1;

		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture), 0, &srcBox);
	}
}

void Upscaling::ResetRenderTarget(int index, bool a_doCopy)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Optionally perform expensive copy operation before swapping back
	if (a_doCopy) {
		D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
		reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture)->GetDesc(&srcDesc);
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture)->GetDesc(&dstDesc);

		D3D11_BOX srcBox;
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = srcDesc.Width;
		srcBox.bottom = srcDesc.Height;
		srcBox.back = 1;

		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), 0, &srcBox);
	}

	// Restore the original render target
	rendererData->renderTargets[index] = originalRenderTargets[index];
}

void Upscaling::UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio)
{
	static auto previousWidthRatio = 0.0f;
	static auto previousHeightRatio = 0.0f;

	// Check for resolution update
	if (previousWidthRatio == a_currentWidthRatio && previousHeightRatio == a_currentHeightRatio)
		return;

	previousWidthRatio = a_currentWidthRatio;
	previousHeightRatio = a_currentHeightRatio;

	// Recreate render targets with new dimensions
	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		UpdateRenderTarget(renderTargetsPatch[i], a_currentWidthRatio, a_currentHeightRatio);

	// Recreate depth stencil target with new dimensions
	UpdateDepth(a_currentWidthRatio, a_currentHeightRatio);

	// Reset intermediate textures to force recreation with new dimensions
	upscalingTexture = nullptr;

	// Get the frame buffer texture description to match its properties
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);

	frameBufferResource->Release();

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = {.MipSlice = 0 }
	};

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	// Intermediate upscaling texture (stores DLSS/FSR output)
	upscalingTexture = std::make_unique<Texture2D>(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);
}

void Upscaling::OverrideRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Replace all patched render targets with their scaled proxy versions
	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++) {
		int targetIndex = renderTargetsPatch[i];
		bool shouldCopy = std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), targetIndex) != a_indicesToCopy.end();
		OverrideRenderTarget(targetIndex, shouldCopy);
	}

	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	// Update render target metadata to match the scaled resolution
	// This ensures code that queries render target dimensions get the correct values
	for (int i = 0; i < 100; i++) {
		originalRenderTargetData[i] = renderTargetManager->renderTargetData[i];
		renderTargetManager->renderTargetData[i].width = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].width) * renderTargetManager->dynamicWidthRatio);
		renderTargetManager->renderTargetData[i].height = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].height) * renderTargetManager->dynamicHeightRatio);
	}

	// Check and override pixel shader SRVs that reference original render targets
	// Get currently bound pixel shader SRVs (first 16 slots)
	ID3D11ShaderResourceView* boundSRVs[16] = {};
	context->PSGetShaderResources(0, 16, boundSRVs);

	// Scan through bound SRVs and replace any that match original render targets
	for (int srvSlot = 0; srvSlot < 16; srvSlot++) {
		if (!boundSRVs[srvSlot])
			continue;

		// Check if this SRV matches any original render target
		for (int rtIndex = 0; rtIndex < ARRAYSIZE(renderTargetsPatch); rtIndex++) {
			int targetIndex = renderTargetsPatch[rtIndex];
			auto& originalRT = originalRenderTargets[targetIndex];
			auto& proxyRT = proxyRenderTargets[targetIndex];

			// If the bound SRV matches an original render target SRV and we have a proxy
			if (boundSRVs[srvSlot] == reinterpret_cast<ID3D11ShaderResourceView*>(originalRT.srView) && proxyRT.srView) {
				// Replace with the proxy SRV
				auto proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRT.srView);
				context->PSSetShaderResources(srvSlot, 1, &proxySRV);
				break;
			}
		}

		// Release the reference from PSGetShaderResources
		boundSRVs[srvSlot]->Release();
	}

	// Temporarily disable dynamic resolution
	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);
}

void Upscaling::ResetRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	// Restore all original full-resolution render targets
	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++) {
		int targetIndex = renderTargetsPatch[i];
		// If indices array is empty, copy all. Otherwise, only copy if in the array
		bool shouldCopy = a_indicesToCopy.empty() ||
			std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), targetIndex) != a_indicesToCopy.end();
		ResetRenderTarget(targetIndex, shouldCopy);
	}

	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	// Restore original render target metadata (full-resolution dimensions)
	for (int i = 0; i < 100; i++) {
		renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
	}

	// Check and restore pixel shader SRVs that reference proxy render targets
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Get currently bound pixel shader SRVs (first 16 slots)
	ID3D11ShaderResourceView* boundSRVs[16] = {};
	context->PSGetShaderResources(0, 16, boundSRVs);

	// Scan through bound SRVs and replace any that match proxy render targets
	for (int srvSlot = 0; srvSlot < 16; srvSlot++) {
		if (!boundSRVs[srvSlot])
			continue;

		// Check if this SRV matches any proxy render target
		for (int rtIndex = 0; rtIndex < ARRAYSIZE(renderTargetsPatch); rtIndex++) {
			int targetIndex = renderTargetsPatch[rtIndex];
			auto& originalRT = originalRenderTargets[targetIndex];
			auto& proxyRT = proxyRenderTargets[targetIndex];

			// If the bound SRV matches a proxy render target SRV and we have an original
			if (boundSRVs[srvSlot] == reinterpret_cast<ID3D11ShaderResourceView*>(proxyRT.srView) && originalRT.srView) {
				// Replace with the original SRV
				auto originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRT.srView);
				context->PSSetShaderResources(srvSlot, 1, &originalSRV);
				break;
			}
		}

		// Release the reference from PSGetShaderResources
		boundSRVs[srvSlot]->Release();
	}

	// Enable dynamic resolution again
	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, true);
}

void Upscaling::OverrideDepth(bool a_doCopy)
{
	if (!depthOverrideTarget.texture)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Save the original depth stencil target (with dynamic resolution)
	originalDepthStencilTarget = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];

	// Optionally perform expensive copy operation
	if (a_doCopy) {
		static auto gameViewport = Util::State_GetSingleton();

		// Only copy depth once per frame
		static auto previousFrame = gameViewport->frameCount;
		if (previousFrame != gameViewport->frameCount)
			CopyDepth();
		previousFrame = gameViewport->frameCount;
	}

	// Replace the entire depth stencil target
	originalDepthStencilTarget = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];
	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain] = depthOverrideTarget;
}

void Upscaling::ResetDepth()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Restore the original depth stencil target with dynamic resolution
	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain] = originalDepthStencilTarget;
}

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static auto samplerStates = SamplerStates::GetSingleton();
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Store original sampler states from the game
	// These will be used to restore the original states later
	for (int a = 0; a < 320; a++)
		originalSamplerStates[a] = samplerStates->a[a];

	static float previousMipBias = 1.0f;

	// Check if mip bias has changed - only recreate sampler states if needed
	if (previousMipBias == a_currentMipBias)
		return;

	previousMipBias = a_currentMipBias;

	// Create new sampler states with negative LOD bias
	for (int a = 0; a < 320; a++) {
		// Release existing biased sampler state
		if (biasedSamplerStates[a]){
			biasedSamplerStates[a]->Release();
			biasedSamplerStates[a] = nullptr;
		}

		// Create modified version with LOD bias applied
		if (auto samplerState = originalSamplerStates[a]) {
			D3D11_SAMPLER_DESC samplerDesc;
			samplerState->GetDesc(&samplerDesc);

			// Only modify 16x anisotropic samplers (the high-quality ones)
			if (samplerDesc.Filter == D3D11_FILTER_ANISOTROPIC) {
				samplerDesc.MaxAnisotropy = 8; // Reduced from 16x to 8x for performance
				samplerDesc.MipLODBias = a_currentMipBias;
			}

			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &biasedSamplerStates[a]));
		}
	}
}

void Upscaling::OverrideSamplerStates()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = biasedSamplerStates[a];
}

void Upscaling::ResetSamplerStates()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = originalSamplerStates[a];
}

void Upscaling::CopyDepth()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	// Calculate both display (screen) and render (scaled) resolutions
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	// Get the scaled depth buffer as input
	auto depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth);

	// Also update the linearized depth used by other effects using compute shader
	auto linearDepthUAV = reinterpret_cast<ID3D11UnorderedAccessView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainDepthMips].uaView);

	{
		UpdateAndBindUpscalingCB(context, screenSize, renderSize);

		// Bind scaled depth as input (SRV)
		ID3D11ShaderResourceView* views[] = { depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		// Bind full-resolution depth outputs (UAV)
		ID3D11UnorderedAccessView* uavs[] = { linearDepthUAV };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		// Run depth upscaling compute shader
		context->CSSetShader(GetOverrideLinearDepthCS(), nullptr, 0);

		// Dispatch with 8x8 thread groups covering the full screen resolution
		uint dispatchX = (uint)std::ceil(screenSize.x / 8.0f);
		uint dispatchY = (uint)std::ceil(screenSize.y / 8.0f);
		context->Dispatch(dispatchX, dispatchY, 1);

		// Clean up compute shader bindings to avoid resource hazards
		ID3D11ShaderResourceView* nullViews[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

		ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

		ID3D11ComputeShader* nullCS = nullptr;
		context->CSSetShader(nullCS, nullptr, 0);
	}

	// Copy depth to depthOverrideTexture using pixel shader with SV_Depth
	{
		// Save current state
		winrt::com_ptr<ID3D11DepthStencilState> oldDepthStencilState;
		UINT oldStencilRef = 0;
		context->OMGetDepthStencilState(oldDepthStencilState.put(), &oldStencilRef);

		winrt::com_ptr<ID3D11BlendState> oldBlendState;
		FLOAT oldBlendFactor[4] = {};
		UINT oldSampleMask = 0;
		context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

		winrt::com_ptr<ID3D11RasterizerState> oldRasterizerState;
		context->RSGetState(oldRasterizerState.put());

		ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		winrt::com_ptr<ID3D11DepthStencilView> oldDSV;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV.put());

		D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		context->RSGetViewports(&numViewports, oldViewports);

		winrt::com_ptr<ID3D11VertexShader> oldVS;
		context->VSGetShader(oldVS.put(), nullptr, nullptr);

		winrt::com_ptr<ID3D11PixelShader> oldPS;
		context->PSGetShader(oldPS.put(), nullptr, nullptr);

		ID3D11ShaderResourceView* oldPSSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
		context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, oldPSSRVs);

		winrt::com_ptr<ID3D11InputLayout> oldInputLayout;
		context->IAGetInputLayout(oldInputLayout.put());

		D3D11_PRIMITIVE_TOPOLOGY oldTopology;
		context->IAGetPrimitiveTopology(&oldTopology);

		winrt::com_ptr<ID3D11Buffer> oldPSCB;
		context->PSGetConstantBuffers(0, 1, oldPSCB.put());

		// Set render state using cached state objects
		auto dsvPointer = reinterpret_cast<ID3D11DepthStencilView*>(depthOverrideTarget.dsView[0]);
		context->OMSetRenderTargets(0, nullptr, dsvPointer);
		context->OMSetDepthStencilState(GetCopyDepthStencilState(), 0xFF);
		FLOAT blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->OMSetBlendState(GetCopyBlendState(), blendFactor, 0xFFFFFFFF);
		context->RSSetState(GetCopyRasterizerState());

		// Set viewport to render size
		D3D11_VIEWPORT viewport = {
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = renderSize.x,
			.Height = renderSize.y,
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f
		};
		context->RSSetViewports(1, &viewport);

		// Bind upscaling constant buffer for pixel shader
		auto upscalingCB = GetUpscalingCB();
		auto upscalingBuffer = upscalingCB->CB();
		context->PSSetConstantBuffers(0, 1, &upscalingBuffer);

		// Bind shaders
		context->VSSetShader(GetCopyDepthVS(), nullptr, 0);
		context->PSSetShader(GetCopyDepthPS(), nullptr, 0);

		// Bind depth texture as input
		context->PSSetShaderResources(0, 1, &depthSRV);

		// Bind sampler state for depth sampling
		ID3D11SamplerState* samplers[] = { GetCopySamplerState() };
		context->PSSetSamplers(0, 1, samplers);

		// Set input layout and topology for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Draw fullscreen triangle (3 vertices, no vertex buffer needed)
		context->Draw(3, 0);

		// Restore all saved state
		context->OMSetDepthStencilState(oldDepthStencilState.get(), oldStencilRef);
		context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
		context->RSSetState(oldRasterizerState.get());
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV.get());
		context->RSSetViewports(numViewports, oldViewports);
		context->VSSetShader(oldVS.get(), nullptr, 0);
		context->PSSetShader(oldPS.get(), nullptr, 0);
		context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, oldPSSRVs);
		context->IASetInputLayout(oldInputLayout.get());
		context->IASetPrimitiveTopology(oldTopology);

		// Restore pixel shader constant buffer
		ID3D11Buffer* psCBArray[] = { oldPSCB.get() };
		context->PSSetConstantBuffers(0, 1, psCBArray);

		// Release old RTVs
		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
			if (oldRTVs[i])
				oldRTVs[i]->Release();
		}

		// Release old PS SRVs
		for (int i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++) {
			if (oldPSSRVs[i])
				oldPSSRVs[i]->Release();
		}
	}
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod(bool a_checkMenu)
{
	auto streamline = Streamline::GetSingleton();
	
	static auto ui = RE::UI::GetSingleton();
	
	// Disable the upscaling method when certain menus are open
	if (a_checkMenu){
		if (ui->GetMenuOpen("ExamineMenu") || ui->GetMenuOpen("PipboyMenu") || ui->GetMenuOpen("LoadingMenu") || ui->GetMenuOpen("TerminalMenu"))
			return UpscaleMethod::kDisabled;
	}

	UpscaleMethod currentUpscaleMethod = (UpscaleMethod)settings.upscaleMethodPreference;
		
	// If DLSS is not available, default to FSR
	if (!streamline->featureDLSS && currentUpscaleMethod == UpscaleMethod::kDLSS)
		currentUpscaleMethod = UpscaleMethod::kFSR;

	// ENB is loaded, disable FSR
	if (enbLoaded && currentUpscaleMethod == UpscaleMethod::kFSR)
		currentUpscaleMethod = UpscaleMethod::kDisabled;

	return currentUpscaleMethod;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMethodNoMenu = UpscaleMethod::kDisabled;

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	// Detect when upscaling method changes and manage resources accordingly
	if (previousUpscaleMethodNoMenu != upscaleMethodNoMenu) {
		// Clean up resources from the previous upscaling method
		if (previousUpscaleMethodNoMenu == UpscaleMethod::kDisabled)
			CreateUpscalingResources();  // Transitioning from disabled to enabled
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();  // Switching away from FSR
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();  // Switching away from DLSS

		// Create resources for the new upscaling method
		if (upscaleMethodNoMenu == UpscaleMethod::kDisabled)
			DestroyUpscalingResources();  // Transitioning to disabled
		else if (upscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();  // Switching to FSR

		previousUpscaleMethodNoMenu = upscaleMethodNoMenu;
	}
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS) {
		logger::debug("Compiling DilateMotionVectorCS.hlsl");
		dilateMotionVectorCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/DilateMotionVectorCS.hlsl", {}, "cs_5_0"));
	}
	return dilateMotionVectorCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideLinearDepthCS()
{
	if (!overrideLinearDepthCS) {
		logger::debug("Compiling OverrideLinearDepthCS.hlsl");
		overrideLinearDepthCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideLinearDepthCS.hlsl", {}, "cs_5_0"));
	}
	return overrideLinearDepthCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS) {
		logger::debug("Compiling OverrideDepthCS.hlsl");
		overrideDepthCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideDepthCS.hlsl", {}, "cs_5_0"));
	}
	return overrideDepthCS.get();
}

ID3D11VertexShader* Upscaling::GetCopyDepthVS()
{
	if (!copyDepthVS) {
		logger::debug("Compiling CopyDepthVS.hlsl");
		copyDepthVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/CopyDepthVS.hlsl", {}, "vs_5_0"));
	}
	return copyDepthVS.get();
}

ID3D11PixelShader* Upscaling::GetCopyDepthPS()
{
	if (!copyDepthPS) {
		logger::debug("Compiling CopyDepthPS.hlsl");
		copyDepthPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/CopyDepthPS.hlsl", {}, "ps_5_0"));
	}
	return copyDepthPS.get();
}

ID3D11DepthStencilState* Upscaling::GetCopyDepthStencilState()
{
	if (!copyDepthStencilState) {
		logger::debug("Creating depth copy depth stencil state");

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {
			.DepthEnable = TRUE,
			.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D11_COMPARISON_ALWAYS,
			.StencilEnable = FALSE,
			.StencilReadMask = 0xFF,
			.StencilWriteMask = 0xFF,
			.FrontFace = {
				.StencilFailOp = D3D11_STENCIL_OP_KEEP,
				.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
				.StencilPassOp = D3D11_STENCIL_OP_REPLACE,
				.StencilFunc = D3D11_COMPARISON_ALWAYS
			},
			.BackFace = {
				.StencilFailOp = D3D11_STENCIL_OP_KEEP,
				.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
				.StencilPassOp = D3D11_STENCIL_OP_REPLACE,
				.StencilFunc = D3D11_COMPARISON_ALWAYS
			}
		};

		static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, copyDepthStencilState.put()));
	}
	return copyDepthStencilState.get();
}

ID3D11BlendState* Upscaling::GetCopyBlendState()
{
	if (!copyBlendState) {
		logger::debug("Creating depth copy blend state");

		D3D11_BLEND_DESC blendDesc = {
			.AlphaToCoverageEnable = FALSE,
			.IndependentBlendEnable = FALSE,
			.RenderTarget = {{
				.BlendEnable = FALSE,
				.RenderTargetWriteMask = 0  // No color writes
			}}
		};

		static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, copyBlendState.put()));
	}
	return copyBlendState.get();
}

ID3D11RasterizerState* Upscaling::GetCopyRasterizerState()
{
	if (!copyRasterizerState) {
		logger::debug("Creating depth copy rasterizer state");

		D3D11_RASTERIZER_DESC rasterizerDesc = {
			.FillMode = D3D11_FILL_SOLID,
			.CullMode = D3D11_CULL_NONE,
			.FrontCounterClockwise = FALSE,
			.DepthBias = 0,
			.DepthBiasClamp = 0.0f,
			.SlopeScaledDepthBias = 0.0f,
			.DepthClipEnable = FALSE,
			.ScissorEnable = FALSE,
			.MultisampleEnable = FALSE,
			.AntialiasedLineEnable = FALSE
		};

		static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, copyRasterizerState.put()));
	}
	return copyRasterizerState.get();
}

ID3D11SamplerState* Upscaling::GetCopySamplerState()
{
	if (!copySamplerState) {
		logger::debug("Creating depth copy sampler state");

		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MipLODBias = 0.0f,
			.MaxAnisotropy = 1,
			.ComparisonFunc = D3D11_COMPARISON_NEVER,
			.BorderColor = { 0.0f, 0.0f, 0.0f, 0.0f },
			.MinLOD = 0.0f,
			.MaxLOD = D3D11_FLOAT32_MAX
		};

		static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, copySamplerState.put()));
	}
	return copySamplerState.get();
}

ID3D11PixelShader* Upscaling::GetBSImagespaceShaderSSLRRaytracing()
{
	if (!BSImagespaceShaderSSLRRaytracing) {
		logger::debug("Compiling BSImagespaceShaderSSLRRaytracing.hlsl");
		BSImagespaceShaderSSLRRaytracing.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/BSImagespaceShaderSSLRRaytracing.hlsl", {}, "ps_5_0"));
	}
	return BSImagespaceShaderSSLRRaytracing.get();
}

ConstantBuffer* Upscaling::GetUpscalingCB()
{
	static std::unique_ptr<ConstantBuffer> upscalingCB = nullptr;

	if (!upscalingCB) {
		logger::debug("Creating UpscalingCB");
		upscalingCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpscalingCB>());
	}
	return upscalingCB.get();
}

void Upscaling::UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize)
{
#if defined(FALLOUT_POST_NG)
	static auto cameraNear = (float*)REL::ID(2712882).address();
	static auto cameraFar = (float*)REL::ID(2712883).address();
#else
	static auto cameraNear = (float*)REL::ID(57985).address();
	static auto cameraFar = (float*)REL::ID(958877).address();
#endif

	float4 cameraData{};
	cameraData.x = *cameraFar;
	cameraData.y = *cameraNear;
	cameraData.z = cameraData.x - cameraData.y;
	cameraData.w = cameraData.x * cameraData.y;

	UpscalingCB upscalingData;
	upscalingData.ScreenSize[0] = static_cast<uint>(a_screenSize.x);
	upscalingData.ScreenSize[1] = static_cast<uint>(a_screenSize.y);
	upscalingData.RenderSize[0] = static_cast<uint>(a_renderSize.x);
	upscalingData.RenderSize[1] = static_cast<uint>(a_renderSize.y);
	upscalingData.CameraData = cameraData;

	auto upscalingCB = GetUpscalingCB();
	upscalingCB->Update(upscalingData);

	auto upscalingBuffer = upscalingCB->CB();
	a_context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
}

void Upscaling::UpdateGameSettings()
{
	static auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();

	// Automatically disable FXAA
#if defined(FALLOUT_POST_NG)
	imageSpaceManager->effectList[(uint)RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA]->isActive = false;
#else
	imageSpaceManager->effectList[17]->isActive = false;
#endif

	// Automatically enable TAA
#if defined(FALLOUT_POST_NG)
	static auto enableTAA = (bool*)REL::ID(2704658).address();
#else
	static auto enableTAA = (bool*)REL::ID(460417).address();
#endif
	* enableTAA = true;
}

void Upscaling::UpdateUpscaling()
{
	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	upscaleMethodNoMenu = GetUpscaleMethod(false);
	upscaleMethod = GetUpscaleMethod(true);

	// Calculate render resolution scale from quality mode
	// Example: Quality mode returns upscale ratio of ~1.5x, so resolutionScale = 1/1.5 = 0.67
	float resolutionScale = enbLoaded || upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);

	// Calculate mipmap LOD bias
	// Example: 0.67 scale -> log2(0.67) = -0.58
	float currentMipBias = std::log2f(resolutionScale);

	if (upscaleMethodNoMenu == UpscaleMethod::kDLSS || upscaleMethodNoMenu == UpscaleMethod::kFSR)
		currentMipBias -= 1.0f;

	UpdateSamplerStates(currentMipBias);
	UpdateRenderTargets(resolutionScale, resolutionScale);
	UpdateGameSettings();

	// Disable upscaling when certain menus are open (Pip-Boy, Examine, Loading)
	if (upscaleMethod == UpscaleMethod::kDisabled) {
		resolutionScale = 1.0f;
	}

	// Apply TAA jitter (shifts projection matrix sub-pixel per frame)
	if (upscaleMethod != UpscaleMethod::kDisabled) {
		auto screenWidth = gameViewport->screenWidth;
		auto screenHeight = gameViewport->screenHeight;

		auto renderWidth = static_cast<uint>(static_cast<float>(screenWidth) * resolutionScale);
		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);
		ffxFsr3GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);

		// Convert to NDC (X negated for DirectX)
		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(screenWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(screenHeight);
	}

	renderTargetManager->dynamicWidthRatio = resolutionScale;
	renderTargetManager->dynamicHeightRatio = resolutionScale;

	renderTargetManager->isDynamicResolutionCurrentlyActivated = renderTargetManager->dynamicWidthRatio != 1.0 || renderTargetManager->dynamicHeightRatio != 1.0;

	CheckResources();
}

void Upscaling::Upscale()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Unbind render targets to avoid resource hazards
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	// Copy frame buffer to upscaling texture (input for DLSS/FSR)
	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	frameBufferResource->Release();

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	// DLSS: Dilate motion vectors for better temporal stability
	if (upscaleMethod == UpscaleMethod::kDLSS){
		{
			UpdateAndBindUpscalingCB(context, screenSize, renderSize);

			auto motionVectorSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMotionVectors].srView);
			auto depthTextureSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth);

			ID3D11ShaderResourceView* views[2] = { motionVectorSRV, depthTextureSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { dilatedMotionVectorTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetDilateMotionVectorCS(), nullptr, 0);

			uint dispatchX = (uint)std::ceil(renderSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(renderSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		// Unbind compute resources
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	// Execute upscaling
	if (upscaleMethod == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTexture.get(), dilatedMotionVectorTexture.get(), jitter, renderSize, settings.qualityMode);
	else if (upscaleMethod == UpscaleMethod::kFSR)
		FidelityFX::GetSingleton()->Upscale(upscalingTexture.get(), jitter, renderSize, 0.0f);

	context->CopyResource(frameBufferResource, upscalingTexture->resource.get());
}

void Upscaling::CreateUpscalingResources()
{
	// Only create DLSS-specific resources if DLSS is available
	if (Streamline::GetSingleton()->featureDLSS) {
		auto renderer = RE::BSGraphics::RendererData::GetSingleton();
		auto& main = renderer->renderTargets[(uint)Util::RenderTarget::kMain];

		// Get main render target dimensions
		D3D11_TEXTURE2D_DESC texDesc{};
		reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		// Create view descriptions
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = {.MipSlice = 0 }
		};

		// Create dilated motion vector texture
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		uavDesc.Format = texDesc.Format;

		dilatedMotionVectorTexture = std::make_unique<Texture2D>(texDesc);
		dilatedMotionVectorTexture->CreateUAV(uavDesc);
	}
}

void Upscaling::DestroyUpscalingResources()
{
	// Clean up DLSS-specific resources
	if (Streamline::GetSingleton()->featureDLSS) {
		dilatedMotionVectorTexture = nullptr;  // Smart pointer automatically releases D3D resources
	}
}

void Upscaling::PatchSSRShader()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Replace the game's SSR pixel shader with our custom one that fixes scaled render targets
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}