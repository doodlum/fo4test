#include "Upscaling.h"

#include <unordered_set>
#include <SimpleIni.h>

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
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	originalRenderTargets[index] = rendererData->renderTargets[index];

	auto& originalRenderTarget = originalRenderTargets[index];
	auto& proxyRenderTarget = proxyRenderTargets[index];

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

	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalRenderTarget.texture)
		originalRenderTarget.texture->GetDesc(&textureDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (originalRenderTarget.rtView)
		originalRenderTarget.rtView->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (originalRenderTarget.srView)
		originalRenderTarget.srView->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc;
	if (originalRenderTarget.uaView)
		originalRenderTarget.uaView->GetDesc(&uaViewDesc);

	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);
	
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	
	if (originalRenderTarget.texture)
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &proxyRenderTarget.texture));
	
	if (auto texture = proxyRenderTarget.texture) {
		if (originalRenderTarget.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, &proxyRenderTarget.rtView));

		if (originalRenderTarget.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, &proxyRenderTarget.srView));

		if (originalRenderTarget.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, &proxyRenderTarget.uaView));
	}

#ifndef NDEBUG
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

	if (upscalingTexture) {
		upscalingTexture->uav = nullptr;
		upscalingTexture->srv = nullptr;
		upscalingTexture->resource = nullptr;
		upscalingTexture = nullptr;
	}

	if (depthOverrideTexture){
		depthOverrideTexture->uav = nullptr;
		depthOverrideTexture->srv = nullptr;
		depthOverrideTexture->resource = nullptr;
		depthOverrideTexture = nullptr;
	}

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);

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

	upscalingTexture = new Texture2D(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	depthOverrideTexture = new Texture2D(texDesc);
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

	originalDepthView = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth;

	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth = depthOverrideTexture->srv.get();
}

void Upscaling::ResetDepth()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain].srViewDepth = originalDepthView;
}

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static auto samplerStates = SamplerStates::GetSingleton();
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Store original sampler states
	for (int a = 0; a < 320; a++) {
		originalSamplerStates[a] = samplerStates->a[a];
	}

	static float previousMipBias = 1.0f;

	// Check for mipbias update
	if (previousMipBias == a_currentMipBias)
		return;

	previousMipBias = a_currentMipBias;

	for (int a = 0; a < 320; a++) {
		// Delete any existing biased sampler state
		if (biasedSamplerStates[a]){
			biasedSamplerStates[a]->Release();
			biasedSamplerStates[a] = nullptr;
		}
		
		// Replace sampler state with biased version
		if (auto samplerState = originalSamplerStates[a]) {
			D3D11_SAMPLER_DESC samplerDesc;
			samplerState->GetDesc(&samplerDesc);

			// Apply mip bias
			if (samplerDesc.Filter == D3D11_FILTER_ANISOTROPIC && samplerDesc.MaxAnisotropy == 16) {
				samplerDesc.MaxAnisotropy = 8;
				samplerDesc.MipLODBias = a_currentMipBias;
			}

			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &biasedSamplerStates[a]));

		} else {
			biasedSamplerStates[a] = nullptr;
		}

		samplerStates->a[a] = biasedSamplerStates[a];
	}
}

void Upscaling::OverrideSamplerStates()
{
	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = biasedSamplerStates[a];
}

void Upscaling::ResetSamplerStates()
{
	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = originalSamplerStates[a];
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

		if (rcas) {
			rcas->Release();
			rcas = nullptr;
		}
	}

	if (!rcas) {
		logger::debug("Compiling RCAS.hlsl");
		rcas = (ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/RCAS/RCAS.hlsl", { { "SHARPNESS", std::format("{}", currentSharpness).c_str() } }, "cs_5_0");
	}
	return rcas;
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS) {
		logger::debug("Compiling DilateMotionVectorCS.hlsl");
		dilateMotionVectorCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/DilateMotionVectorCS.hlsl", { }, "cs_5_0");
	}
	return dilateMotionVectorCS;
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS) {
		logger::debug("Compiling OverrideDepthCS.hlsl");
		overrideDepthCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideDepthCS.hlsl", { }, "cs_5_0");
	}
	return overrideDepthCS;
}

ID3D11PixelShader* Upscaling::GetBSImagespaceShaderSSLRRaytracing()
{
	if (!BSImagespaceShaderSSLRRaytracing) {
		logger::debug("Compiling BSImagespaceShaderSSLRRaytracing.hlsl");
		BSImagespaceShaderSSLRRaytracing = (ID3D11PixelShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/BSImagespaceShaderSSLRRaytracing.hlsl", { }, "ps_5_0");
	}
	return BSImagespaceShaderSSLRRaytracing;
}

ConstantBuffer* Upscaling::GetUpscalingCB()
{
	static ConstantBuffer* upscalingCB = nullptr;

	if (!upscalingCB) {
		logger::debug("Creating UpscalingCB");
		upscalingCB = new ConstantBuffer(ConstantBufferDesc<UpscalingCB>());
	}
	return upscalingCB;
}

void Upscaling::UpdateJitter()
{
	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	auto upscaleMethodNoMenu = GetUpscaleMethod(false);
	auto upscaleMethod = GetUpscaleMethod(true);

	float resolutionScale = upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);
	float currentMipBias = std::log2f(resolutionScale);

	if (upscaleMethodNoMenu == UpscaleMethod::kDLSS)
		currentMipBias -= 1.0f;

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

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();
	
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	if (upscaleMethod == UpscaleMethod::kDLSS){
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

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	if (upscaleMethod == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTexture, dilatedMotionVectorTexture, jitter, renderSize, settings.qualityMode);
	else if (upscaleMethod == UpscaleMethod::kFSR)
		FidelityFX::GetSingleton()->Upscale(upscalingTexture, jitter, renderSize, settings.sharpness);

	if (upscaleMethod != UpscaleMethod::kFSR) {
		context->CopyResource(frameBufferResource, upscalingTexture->resource.get());

		{
			{
				ID3D11ShaderResourceView* views[1] = { frameBufferSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCAS(), nullptr, 0);

				uint dispatchX = (uint)std::ceil(screenSize.x / 8.0f);
				uint dispatchY = (uint)std::ceil(screenSize.y / 8.0f);
				context->Dispatch(dispatchX, dispatchY, 1);
			}

			ID3D11ShaderResourceView* views[1] = { nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11ComputeShader* shader = nullptr;
			context->CSSetShader(shader, nullptr, 0);
		}
	}

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

		dilatedMotionVectorTexture = new Texture2D(texDesc);
		dilatedMotionVectorTexture->CreateUAV(uavDesc);
	}
}

void Upscaling::DestroyUpscalingResources()
{
	if (Streamline::GetSingleton()->featureDLSS) {
		dilatedMotionVectorTexture->uav = nullptr;
		dilatedMotionVectorTexture->resource = nullptr;
		delete dilatedMotionVectorTexture;
	}
}

void Upscaling::PatchSSRShader()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}