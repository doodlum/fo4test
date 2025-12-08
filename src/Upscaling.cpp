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

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod()
{
	auto streamline = Streamline::GetSingleton();

	// If DLSS is not available, default to FSR
	if (!streamline->featureDLSS && settings.upscaleMethodPreference == (uint)UpscaleMethod::kDLSS)
		return UpscaleMethod::kFSR;

	return (UpscaleMethod)settings.upscaleMethodPreference;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMethod::kDisabled;
	auto currentUpscaleMode = GetUpscaleMethod();

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

ID3D11VertexShader* Upscaling::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS = (ID3D11VertexShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/UpscaleVS.hlsl", { }, "vs_5_0");
	}
	return upscaleVS;
}

ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		depthRefractionUpscalePS = (ID3D11PixelShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/DepthRefractionUpscalePS.hlsl", { }, "ps_5_0");
	}
	return depthRefractionUpscalePS;
}

void Upscaling::UpdateJitter()
{
	static auto gameViewport = State_GetSingleton();
	static auto renderTargetManager = RenderTargetManager_GetSingleton();

	auto upscaleMethod = GetUpscaleMethod();

	float resolutionScaleBase = upscaleMethod == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);

	auto screenWidth = gameViewport->screenWidth;
	auto screenHeight = gameViewport->screenHeight;
	auto renderWidth = static_cast<uint>(screenWidth * resolutionScaleBase);
	auto renderHeight = static_cast<uint>(screenHeight * resolutionScaleBase);

	resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
	resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);
	
	if (upscaleMethod != UpscaleMethod::kDisabled) {
		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);

		ffxFsr3GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);

		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(screenWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(screenHeight);
	}
	
	renderTargetManager->lowestWidthRatio = renderTargetManager->dynamicWidthRatio;
	renderTargetManager->lowestHeightRatio = renderTargetManager->dynamicHeightRatio;
	renderTargetManager->dynamicWidthRatio = resolutionScale.x;
	renderTargetManager->dynamicHeightRatio = resolutionScale.y;
	
	float currentMipBias = std::log2f(static_cast<float>(renderWidth) / static_cast<float>(screenWidth));
	
	if (upscaleMethod == UpscaleMethod::kDLSS)
		currentMipBias -= 1.0f;

	UpdateSamplerStates(currentMipBias);
}

void Upscaling::Upscale()
{
	CheckResources();

	auto upscaleMethod = GetUpscaleMethod();

	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	context->OMSetRenderTargets(0, nullptr, nullptr);

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
		Streamline::GetSingleton()->Upscale(dilatedMotionVectorTexture, jitter, renderSize, settings.qualityMode, dlssPreset);
	else
		FidelityFX::GetSingleton()->Upscale(jitter, renderSize, settings.sharpness);

	if (upscaleMethod != UpscaleMethod::kFSR) {
		
		auto& mainTexture = rendererData->renderTargets[(uint)Util::RenderTarget::kMain];
		auto& mainTempTexture = rendererData->renderTargets[(uint)Util::RenderTarget::kMainTemp];

		context->CopyResource(mainTexture.texture, mainTempTexture.texture);

		{
			{
				ID3D11ShaderResourceView* views[1] = { mainTexture.srView };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { mainTempTexture.uaView };
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

	UpscaleDepth();
}

void Upscaling::UpscaleDepth()
{
	if (resolutionScale.x != 1.0f || resolutionScale.y != 1.0f) {
		static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		
		static auto gameViewport = State_GetSingleton();
		static auto renderTargetManager = RenderTargetManager_GetSingleton();

		// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader that generates fullscreen triangle using SV_VertexID
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up viewport for fullscreen rendering
		auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
		auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set rasterizer state
		context->RSSetState(upscaleRasterizerState);

		// Set blend state
		context->OMSetBlendState(upscaleBlendState, nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11SamplerState* samplers[] = { linearSampler };
		context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		// Set up constant buffer for upscaling
		UpscaleCB upscaleData;
		upscaleData.Jitter = jitter;
		upscaleData.ResolutionScale = resolutionScale;
		upscaleData.BufferDim = float4(screenSize.x, screenSize.y, 1.0f / screenSize.x, 1.0f / screenSize.y);
		jitterCB->Update(upscaleData);

		auto buffer = jitterCB->CB();
		context->PSSetConstantBuffers(0, 1, &buffer);

		// black = underwater
		
		//auto& depth = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];
		//auto& depthCopy = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMainCopy];
		//auto& refractionNormal = rendererData->renderTargets[(uint)Util::RenderTarget::kRefractionNormal];

		//context->CopyResource(depthCopy.texture, depth.texture);

		//ID3D11ShaderResourceView* srvs[] = { refractionNormal.srView, depthCopy.srViewDepth };
		//context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		//context->OMSetRenderTargets(0, nullptr, depth.dsView[0]);

		//context->PSSetShader(GetDepthRefractionUpscalePS(), nullptr, 0);
		//context->Draw(3, 0);
		//
		//// Unbind resources
		//ID3D11SamplerState* nullSamplers[] = { nullptr };
		//context->PSSetSamplers(0, ARRAYSIZE(nullSamplers), nullSamplers);

		//ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
		//context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

		//context->OMSetRenderTargets(0, nullptr, nullptr);

	}
}

void Upscaling::CreateUpscalingResources()
{
	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto& main = rendererData->renderTargets[(uint)Util::RenderTarget::kMain];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

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
	texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	dilatedMotionVectorTexture = new Texture2D(texDesc);
	dilatedMotionVectorTexture->CreateSRV(srvDesc);
	dilatedMotionVectorTexture->CreateUAV(uavDesc);

	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());
	
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Create depth stencil states for depth upscaling
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)
	depthStencilDesc.StencilEnable = false;						   // Disable stencil testing

	DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &upscaleDepthStencilState));

	depthStencilDesc.StencilEnable = true;     // Enable stencil testing
	depthStencilDesc.StencilReadMask = 0xFF;   // Read all stencil bits
	depthStencilDesc.StencilWriteMask = 0xFF;  // Write to all stencil bits

	// Configure front-facing stencil operations
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;       // Replace on stencil fail
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;  // Replace on depth fail
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;    // Replace on pass
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;       // Always pass stencil test

	// Configure back-facing stencil operations (same as front)
	depthStencilDesc.BackFace.StencilFailOp = depthStencilDesc.FrontFace.StencilFailOp;
	depthStencilDesc.BackFace.StencilDepthFailOp = depthStencilDesc.FrontFace.StencilDepthFailOp;
	depthStencilDesc.BackFace.StencilPassOp = depthStencilDesc.FrontFace.StencilPassOp;
	depthStencilDesc.BackFace.StencilFunc = depthStencilDesc.FrontFace.StencilFunc;

	DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &upscaleDepthStencilStateWithStencil));

	// Create blend state for depth upscaling
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &upscaleBlendState));

	// Create rasterizer state for fullscreen rendering
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, &upscaleRasterizerState));

	// Create jitter offset constant buffer for depth upscaling
	jitterCB = new ConstantBuffer(ConstantBufferDesc<UpscaleCB>());

	// Create linear and point sampler
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));

	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointSampler));
}

void Upscaling::DestroyUpscalingResources()
{
	dilatedMotionVectorTexture->srv = nullptr;
	dilatedMotionVectorTexture->uav = nullptr;
	dilatedMotionVectorTexture->resource = nullptr;
	delete dilatedMotionVectorTexture;

	upscalingDataCB = nullptr;
}