#include "Upscaling.h"

#include "Util.h"

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod()
{
	auto streamline = Streamline::GetSingleton();
	return streamline->featureDLSS ? (UpscaleMethod)settings.upscaleMethod : (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	auto currentUpscaleMode = GetUpscaleMethod();

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMethod::kTAA)
			CreateUpscalingResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (previousUpscaleMode == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();

		if (currentUpscaleMode == UpscaleMethod::kTAA)
			DestroyUpscalingResources();
		else if (currentUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();

		previousUpscaleMode = currentUpscaleMode;
	}
}

ID3D11ComputeShader* Upscaling::GetRCASComputeShader()
{
	static auto previousSharpness = settings.sharpness;
	float currentSharpness = (-2.0f * settings.sharpness) + 2.0f;

	if (previousSharpness != currentSharpness) {
		previousSharpness = currentSharpness;

		if (rcasCS) {
			rcasCS->Release();
			rcasCS = nullptr;
		}
	}

	if (!rcasCS) {
		logger::debug("Compiling RCAS.hlsl");
		rcasCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/F4SE/Plugins/Upscaling/RCAS/RCAS.hlsl", { { "SHARPNESS", std::format("{}", currentSharpness).c_str() } }, "cs_5_0");
	}
	return rcasCS;
}

void Upscaling::UpdateJitter()
{
	auto upscaleMethod = GetUpscaleMethod();
	if (upscaleMethod != UpscaleMethod::kTAA) {
		static auto gameViewport = RE::BSGraphics::State::GetSingleton();

		ffxFsr3UpscalerGetJitterOffset(&jitter.x, &jitter.y, gameViewport.frameCount, 8);

		gameViewport.offsetX = -2.0f * jitter.x / (float)gameViewport.screenWidth;
		gameViewport.offsetY = 2.0f * jitter.y / (float)gameViewport.screenHeight;
	}
}

void Upscaling::Upscale()
{
	CheckResources();

	static auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)Util::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	auto upscaleMethod = GetUpscaleMethod();
	auto dlssPreset = (sl::DLSSPreset)settings.dlssPreset;

	if (upscaleMethod == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTexture, jitter, dlssPreset);
	else
		FidelityFX::GetSingleton()->Upscale(upscalingTexture, jitter, settings.sharpness);

	if (upscaleMethod != UpscaleMethod::kFSR && settings.sharpness > 0.0f) {
		context->CopyResource(frameBufferResource, upscalingTexture->resource.get());

		static auto gameViewport = RE::BSGraphics::State::GetSingleton();
		uint dispatchX = (uint)std::ceil((float)gameViewport.screenWidth / 8.0f);
		uint dispatchY = (uint)std::ceil((float)gameViewport.screenHeight / 8.0f);

		{
			{
				ID3D11ShaderResourceView* views[1] = { frameBufferSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCASComputeShader(), nullptr, 0);

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
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTexture->srv = nullptr;
	upscalingTexture->uav = nullptr;
	upscalingTexture->resource = nullptr;
	delete upscalingTexture;
}
