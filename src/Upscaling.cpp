#include "Upscaling.h"

#include <SimpleIni.h>

/** @brief Hook for updating temporal data and jitter */
struct BSGraphics_State_UpdateTemporalData
{
	static void thunk(RE::BSGraphics::State* a_state)
	{
		func(a_state);
		Upscaling::GetSingleton()->UpdateJitter();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to disable TAA when upscaling is active */
struct ImageSpaceEffectTemporalAA_IsActive
{
	static bool thunk(struct ImageSpaceEffectTemporalAA* This)
	{
		auto upscaleMethod = Upscaling::GetSingleton()->GetUpscaleMethod(true);
		return upscaleMethod == Upscaling::UpscaleMethod::kDisabled && func(This);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to disable dynamic resolution upsampling when using FSR/DLSS */
struct ImageSpaceEffectUpsampleDynamicResolution_IsActive
{
	static bool thunk(struct ImageSpaceEffectUpsampleDynamicResolution*)
	{
		auto upscaleMethod = Upscaling::GetSingleton()->GetUpscaleMethod(true);
		return upscaleMethod == Upscaling::UpscaleMethod::kDisabled;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook to replace dynamic resolution viewport with upscaling pass */
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

/** @brief Hook for deferred decals rendering with sampler state override */
struct DrawWorld_Render_PreUI_DeferredDecals
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

/** @brief Hook for forward rendering pass with reactive mask generation */
struct DrawWorld_Render_PreUI_Forward
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();

		upscaling->OverrideSamplerStates();
		func(This);
		upscaling->ResetSamplerStates();

		auto upscaleMethod = upscaling->GetUpscaleMethod(false);
		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->GenerateReactiveMask();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for environment mapping with render target override */
struct BSDFComposite_Envmap
{
	static void thunk(void* This, uint a2, bool a3)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		if (requiresOverride) {
			upscaling->OverrideRenderTargets();
			upscaling->OverrideDepth();
		}

		func(This, a2, a3);

		if (requiresOverride) {
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for lens flare rendering with depth override */
struct BSImagespaceShaderLensFlare_RenderLensFlare
{
	static void thunk(RE::NiCamera* a_camera)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
		bool requiresOverride = renderTargetManager->dynamicHeightRatio != 1.0 || renderTargetManager->dynamicWidthRatio != 1.0;

		if (requiresOverride) {
			upscaling->OverrideDepth();
		}

		func(a_camera);

		if (requiresOverride) {
			upscaling->ResetDepth();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for SSR shader with custom raytracing implementation */
struct BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique
{
	static void thunk(RE::BSShader* This, uint a2, uint a3, uint a4, uint a5)
	{
		func(This, a2, a3, a4, a5);
		Upscaling::GetSingleton()->PatchSSRShader();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

/** @brief Hook for forward alpha rendering with opaque texture copy for FSR */
struct DrawWorld_Forward_ForwardAlphaImpl
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();
		auto upscaleMethod = upscaling->GetUpscaleMethod(false);
		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->CopyOpaqueTexture();

		func(This);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	// Control jitters, dynamic resolution, and sampler states
	stl::write_thunk_call<BSGraphics_State_UpdateTemporalData>(REL::ID(502840).address() + 0x3C1);

	// Disable TAA shader if using alternative scaling method
	stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

	// Disable dynamic resolution shader if using alternative scaling method
	stl::write_vfunc<0x8, ImageSpaceEffectUpsampleDynamicResolution_IsActive>(RE::VTABLE::ImageSpaceEffectUpsampleDynamicResolution[0]);

	// Replace original upscaling pass
	stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);

	// Disable BSGraphics::RenderTargetManager::UpdateDynamicResolution
	REL::Relocation<std::uintptr_t> target{ REL::ID(984743), 0x14B };
	REL::safe_fill(target.address(), 0x90, 5);

	// Control sampler states for mipmap bias
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(984743).address() + 0x17F);
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredDecals>(REL::ID(984743).address() + 0x189);
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(984743).address() + 0x1C9);

	// Fix dynamic resolution for BSDFComposite
	stl::write_thunk_call<BSDFComposite_Envmap>(REL::ID(728427).address() + 0x8DC);

	// Fix dynamic resolution for Lens Flare visibility
	stl::detour_thunk<BSImagespaceShaderLensFlare_RenderLensFlare>(REL::ID(676108));

	// Fix dynamic resolution for Screenspace Reflections
	stl::write_thunk_call<BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique>(REL::ID(779077).address() + 0x1C);

	// Generate reactive mask for FSR
	stl::write_thunk_call<DrawWorld_Forward_ForwardAlphaImpl>(REL::ID(656535).address() + 0x2E8);
}

struct SamplerStates
{
	ID3D11SamplerState* a[320];

	static SamplerStates* GetSingleton()
	{
		static auto samplerStates = reinterpret_cast<SamplerStates*>(REL::ID(44312).address());
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
	settings.sharpness = static_cast<float>(ini.GetDoubleValue("Settings", "fSharpness", 0.5));
}

void Upscaling::OnDataLoaded()
{
	RE::UI::GetSingleton()->RegisterSink<RE::MenuOpenCloseEvent>(this);
	LoadSettings();
}

RE::BSEventNotifyControl Upscaling::ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
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

	// Get the original render target properties to create a scaled version
	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalRenderTarget.texture)
		originalRenderTarget.texture->GetDesc(&textureDesc);

	// Get all view descriptions from the original render target
	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (originalRenderTarget.rtView)
		originalRenderTarget.rtView->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (originalRenderTarget.srView)
		originalRenderTarget.srView->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc;
	if (originalRenderTarget.uaView)
		originalRenderTarget.uaView->GetDesc(&uaViewDesc);

	// Scale the texture dimensions based on the current render resolution
	// For example, 1920x1080 at 0.67 ratio becomes 1280x720
	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Create the scaled texture
	if (originalRenderTarget.texture)
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &proxyRenderTarget.texture));

	// Create all views (RTV, SRV, UAV) for the proxy texture if the original had them
	if (auto texture = proxyRenderTarget.texture) {
		if (originalRenderTarget.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, &proxyRenderTarget.rtView));

		if (originalRenderTarget.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, &proxyRenderTarget.srView));

		if (originalRenderTarget.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, &proxyRenderTarget.uaView));
	}

#ifndef NDEBUG
	// Set debug names for easier identification in graphics debuggers (PIX, RenderDoc, etc.)
	if (auto texture = proxyRenderTarget.texture) {
		auto name = std::format("RT PROXY {}", index);
		texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto srView = proxyRenderTarget.srView) {
		auto name = std::format("SRV PROXY {}", index);
		srView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto rtView = proxyRenderTarget.rtView) {
		auto name = std::format("RTV PROXY {}", index);
		rtView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto uaView = proxyRenderTarget.uaView) {
		auto name = std::format("UAV PROXY {}", index);
		uaView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}
#endif
}

void Upscaling::OverrideRenderTarget(int index)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->renderTargets[index] = proxyRenderTargets[index];

	D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
	originalRenderTargets[index].texture->GetDesc(&srcDesc);
	proxyRenderTargets[index].texture->GetDesc(&dstDesc);

	D3D11_BOX srcBox;
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = dstDesc.Width;
	srcBox.bottom = dstDesc.Height;
	srcBox.back = 1;

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->CopySubresourceRegion(proxyRenderTargets[index].texture, 0, 0, 0, 0, originalRenderTargets[index].texture, 0, &srcBox);
}

void Upscaling::ResetRenderTarget(int index)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->renderTargets[index] = originalRenderTargets[index];

	D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
	proxyRenderTargets[index].texture->GetDesc(&srcDesc);
	originalRenderTargets[index].texture->GetDesc(&dstDesc);

	D3D11_BOX srcBox;
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = srcDesc.Width;
	srcBox.bottom = srcDesc.Height;
	srcBox.back = 1;

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->CopySubresourceRegion(originalRenderTargets[index].texture, 0, 0, 0, 0, proxyRenderTargets[index].texture, 0, &srcBox);
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

	upscalingTexture = nullptr;
	depthOverrideTexture = nullptr;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);

	// Release the resource acquired by GetResource
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

	upscalingTexture = std::make_unique<Texture2D>(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	depthOverrideTexture = std::make_unique<Texture2D>(texDesc);
	depthOverrideTexture->CreateSRV(srvDesc);
	depthOverrideTexture->CreateUAV(uavDesc);
}

void Upscaling::OverrideRenderTargets()
{
	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		OverrideRenderTarget(renderTargetsPatch[i]);

	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	for (int i = 0; i < 100; i++) {
		originalRenderTargetData[i] = renderTargetManager->renderTargetData[i];
		renderTargetManager->renderTargetData[i].width = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].width) * renderTargetManager->dynamicWidthRatio);
		renderTargetManager->renderTargetData[i].height = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].height) * renderTargetManager->dynamicHeightRatio);
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);
}

void Upscaling::ResetRenderTargets()
{
	for(int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		ResetRenderTarget(renderTargetsPatch[i]);

	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	for (int i = 0; i < 100; i++) {
		renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, true);
}

void Upscaling::OverrideDepth()
{
	static auto gameViewport = Util::State_GetSingleton();

	static auto previousFrame = gameViewport->frameCount;
	if (previousFrame != gameViewport->frameCount)
		CopyDepth();
	previousFrame = gameViewport->frameCount;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	originalDepthView.copy_from(rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth);

	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth = depthOverrideTexture->srv.get();
}

void Upscaling::ResetDepth()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth = originalDepthView.get();
}

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static auto samplerStates = SamplerStates::GetSingleton();
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Store original sampler states from the game
	// These will be used to restore the original states later
	for (int a = 0; a < 320; a++) {
		originalSamplerStates[a].copy_from(samplerStates->a[a]);
	}

	static float previousMipBias = 1.0f;

	// Check if mip bias has changed - only recreate sampler states if needed
	// This optimization avoids recreating 320 sampler states every frame
	if (previousMipBias == a_currentMipBias)
		return;

	previousMipBias = a_currentMipBias;

	// Create new sampler states with negative LOD bias
	// Negative bias = sharper textures, compensates for lower render resolution
	for (int a = 0; a < 320; a++) {
		// Release existing biased sampler state
		biasedSamplerStates[a] = nullptr;

		// Create modified version with LOD bias applied
		if (auto samplerState = originalSamplerStates[a].get()) {
			D3D11_SAMPLER_DESC samplerDesc;
			samplerState->GetDesc(&samplerDesc);

			// Only modify 16x anisotropic samplers (the high-quality ones)
			// Reduce to 8x aniso and apply negative MipLODBias to sharpen textures
			if (samplerDesc.Filter == D3D11_FILTER_ANISOTROPIC && samplerDesc.MaxAnisotropy == 16) {
				samplerDesc.MaxAnisotropy = 8;
				samplerDesc.MipLODBias = a_currentMipBias;  // Usually negative (e.g., -0.5)
			}

			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, biasedSamplerStates[a].put()));
		}

		// Update the game's sampler state pointer to our biased version
		samplerStates->a[a] = biasedSamplerStates[a].get();
	}
}

void Upscaling::OverrideSamplerStates()
{
	if (GetUpscaleMethod(true) == UpscaleMethod::kDisabled)
		return;

	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = biasedSamplerStates[a].get();
}

void Upscaling::ResetSamplerStates()
{
	if (GetUpscaleMethod(true) == UpscaleMethod::kDisabled)
		return;

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = originalSamplerStates[a].get();
}

void Upscaling::CopyDepth()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	auto depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth);

	auto depthUAV = depthOverrideTexture->uav.get();
	auto linearDepthUAV = reinterpret_cast<ID3D11UnorderedAccessView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainDepthMips].uaView);

	{
#if defined(FALLOUT_POST_NG)
		float cameraNear = *(float*)REL::ID(2712882).address();
		float cameraFar = *(float*)REL::ID(2712883).address();
#else
		float cameraNear = *(float*)REL::ID(57985).address();
		float cameraFar = *(float*)REL::ID(958877).address();
#endif

		float4 cameraData{};
		cameraData.x = cameraFar;
		cameraData.y = cameraNear;
		cameraData.z = cameraFar - cameraNear;
		cameraData.w = cameraFar * cameraNear;

		UpscalingCB upscalingData;
		upscalingData.ScreenSize[0] = static_cast<uint>(screenSize.x);
		upscalingData.ScreenSize[1] = static_cast<uint>(screenSize.y);

		upscalingData.RenderSize[0] = static_cast<uint>(renderSize.x);
		upscalingData.RenderSize[1] = static_cast<uint>(renderSize.y);

		upscalingData.CameraData = cameraData;

		auto upscalingCB = GetUpscalingCB();
		upscalingCB->Update(upscalingData);

		auto upscalingBuffer = upscalingCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		{
			ID3D11ShaderResourceView* views[] = { depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[] = { depthUAV, linearDepthUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetOverrideDepthCS(), nullptr, 0);

			uint dispatchX = (uint)std::ceil(screenSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(screenSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod(bool a_checkMenu)
{
	auto streamline = Streamline::GetSingleton();
	
	static auto ui = RE::UI::GetSingleton();
	
	// Disable the upscaling method when certain menus are open
	if (a_checkMenu){
		if (ui->GetMenuOpen("ExamineMenu") || ui->GetMenuOpen("PipboyMenu") || ui->GetMenuOpen("LoadingMenu"))
			return UpscaleMethod::kDisabled;
	}
		
	// If DLSS is not available, default to FSR
	if (!streamline->featureDLSS && settings.upscaleMethodPreference == (uint)UpscaleMethod::kDLSS)
		return UpscaleMethod::kFSR;

	return (UpscaleMethod)settings.upscaleMethodPreference;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMethodNoMenu = UpscaleMethod::kDisabled;
	auto upscaleMethodNoMenu = GetUpscaleMethod(false);

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMethodNoMenu != upscaleMethodNoMenu) {
		if (previousUpscaleMethodNoMenu == UpscaleMethod::kDisabled)
			CreateUpscalingResources();
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();

		if (upscaleMethodNoMenu == UpscaleMethod::kDisabled)
			DestroyUpscalingResources();
		else if (upscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();

		previousUpscaleMethodNoMenu = upscaleMethodNoMenu;
	}
}

ID3D11ComputeShader* Upscaling::GetRCAS()
{
	float currentSharpness = (-2.0f * settings.sharpness) + 2.0f;
	currentSharpness = exp2(-currentSharpness);

	static auto previousSharpness = currentSharpness;

	if (previousSharpness != currentSharpness) {
		previousSharpness = currentSharpness;
		rcas = nullptr;
	}

	if (!rcas) {
		logger::debug("Compiling RCAS.hlsl");
		rcas.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/RCAS/RCAS.hlsl", { { "SHARPNESS", std::format("{}", currentSharpness).c_str() } }, "cs_5_0"));
	}
	return rcas.get();
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS) {
		logger::debug("Compiling DilateMotionVectorCS.hlsl");
		dilateMotionVectorCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/DilateMotionVectorCS.hlsl", {}, "cs_5_0"));
	}
	return dilateMotionVectorCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS) {
		logger::debug("Compiling OverrideDepthCS.hlsl");
		overrideDepthCS.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideDepthCS.hlsl", {}, "cs_5_0"));
	}
	return overrideDepthCS.get();
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

void Upscaling::UpdateJitter()
{
	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	auto upscaleMethodNoMenu = GetUpscaleMethod(false);
	auto upscaleMethod = GetUpscaleMethod(true);

	float resolutionScale = upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);
	float currentMipBias = std::log2f(resolutionScale) - 1.0f;

	UpdateSamplerStates(currentMipBias);
	UpdateRenderTargets(resolutionScale, resolutionScale);

	if (upscaleMethod == UpscaleMethod::kDisabled)
		resolutionScale = 1.0f;

	renderTargetManager->lowestWidthRatio = renderTargetManager->dynamicWidthRatio;
	renderTargetManager->lowestHeightRatio = renderTargetManager->dynamicHeightRatio;

	if (upscaleMethod != UpscaleMethod::kDisabled) {
		auto screenWidth = gameViewport->screenWidth;
		auto screenHeight = gameViewport->screenHeight;

		auto renderWidth = static_cast<uint>(static_cast<float>(screenWidth) * resolutionScale);

		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);

		ffxFsr3GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);

		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(screenWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(screenHeight);
	}

	renderTargetManager->dynamicWidthRatio = resolutionScale;
	renderTargetManager->dynamicHeightRatio = resolutionScale;
	
	CheckResources();
}

void Upscaling::Upscale()
{
	auto upscaleMethod = GetUpscaleMethod(true);

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Unbind all render targets before we start manipulating textures
	// This ensures we don't have any resource hazards during the copy
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Get the frame buffer (the rendered image at render resolution)
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	// Copy the frame buffer to our intermediate upscaling texture
	// This is the input for DLSS/FSR upscaling
	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	// Release the resource acquired by GetResource (prevents memory leak)
	frameBufferResource->Release();

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	// Calculate the screen dimensions (display resolution) and render dimensions (scaled resolution)
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	// DLSS upscaling path
	if (upscaleMethod == UpscaleMethod::kDLSS){
		// Step 1: Dilate motion vectors for better temporal stability
		// DLSS needs high-quality motion vectors, so we dilate them using a compute shader
		{
			// Get camera near/far plane values from the game engine
#if defined(FALLOUT_POST_NG)
			float cameraNear = *(float*)REL::ID(2712882).address();
			float cameraFar = *(float*)REL::ID(2712883).address();
#else
			float cameraNear = *(float*)REL::ID(57985).address();
			float cameraFar = *(float*)REL::ID(958877).address();
#endif

			// Pack camera data for the compute shader
			float4 cameraData{};
			cameraData.x = cameraFar;
			cameraData.y = cameraNear;
			cameraData.z = cameraFar - cameraNear;    // Range
			cameraData.w = cameraFar * cameraNear;     // Product (used for depth linearization)

			// Prepare constant buffer with screen/render dimensions and camera data
			UpscalingCB upscalingData;
			upscalingData.ScreenSize[0] = static_cast<uint>(screenSize.x);
			upscalingData.ScreenSize[1] = static_cast<uint>(screenSize.y);
			upscalingData.RenderSize[0] = static_cast<uint>(renderSize.x);
			upscalingData.RenderSize[1] = static_cast<uint>(renderSize.y);
			upscalingData.CameraData = cameraData;

			// Update and bind the constant buffer
			auto upscalingCB = GetUpscalingCB();
			upscalingCB->Update(upscalingData);
			auto upscalingBuffer = upscalingCB->CB();
			context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

			// Bind input resources: motion vectors and depth
			auto motionVectorSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMotionVectors].srView);
			auto depthTextureSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth);

			ID3D11ShaderResourceView* views[2] = { motionVectorSRV, depthTextureSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			// Bind output: dilated motion vector texture
			ID3D11UnorderedAccessView* uavs[1] = { dilatedMotionVectorTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			// Run the motion vector dilation compute shader
			context->CSSetShader(GetDilateMotionVectorCS(), nullptr, 0);

			// Dispatch compute shader (8x8 thread groups)
			uint dispatchX = (uint)std::ceil(renderSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(renderSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		// Clean up compute shader bindings to avoid resource hazards
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	// Step 2: Execute the upscaling algorithm
	if (upscaleMethod == UpscaleMethod::kDLSS)
		// DLSS upscaling using NVIDIA Streamline
		Streamline::GetSingleton()->Upscale(upscalingTexture.get(), dilatedMotionVectorTexture.get(), jitter, renderSize, settings.qualityMode);
	else if (upscaleMethod == UpscaleMethod::kFSR)
		// FSR3 upscaling using AMD FidelityFX
		FidelityFX::GetSingleton()->Upscale(upscalingTexture.get(), jitter, renderSize, settings.sharpness);

	// Step 3: Apply sharpening (RCAS) for DLSS and disabled modes
	// FSR has built-in sharpening, so we skip this step for FSR
	if (upscaleMethod != UpscaleMethod::kFSR) {
		// Copy upscaled result back to frame buffer
		context->CopyResource(frameBufferResource, upscalingTexture->resource.get());

		{
			// Apply RCAS (Robust Contrast Adaptive Sharpening)
			{
				ID3D11ShaderResourceView* views[1] = { frameBufferSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCAS(), nullptr, 0);

				// Dispatch RCAS compute shader (8x8 thread groups)
				uint dispatchX = (uint)std::ceil(screenSize.x / 8.0f);
				uint dispatchY = (uint)std::ceil(screenSize.y / 8.0f);
				context->Dispatch(dispatchX, dispatchY, 1);
			}

			// Clean up compute shader bindings
			ID3D11ShaderResourceView* views[1] = { nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11ComputeShader* shader = nullptr;
			context->CSSetShader(shader, nullptr, 0);
		}
	}

	// Copy final upscaled result back to the frame buffer for display
	context->CopyResource(frameBufferResource, upscalingTexture->resource.get());
}

void Upscaling::CreateUpscalingResources()
{
	if (Streamline::GetSingleton()->featureDLSS) {
		auto renderer = RE::BSGraphics::RendererData::GetSingleton();
		auto& main = renderer->renderTargets[(uint)Util::RenderTarget::kMain];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

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
		texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		uavDesc.Format = texDesc.Format;

		dilatedMotionVectorTexture = std::make_unique<Texture2D>(texDesc);
		dilatedMotionVectorTexture->CreateUAV(uavDesc);
	}
}

void Upscaling::DestroyUpscalingResources()
{
	if (Streamline::GetSingleton()->featureDLSS) {
		dilatedMotionVectorTexture = nullptr;
	}
}

void Upscaling::PatchSSRShader()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}