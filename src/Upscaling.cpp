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
	static float previousMipBiasSharper = 1.0f;

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
}

void Upscaling::Upscale()
{
	CheckResources();

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

	auto dlssPreset = (sl::DLSSPreset)settings.dlssPreset;

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
		Streamline::GetSingleton()->Upscale(upscalingTexture, dilatedMotionVectorTexture, jitter, renderSize, settings.qualityMode, dlssPreset);
	else
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

	upscalingDataCB = nullptr;
}