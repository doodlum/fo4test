#include "Upscaling.h"

#include <unordered_set>
#include <SimpleIni.h>

#include "Util.h"

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

void Upscaling::UpdateRenderTarget(int index, uint a_currentWidth, uint a_currentHeight)
{
	auto& originalRenderTarget = originalRenderTargets[index];
	auto& proxyRenderTarget = proxyRenderTargets[index];

	if (proxyRenderTarget.uaView)
		proxyRenderTarget.uaView->Release();
	proxyRenderTarget.uaView = nullptr;

	if (proxyRenderTarget.copySRView)
		proxyRenderTarget.copySRView->Release();
	proxyRenderTarget.copySRView = nullptr;

	if (proxyRenderTarget.srView)
		proxyRenderTarget.srView->Release();
	proxyRenderTarget.srView = nullptr;

	if (proxyRenderTarget.rtView)
		proxyRenderTarget.rtView->Release();
	proxyRenderTarget.rtView = nullptr;

	if (proxyRenderTarget.copyTexture)
		proxyRenderTarget.copyTexture->Release();
	proxyRenderTarget.copyTexture = nullptr;

	if (proxyRenderTarget.texture)
		proxyRenderTarget.texture->Release();
	proxyRenderTarget.texture = nullptr;

	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalRenderTarget.texture)
		originalRenderTarget.texture->GetDesc(&textureDesc);

	D3D11_TEXTURE2D_DESC copyTextureDesc{};
	if (originalRenderTarget.copyTexture)
		originalRenderTarget.copyTexture->GetDesc(&copyTextureDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (originalRenderTarget.rtView)
		originalRenderTarget.rtView->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (originalRenderTarget.srView)
		originalRenderTarget.srView->GetDesc(&srViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC copySRViewDesc{};
	if (originalRenderTarget.copySRView)
		originalRenderTarget.copySRView->GetDesc(&copySRViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc;
	if (originalRenderTarget.uaView)
		originalRenderTarget.uaView->GetDesc(&uaViewDesc);

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	textureDesc.Width = a_currentWidth;
	textureDesc.Height = a_currentHeight;

	copyTextureDesc.Width = a_currentWidth;
	copyTextureDesc.Height = a_currentHeight;

	if (originalRenderTarget.texture)
		device->CreateTexture2D(&textureDesc, nullptr, &proxyRenderTarget.texture);

	if (originalRenderTarget.copyTexture)
		device->CreateTexture2D(&copyTextureDesc, nullptr, &proxyRenderTarget.copyTexture);
	
	if (originalRenderTarget.rtView)
		device->CreateRenderTargetView(proxyRenderTarget.texture, &rtViewDesc, &proxyRenderTarget.rtView);

	if (originalRenderTarget.srView)
		device->CreateShaderResourceView(proxyRenderTarget.texture, &srViewDesc, &proxyRenderTarget.srView);

	if (originalRenderTarget.copySRView)
		device->CreateShaderResourceView(proxyRenderTarget.copyTexture, &copySRViewDesc, &proxyRenderTarget.copySRView);

	if (originalRenderTarget.uaView)
		device->CreateUnorderedAccessView(proxyRenderTarget.texture, &uaViewDesc, &proxyRenderTarget.uaView);
}

void Upscaling::UpdateRenderTargets(uint a_currentWidth, uint a_currentHeight)
{
	// 20 57 24 23 58 59 2 25 3 9 39
	// 4
	// 22 
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	// Store original render targets
	static std::once_flag setup;
	std::call_once(setup, [&]() {

		for (int i = 0; i < 101; i++) {
			originalRenderTargets[i] = rendererData->renderTargets[i];
		}

		for (int i = 0; i < 13; i++) {
			originalDepthStencilTargets[i] = rendererData->depthStencilTargets[i];
		}

	});

	static uint previousWidth = 0;
	static uint previousHeight = 0;

	// Check for resolution update
	if (previousWidth == a_currentWidth && previousHeight == a_currentHeight)
		return;

	previousWidth = a_currentWidth;
	previousHeight = a_currentHeight;

	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		UpdateRenderTarget(renderTargetsPatch[i], a_currentWidth, a_currentHeight);

	//UpdateRenderTarget(39, a_currentWidth, a_currentHeight);

	for (int i = 0; i < ARRAYSIZE(depthStencilTargetPatch); i++)
		UpdateDepthStencilRenderTarget(depthStencilTargetPatch[i], a_currentWidth, a_currentHeight);
}

void Upscaling::OverrideRenderTarget(int index)
{
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
	
	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	renderTargetManager->renderTargetData[index].width = dstDesc.Width;
	renderTargetManager->renderTargetData[index].height = dstDesc.Height;
}

void Upscaling::UpdateDepthStencilRenderTarget(int index, uint a_currentWidth, uint a_currentHeight)
{
	auto& originalDepthTarget = originalDepthStencilTargets[index];
	auto& proxyDepthTarget = proxyDepthStencilTargets[index];

	// Release existing resources
	if (proxyDepthTarget.srViewStencil) {
		proxyDepthTarget.srViewStencil->Release();
		proxyDepthTarget.srViewStencil = nullptr;
	}
	if (proxyDepthTarget.srViewDepth) {
		proxyDepthTarget.srViewDepth->Release();
		proxyDepthTarget.srViewDepth = nullptr;
	}
	for (int i = 0; i < 4; ++i) {
		if (proxyDepthTarget.dsViewReadOnlyDepthStencil[i]) {
			proxyDepthTarget.dsViewReadOnlyDepthStencil[i]->Release();
			proxyDepthTarget.dsViewReadOnlyDepthStencil[i] = nullptr;
		}
		if (proxyDepthTarget.dsViewReadOnlyStencil[i]) {
			proxyDepthTarget.dsViewReadOnlyStencil[i]->Release();
			proxyDepthTarget.dsViewReadOnlyStencil[i] = nullptr;
		}
		if (proxyDepthTarget.dsViewReadOnlyDepth[i]) {
			proxyDepthTarget.dsViewReadOnlyDepth[i]->Release();
			proxyDepthTarget.dsViewReadOnlyDepth[i] = nullptr;
		}
		if (proxyDepthTarget.dsView[i]) {
			proxyDepthTarget.dsView[i]->Release();
			proxyDepthTarget.dsView[i] = nullptr;
		}
	}
	if (proxyDepthTarget.texture) {
		proxyDepthTarget.texture->Release();
		proxyDepthTarget.texture = nullptr;
	}

	// Get original descriptions
	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalDepthTarget.texture)
		originalDepthTarget.texture->GetDesc(&textureDesc);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsViewDesc[4]{};
	D3D11_DEPTH_STENCIL_VIEW_DESC dsViewReadOnlyDepthDesc[4]{};
	D3D11_DEPTH_STENCIL_VIEW_DESC dsViewReadOnlyStencilDesc[4]{};
	D3D11_DEPTH_STENCIL_VIEW_DESC dsViewReadOnlyDepthStencilDesc[4]{};

	for (int i = 0; i < 4; ++i) {
		if (originalDepthTarget.dsView[i])
			originalDepthTarget.dsView[i]->GetDesc(&dsViewDesc[i]);
		if (originalDepthTarget.dsViewReadOnlyDepth[i])
			originalDepthTarget.dsViewReadOnlyDepth[i]->GetDesc(&dsViewReadOnlyDepthDesc[i]);
		if (originalDepthTarget.dsViewReadOnlyStencil[i])
			originalDepthTarget.dsViewReadOnlyStencil[i]->GetDesc(&dsViewReadOnlyStencilDesc[i]);
		if (originalDepthTarget.dsViewReadOnlyDepthStencil[i])
			originalDepthTarget.dsViewReadOnlyDepthStencil[i]->GetDesc(&dsViewReadOnlyDepthStencilDesc[i]);
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDepthDesc{};
	if (originalDepthTarget.srViewDepth)
		originalDepthTarget.srViewDepth->GetDesc(&srViewDepthDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewStencilDesc{};
	if (originalDepthTarget.srViewStencil)
		originalDepthTarget.srViewStencil->GetDesc(&srViewStencilDesc);

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Update dimensions
	textureDesc.Width = a_currentWidth;
	textureDesc.Height = a_currentHeight;

	// Create new texture
	if (originalDepthTarget.texture)
		device->CreateTexture2D(&textureDesc, nullptr, &proxyDepthTarget.texture);

	// Create depth stencil views
	for (int i = 0; i < 4; ++i) {
		if (originalDepthTarget.dsView[i])
			device->CreateDepthStencilView(proxyDepthTarget.texture, &dsViewDesc[i], &proxyDepthTarget.dsView[i]);
		if (originalDepthTarget.dsViewReadOnlyDepth[i])
			device->CreateDepthStencilView(proxyDepthTarget.texture, &dsViewReadOnlyDepthDesc[i], &proxyDepthTarget.dsViewReadOnlyDepth[i]);
		if (originalDepthTarget.dsViewReadOnlyStencil[i])
			device->CreateDepthStencilView(proxyDepthTarget.texture, &dsViewReadOnlyStencilDesc[i], &proxyDepthTarget.dsViewReadOnlyStencil[i]);
		if (originalDepthTarget.dsViewReadOnlyDepthStencil[i])
			device->CreateDepthStencilView(proxyDepthTarget.texture, &dsViewReadOnlyDepthStencilDesc[i], &proxyDepthTarget.dsViewReadOnlyDepthStencil[i]);
	}

	// Create shader resource views
	if (originalDepthTarget.srViewDepth)
		device->CreateShaderResourceView(proxyDepthTarget.texture, &srViewDepthDesc, &proxyDepthTarget.srViewDepth);
	if (originalDepthTarget.srViewStencil)
		device->CreateShaderResourceView(proxyDepthTarget.texture, &srViewStencilDesc, &proxyDepthTarget.srViewStencil);
}

void Upscaling::OverrideDepthStencilRenderTarget(int index)
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->depthStencilTargets[index] = proxyDepthStencilTargets[index];

	D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
	originalDepthStencilTargets[index].texture->GetDesc(&srcDesc);
	proxyDepthStencilTargets[index].texture->GetDesc(&dstDesc);

	D3D11_BOX srcBox;
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = dstDesc.Width;
	srcBox.bottom = dstDesc.Height;
	srcBox.back = 1;

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->CopySubresourceRegion(proxyDepthStencilTargets[index].texture, 0, 0, 0, 0, originalDepthStencilTargets[index].texture, 0, &srcBox);

	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	renderTargetManager->depthStencilTargetData[index].width = dstDesc.Width;
	renderTargetManager->depthStencilTargetData[index].height = dstDesc.Height;
}

void Upscaling::ResetDepthStencilRenderTarget(int index)
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	rendererData->depthStencilTargets[index] = originalDepthStencilTargets[index];

	D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
	proxyDepthStencilTargets[index].texture->GetDesc(&srcDesc);
	originalDepthStencilTargets[index].texture->GetDesc(&dstDesc);

	D3D11_BOX srcBox;
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = srcDesc.Width;
	srcBox.bottom = srcDesc.Height;
	srcBox.back = 1;

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->CopySubresourceRegion(originalDepthStencilTargets[index].texture, 0, 0, 0, 0, proxyDepthStencilTargets[index].texture, 0, &srcBox);

	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	renderTargetManager->depthStencilTargetData[index].width = dstDesc.Width;
	renderTargetManager->depthStencilTargetData[index].height = dstDesc.Height;
}

void Upscaling::OverrideRenderTargets()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	
	static auto gameViewport = State_GetSingleton();
	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	UpdateRenderTargets((uint)renderSize.x, (uint)renderSize.y);

	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		OverrideRenderTarget(renderTargetsPatch[i]);

	for (int i = 0; i < ARRAYSIZE(depthStencilTargetPatch); i++)
		OverrideDepthStencilRenderTarget(depthStencilTargetPatch[i]);

	for (int i = 0; i < 100; i++) {
		originalRenderTargetData[i] = renderTargetManager->renderTargetData[i];
		renderTargetManager->renderTargetData[i].width = (uint)renderSize.x;
		renderTargetManager->renderTargetData[i].height = (uint)renderSize.y;
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);
}

void Upscaling::ResetRenderTargets()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	for(int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		ResetRenderTarget(renderTargetsPatch[i]);

	//ResetRenderTarget(39);

	for (int i = 0; i < ARRAYSIZE(depthStencilTargetPatch); i++)
		ResetDepthStencilRenderTarget(depthStencilTargetPatch[i]);

	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	for (int i = 0; i < 100; i++) {
		renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, true);
}

// Hacky method of overriding sampler states
void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static auto samplerStates = SamplerStates::GetSingleton();
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Store original sampler states
	static std::once_flag setup;
	std::call_once(setup, [&]() {
		for (int a = 0; a < 320; a++) {
			originalSamplerStates[a] = samplerStates->a[a];
		}
	});

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
	static auto previousUpscaleMode = UpscaleMethod::kDisabled;
	auto currentUpscaleMode = GetUpscaleMethod(false);

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMethod::kDisabled)
			CreateUpscalingResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (previousUpscaleMode == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();

		if (currentUpscaleMode == UpscaleMethod::kDisabled)
			DestroyUpscalingResources();
		else if (currentUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();

		previousUpscaleMode = currentUpscaleMode;
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

ID3D11ComputeShader* Upscaling::GetGenerateReactiveMaskCS()
{
	if (!generateReactiveMaskCS) {
		logger::debug("Compiling GenerateReactiveMaskCS.hlsl");
		generateReactiveMaskCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/GenerateReactiveMaskCS.hlsl", { }, "cs_5_0");
	}
	return generateReactiveMaskCS;
}

void Upscaling::GenerateReactiveMask()
{
	auto upscaleMethod = GetUpscaleMethod(true);

	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	static auto gameViewport = State_GetSingleton();
	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	auto mainPreAlphaSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainPreAlpha].srView);
	auto mainPostAlpha = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainTemp].srView);

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

		UpscalingDataCB upscalingData;
		upscalingData.trueSamplingDim = renderSize;
		upscalingData.cameraData = cameraData;

		upscalingDataCB->Update(upscalingData);

		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		{
			ID3D11ShaderResourceView* views[] = { mainPreAlphaSRV, mainPostAlpha };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[] = { reactiveMaskTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetGenerateReactiveMaskCS(), nullptr, 0);

			uint dispatchX = (uint)std::ceil(renderSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(renderSize.y / 8.0f);
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

void Upscaling::UpdateJitter()
{
	static auto gameViewport = State_GetSingleton();
	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	auto upscaleMethod = GetUpscaleMethod(false);
	auto upscaleMethodMenu = GetUpscaleMethod(true);

	float resolutionScaleBase = upscaleMethod == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);
	
	renderTargetManager->lowestWidthRatio = renderTargetManager->dynamicWidthRatio;
	renderTargetManager->lowestHeightRatio = renderTargetManager->dynamicHeightRatio;

	if (upscaleMethodMenu != UpscaleMethod::kDisabled) {
		auto screenWidth = gameViewport->screenWidth;
		auto screenHeight = gameViewport->screenHeight;
		auto renderWidth = static_cast<uint>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<uint>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);

		ffxFsr3GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);

		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(screenWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(screenHeight);
	} else {
		resolutionScale = { 1.0f, 1.0f };
	}

	renderTargetManager->dynamicWidthRatio = resolutionScale.x;
	renderTargetManager->dynamicHeightRatio = resolutionScale.y;
		
	float currentMipBias = std::log2f(resolutionScaleBase);
	
	if (upscaleMethod == UpscaleMethod::kDLSS)
		currentMipBias -= 1.0f;

	UpdateSamplerStates(currentMipBias);

	CheckResources();
}

void Upscaling::Upscale()
{
	auto upscaleMethod = GetUpscaleMethod(true);

	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	static auto gameViewport = State_GetSingleton();
	static auto renderTargetManager = RenderTargetManager_GetSingleton();
	
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

	{
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

			UpscalingDataCB upscalingData;
			upscalingData.trueSamplingDim = renderSize;
			upscalingData.cameraData = cameraData;

			upscalingDataCB->Update(upscalingData);

			auto upscalingBuffer = upscalingDataCB->CB();
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
	else
		FidelityFX::GetSingleton()->Upscale(upscalingTexture, reactiveMaskTexture, jitter, renderSize, settings.sharpness);

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
		.Texture2D = { .MipSlice = 0 }
	};

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	upscalingTexture = new Texture2D(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	dilatedMotionVectorTexture = new Texture2D(texDesc);
	dilatedMotionVectorTexture->CreateSRV(srvDesc);
	dilatedMotionVectorTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R16_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	reactiveMaskTexture = new Texture2D(texDesc);
	reactiveMaskTexture->CreateSRV(srvDesc);
	reactiveMaskTexture->CreateUAV(uavDesc);

	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTexture->srv = nullptr;
	upscalingTexture->uav = nullptr;
	upscalingTexture->resource = nullptr;
	delete upscalingTexture;

	dilatedMotionVectorTexture->srv = nullptr;
	dilatedMotionVectorTexture->uav = nullptr;
	dilatedMotionVectorTexture->resource = nullptr;
	delete dilatedMotionVectorTexture;

	reactiveMaskTexture->srv = nullptr;
	reactiveMaskTexture->uav = nullptr;
	reactiveMaskTexture->resource = nullptr;
	delete reactiveMaskTexture;

	upscalingDataCB = nullptr;

	generateReactiveMaskCS = nullptr;
}