#include "Upscaler.h"

#include "RE/CameraData.h"
#include "RE/SingletonAccessors.h"
#include <RE/FO4Runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"

extern bool enbLoaded;

namespace
{
	const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61, 4, 29, 1, 36, 37, 22, 10, 11, 7, 8, 64, 14, 16 };

	enum class RenderTarget
	{
		kFrameBuffer = 0,
		kRefractionNormal = 1,
		kMain = 3,
		kMainTemp = 4,
		kSSRRaw = 7,
		kSSRBlurred = 8,
		kSSRBlurredExtra = 9,
		kSSRDirection = 10,
		kSSRMask = 11,
		kMainVerticalBlur = 14,
		kMotionVectors = 29,
		kUnkMask = 57,
		kDownscaledHDR = 64
	};

	enum class DepthStencilTarget
	{
		kMain = 2
	};

	float GetUpscaleRatio(uint qualityMode)
	{
		switch (qualityMode) {
		case 1:
			return 1.5f;
		case 2:
			return 1.7f;
		case 3:
			return 2.0f;
		case 4:
			return 3.0f;
		default:
			return 1.0f;
		}
	}

	uint32_t ScaleRenderExtent(uint32_t displayExtent, float ratio)
	{
		return std::max(1u, static_cast<uint32_t>(static_cast<float>(displayExtent) * ratio));
	}

	void GetJitterOffset(float* outX, float* outY, uint frameIndex, int phaseCount)
	{
		const auto halton = [](uint index, uint base) {
			float result = 0.0f;
			float fraction = 1.0f / static_cast<float>(base);
			while (index > 0) {
				result += static_cast<float>(index % base) * fraction;
				index /= base;
				fraction /= static_cast<float>(base);
			}
			return result;
		};

		const uint index = (frameIndex % static_cast<uint>(std::max(1, phaseCount))) + 1;
		*outX = halton(index, 2) - 0.5f;
		*outY = halton(index, 3) - 0.5f;
	}

	ID3D11DeviceChild* CompileShaderAny(const wchar_t* filePath, const char* programType, const char* program = "main")
	{
		auto rendererData = fo4cs::GetRendererData();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> shaderErrors;

		if (!std::filesystem::exists(filePath)) {
			logger::error("[Upscaler] Failed to compile shader; file does not exist");
			return nullptr;
		}

		const auto hr = D3DCompileFromFile(filePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, program, programType, flags, 0, shaderBlob.put(), shaderErrors.put());
		if (FAILED(hr)) {
			logger::warn("[Upscaler] Shader compilation failed: {}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}

		if (std::string_view(programType).starts_with("cs")) {
			ID3D11ComputeShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}
		if (std::string_view(programType).starts_with("vs")) {
			ID3D11VertexShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}
		if (std::string_view(programType).starts_with("ps")) {
			ID3D11PixelShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}

		return nullptr;
	}

	void CopyNativeAABorder(ID3D11DeviceContext* context, ID3D11Resource* source, ID3D11Resource* destination, UINT width, UINT height)
	{
		if (!context || !source || !destination)
			return;

		if (width == 0 || height == 0)
			return;

		constexpr UINT kBorderPixels = 12;
		const auto border = std::min({ kBorderPixels, width / 3, height / 3 });
		if (border == 0)
			return;

		auto copyBox = [&](UINT left, UINT top, UINT right, UINT bottom) {
			D3D11_BOX box{ left, top, 0, right, bottom, 1 };
			context->CopySubresourceRegion(destination, 0, left, top, 0, source, 0, &box);
		};

		// Native AA is 1:1. Restore the final frame edges from the pre-upscale
		// color after the upscaler output copy so clamped output texels cannot
		// stretch into a visible edge strip.
		copyBox(0, 0, border, height);
		copyBox(width - border, 0, width, height);
		copyBox(0, 0, width, border);
		copyBox(0, height - border, width, height);
	}

	bool IsLoadingMenuOpen()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			return ui->GetMenuOpen("LoadingMenu");
		}
		return false;
	}

	void TraceRenderBackendStage(std::string_view stage)
	{
		fo4cs::Diagnostics::WriteHangTraceLine(stage);

		auto upscaling = Upscaling::GetSingleton();
		if (!upscaling->debugTraceCurrentPresent) {
			return;
		}

		static std::string previousStage;
		if (previousStage == stage) {
			return;
		}

		previousStage = stage;
		logger::debug("[Upscaler] Render backend stage: {}", stage);
	}

	void RestoreNativeRenderState(RE::BSGraphics::RenderTargetManager* a_renderTargetManager, RE::BSGraphics::State* a_gameViewport)
	{
		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:begin");
		auto* upscaling = Upscaling::GetSingleton();
		upscaling->upscaleMethodNoMenu = Upscaling::UpscaleMethod::kDisabled;
		upscaling->upscaleMethod = Upscaling::UpscaleMethod::kDisabled;
		upscaling->postLoadingSkipUpscale = true;

		if (a_gameViewport) {
			fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:viewport-reset:begin");
			a_gameViewport->offsetX = 0.0f;
			a_gameViewport->offsetY = 0.0f;
			fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:viewport-reset:end");
		}

		if (a_renderTargetManager) {
			fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:rtm-reset:begin");
			a_renderTargetManager->dynamicWidthRatio = 1.0f;
			a_renderTargetManager->dynamicHeightRatio = 1.0f;
			a_renderTargetManager->isDynamicResolutionCurrentlyActivated = false;
			fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:rtm-reset:end");
		}

		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateSamplerStates:begin");
		upscaling->UpdateSamplerStates(0.0f);
		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateSamplerStates:end");
		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateRenderTargets:begin");
		upscaling->UpdateRenderTargets(1.0f, 1.0f);
		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateRenderTargets:end");
		fo4cs::Diagnostics::WriteHangTraceLine("RestoreNativeRenderState:end");
	}

}


void Upscaling::OnD3D11DeviceCreated(ID3D11Device* a_device, IDXGIAdapter* a_adapter)
{
	(void)a_device;
	(void)a_adapter;
	renderBackendEnabled =
		(UsesDLSSUpscaling() && d3d12Interop && Streamline::GetSingleton()->featureDLSS) ||
		(UsesFSRUpscaling() && d3d12Interop && FidelityFX::GetSingleton()->featureFSR);
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod(bool a_checkMenu) const
{
	if (pluginMode != PluginMode::kUpscaler || !renderBackendEnabled)
		return UpscaleMethod::kDisabled;

	if (IsLoadingMenuOpen())
		return UpscaleMethod::kDisabled;

	if (a_checkMenu) {
		if (auto ui = RE::UI::GetSingleton()) {
			if (ui->GetMenuOpen("ExamineMenu") || ui->GetMenuOpen("PipboyMenu") || ui->GetMenuOpen("TerminalMenu"))
				return UpscaleMethod::kDisabled;
		}
	}

	auto method = GetPreferredUpscaleMethod();
	if (method == UpscaleMethod::kFSR) {
		static bool loggedUnavailableFSR = false;
		const bool fsrInteropReady = d3d12Interop;
		if (!fsrInteropReady || !FidelityFX::GetSingleton()->featureFSR) {
			if (!loggedUnavailableFSR) {
				logger::warn("[Upscaler] FSR is unavailable; disabling FSR");
				loggedUnavailableFSR = true;
			}
			return UpscaleMethod::kDisabled;
		}
	}
	if (method == UpscaleMethod::kDLSS && !Streamline::GetSingleton()->featureDLSS)
		return UpscaleMethod::kDisabled;

	return method;
}

void Upscaling::UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio)
{
	auto rendererData = fo4cs::GetRendererData();
	originalRenderTargets[index] = rendererData->renderTargets[index];

	auto& original = originalRenderTargets[index];
	auto& proxy = proxyRenderTargets[index];

	if (proxy.uaView)
		proxy.uaView->Release();
	if (proxy.srView)
		proxy.srView->Release();
	if (proxy.rtView)
		proxy.rtView->Release();
	if (proxy.texture)
		proxy.texture->Release();
	proxy = {};

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	if (!original.texture) {
		if (settings.debugLogging) {
			logger::debug("[Upscaler] Render target {} has no texture; skipping proxy creation", index);
		}
		return;
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	reinterpret_cast<ID3D11Texture2D*>(original.texture)->GetDesc(&textureDesc);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Render target {} has invalid size {}x{}; skipping proxy creation", index, textureDesc.Width, textureDesc.Height);
		return;
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (original.rtView)
		reinterpret_cast<ID3D11RenderTargetView*>(original.rtView)->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (original.srView)
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srView)->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc{};
	if (original.uaView)
		reinterpret_cast<ID3D11UnorderedAccessView*>(original.uaView)->GetDesc(&uaViewDesc);

	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Render target {} scaled to invalid size; skipping proxy creation", index);
		return;
	}

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxy.texture)));

	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture)) {
		if (original.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, reinterpret_cast<ID3D11RenderTargetView**>(&proxy.rtView)));
		if (original.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srView)));
		if (original.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, reinterpret_cast<ID3D11UnorderedAccessView**>(&proxy.uaView)));
	}
}

void Upscaling::UpdateDepth(float a_currentWidthRatio, float a_currentHeightRatio)
{
	auto rendererData = fo4cs::GetRendererData();
	originalDepthStencilTarget = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
	auto& original = originalDepthStencilTarget;
	auto& proxy = depthOverrideTarget;

	for (int i = 0; i < 4; i++) {
		if (proxy.dsView[i])
			proxy.dsView[i]->Release();
		if (proxy.dsViewReadOnlyDepth[i])
			proxy.dsViewReadOnlyDepth[i]->Release();
		if (proxy.dsViewReadOnlyStencil[i])
			proxy.dsViewReadOnlyStencil[i]->Release();
		if (proxy.dsViewReadOnlyDepthStencil[i])
			proxy.dsViewReadOnlyDepthStencil[i]->Release();
	}
	if (proxy.srViewDepth)
		proxy.srViewDepth->Release();
	if (proxy.srViewStencil)
		proxy.srViewStencil->Release();
	if (proxy.texture)
		proxy.texture->Release();
	proxy = {};

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	if (!original.texture) {
		logger::warn("[Upscaler] Main depth target is unavailable; skipping depth override");
		return;
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	reinterpret_cast<ID3D11Texture2D*>(original.texture)->GetDesc(&textureDesc);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Main depth target has invalid size {}x{}; skipping depth override", textureDesc.Width, textureDesc.Height);
		return;
	}
	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Main depth target scaled to invalid size; skipping depth override");
		return;
	}

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxy.texture)));
	auto texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture);

	for (int i = 0; i < 4; i++) {
		if (original.dsView[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsView[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsView[i])));
		}
		if (original.dsViewReadOnlyDepth[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyDepth[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyDepth[i])));
		}
		if (original.dsViewReadOnlyStencil[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyStencil[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyStencil[i])));
		}
		if (original.dsViewReadOnlyDepthStencil[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyDepthStencil[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyDepthStencil[i])));
		}
	}

	if (original.srViewDepth) {
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srViewDepth)->GetDesc(&desc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srViewDepth)));
	}
	if (original.srViewStencil) {
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srViewStencil)->GetDesc(&desc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srViewStencil)));
	}
}

void Upscaling::UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio)
{
	static float previousWidthRatio = 0.0f;
	static float previousHeightRatio = 0.0f;
	const bool upscalingTextureRequired = upscaleMethodNoMenu != UpscaleMethod::kDisabled;
	if (previousWidthRatio == a_currentWidthRatio && previousHeightRatio == a_currentHeightRatio &&
		(!upscalingTextureRequired || upscalingTexture))
		return;

	TraceRenderBackendStage("UpdateRenderTargets");
	previousWidthRatio = a_currentWidthRatio;
	previousHeightRatio = a_currentHeightRatio;

	for (auto index : renderTargetsPatch)
		UpdateRenderTarget(index, a_currentWidthRatio, a_currentHeightRatio);

	UpdateDepth(a_currentWidthRatio, a_currentHeightRatio);

	upscalingTexture = nullptr;
	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView) {
		logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscaling texture creation");
		return;
	}
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource = nullptr;
	frameBufferSRV->GetResource(&frameBufferResource);
	if (!frameBufferResource) {
		logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscaling texture creation");
		return;
	}

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);
	frameBufferResource->Release();
	if (texDesc.Width == 0 || texDesc.Height == 0) {
		logger::warn("[Upscaler] Frame buffer has invalid size {}x{}; skipping upscaling texture creation", texDesc.Width, texDesc.Height);
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	upscalingTexture = std::make_unique<Texture2D>(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);
}

void Upscaling::OverrideRenderTarget(int index, bool a_doCopy)
{
	if (!proxyRenderTargets[index].texture)
		return;

	auto rendererData = fo4cs::GetRendererData();
	if (a_doCopy) {
		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture));
	}
	rendererData->renderTargets[index] = proxyRenderTargets[index];
}

void Upscaling::ResetRenderTarget(int index, bool a_doCopy)
{
	if (!proxyRenderTargets[index].texture)
		return;

	auto rendererData = fo4cs::GetRendererData();
	if (a_doCopy) {
		D3D11_TEXTURE2D_DESC desc{};
		reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture)->GetDesc(&desc);
		D3D11_BOX box{ 0, 0, 0, desc.Width, desc.Height, 1 };
		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), 0, &box);
	}
	rendererData->renderTargets[index] = originalRenderTargets[index];
}

void Upscaling::OverrideRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	const bool copyAll = a_indicesToCopy.empty();
	for (auto index : renderTargetsPatch) {
		const bool doCopy = copyAll || std::ranges::find(a_indicesToCopy, index) != a_indicesToCopy.end();
		OverrideRenderTarget(index, doCopy);
	}
}

void Upscaling::ResetRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	const bool copyAll = a_indicesToCopy.empty();
	for (auto index : renderTargetsPatch) {
		const bool doCopy = copyAll || std::ranges::find(a_indicesToCopy, index) != a_indicesToCopy.end();
		ResetRenderTarget(index, doCopy);
	}
}

void Upscaling::OverrideDepth(bool a_doCopy)
{
	if (!depthOverrideTarget.texture)
		return;

	auto rendererData = fo4cs::GetRendererData();
	if (a_doCopy)
		CopyDepth();
	rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain] = depthOverrideTarget;
}

void Upscaling::ResetDepth()
{
	if (!depthOverrideTarget.texture)
		return;
	fo4cs::GetRendererData()->depthStencilTargets[(uint)DepthStencilTarget::kMain] = originalDepthStencilTarget;
}

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static float previousMipBias = 1000.0f;
	if (previousMipBias == a_currentMipBias)
		return;
	previousMipBias = a_currentMipBias;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();

	for (int i = 0; i < 320; i++) {
		originalSamplerStates[i] = samplerStates[i];
		if (!originalSamplerStates[i])
			continue;

		if (biasedSamplerStates[i]) {
			biasedSamplerStates[i]->Release();
			biasedSamplerStates[i] = nullptr;
		}

		D3D11_SAMPLER_DESC desc{};
		originalSamplerStates[i]->GetDesc(&desc);
		desc.MipLODBias = a_currentMipBias;
		DX::ThrowIfFailed(device->CreateSamplerState(&desc, &biasedSamplerStates[i]));
	}
}

void Upscaling::OverrideSamplerStates()
{
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();
	for (int i = 0; i < 320; i++) {
		if (biasedSamplerStates[i])
			samplerStates[i] = biasedSamplerStates[i];
	}
}

void Upscaling::ResetSamplerStates()
{
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();
	for (int i = 0; i < 320; i++) {
		if (originalSamplerStates[i])
			samplerStates[i] = originalSamplerStates[i];
	}
}

void Upscaling::CopyDepth()
{
	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto gameViewport = fo4cs::RE::GetGraphicsState();
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.x * renderTargetManager->dynamicWidthRatio))),
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.y * renderTargetManager->dynamicHeightRatio))));
	UpdateAndBindUpscalingCB(context, screenSize, renderSize);

	ID3D11ShaderResourceView* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalDepthStencilTarget.srViewDepth);
	if (!depthSRV || !depthOverrideTarget.dsView[0])
		return;

	winrt::com_ptr<ID3D11DepthStencilState> oldDepthStencilState;
	UINT oldStencilRef = 0;
	context->OMGetDepthStencilState(oldDepthStencilState.put(), &oldStencilRef);

	winrt::com_ptr<ID3D11BlendState> oldBlendState;
	FLOAT oldBlendFactor[4]{};
	UINT oldSampleMask = 0;
	context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

	winrt::com_ptr<ID3D11RasterizerState> oldRasterizerState;
	context->RSGetState(oldRasterizerState.put());

	ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	winrt::com_ptr<ID3D11DepthStencilView> oldDSV;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV.put());

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	winrt::com_ptr<ID3D11VertexShader> oldVS;
	context->VSGetShader(oldVS.put(), nullptr, nullptr);
	winrt::com_ptr<ID3D11PixelShader> oldPS;
	context->PSGetShader(oldPS.put(), nullptr, nullptr);

	ID3D11ShaderResourceView* oldPSSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
	context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, oldPSSRVs);

	winrt::com_ptr<ID3D11InputLayout> oldInputLayout;
	context->IAGetInputLayout(oldInputLayout.put());
	D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
	context->IAGetPrimitiveTopology(&oldTopology);

	auto dsvPointer = reinterpret_cast<ID3D11DepthStencilView*>(depthOverrideTarget.dsView[0]);
	context->OMSetRenderTargets(0, nullptr, dsvPointer);
	context->OMSetDepthStencilState(GetCopyDepthStencilState(), 0xFF);
	FLOAT blendFactor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
	context->OMSetBlendState(GetCopyBlendState(), blendFactor, 0xFFFFFFFF);
	context->RSSetState(GetCopyRasterizerState());

	D3D11_VIEWPORT viewport{ 0.0f, 0.0f, renderSize.x, renderSize.y, 0.0f, 1.0f };
	context->RSSetViewports(1, &viewport);

	auto upscalingBuffer = GetUpscalingCB()->CB();
	context->PSSetConstantBuffers(0, 1, &upscalingBuffer);
	context->VSSetShader(GetCopyDepthVS(), nullptr, 0);
	context->PSSetShader(GetCopyDepthPS(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &depthSRV);
	ID3D11SamplerState* samplers[] = { GetCopySamplerState() };
	context->PSSetSamplers(0, 1, samplers);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->Draw(3, 0);

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

	for (auto* rtv : oldRTVs) {
		if (rtv)
			rtv->Release();
	}
	for (auto* srv : oldPSSRVs) {
		if (srv)
			srv->Release();
	}
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMethodNoMenu = UpscaleMethod::kDisabled;

	const auto hasUpscalingResources = [&]() {
		if (upscaleMethodNoMenu == UpscaleMethod::kDisabled)
			return true;
		if (!d3d12Interop || !upscalingTexture)
			return false;
		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;
		return upscalerInputShared[frameIndex] &&
		       upscalerOutputShared[frameIndex] &&
		       upscalerInputShared12[frameIndex] &&
		       upscalerOutputShared12[frameIndex];
	};

	const bool methodChanged = previousUpscaleMethodNoMenu != upscaleMethodNoMenu;
	if (!methodChanged && hasUpscalingResources())
		return;

	TraceRenderBackendStage("CheckResources");
	if (methodChanged && previousUpscaleMethodNoMenu == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->DestroyDLSSResources();

	if (upscaleMethodNoMenu == UpscaleMethod::kDisabled) {
		DestroyUpscalingResources();
	} else {
		if (methodChanged && previousUpscaleMethodNoMenu != UpscaleMethod::kDisabled)
			DestroyUpscalingResources();
		if (!setupBuffers)
			CreateFrameGenerationResources();
		CreateUpscalingResources();
	}

	previousUpscaleMethodNoMenu = upscaleMethodNoMenu;
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS)
		dilateMotionVectorCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\DilateMotionVectorCS.hlsl", "cs_5_0"));
	return dilateMotionVectorCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideLinearDepthCS()
{
	if (!overrideLinearDepthCS)
		overrideLinearDepthCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\OverrideLinearDepthCS.hlsl", "cs_5_0"));
	return overrideLinearDepthCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS)
		overrideDepthCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\OverrideDepthCS.hlsl", "cs_5_0"));
	return overrideDepthCS.get();
}

ID3D11VertexShader* Upscaling::GetCopyDepthVS()
{
	if (!copyDepthVS)
		copyDepthVS.attach((ID3D11VertexShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\CopyDepthVS.hlsl", "vs_5_0"));
	return copyDepthVS.get();
}

ID3D11PixelShader* Upscaling::GetCopyDepthPS()
{
	if (!copyDepthPS)
		copyDepthPS.attach((ID3D11PixelShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\CopyDepthPS.hlsl", "ps_5_0"));
	return copyDepthPS.get();
}

ID3D11DepthStencilState* Upscaling::GetCopyDepthStencilState()
{
	if (!copyDepthStencilState) {
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateDepthStencilState(&desc, copyDepthStencilState.put()));
	}
	return copyDepthStencilState.get();
}

ID3D11BlendState* Upscaling::GetCopyBlendState()
{
	if (!copyBlendState) {
		D3D11_BLEND_DESC desc{};
		desc.RenderTarget[0].RenderTargetWriteMask = 0;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBlendState(&desc, copyBlendState.put()));
	}
	return copyBlendState.get();
}

ID3D11RasterizerState* Upscaling::GetCopyRasterizerState()
{
	if (!copyRasterizerState) {
		D3D11_RASTERIZER_DESC desc{};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRasterizerState(&desc, copyRasterizerState.put()));
	}
	return copyRasterizerState.get();
}

ID3D11SamplerState* Upscaling::GetCopySamplerState()
{
	if (!copySamplerState) {
		D3D11_SAMPLER_DESC desc{};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateSamplerState(&desc, copySamplerState.put()));
	}
	return copySamplerState.get();
}

ID3D11PixelShader* Upscaling::GetBSImagespaceShaderSSLRRaytracing()
{
	if (!BSImagespaceShaderSSLRRaytracing)
		BSImagespaceShaderSSLRRaytracing.attach((ID3D11PixelShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\BSImagespaceShaderSSLRRaytracing.hlsl", "ps_5_0"));
	return BSImagespaceShaderSSLRRaytracing.get();
}

ConstantBuffer* Upscaling::GetUpscalingCB()
{
	static std::unique_ptr<ConstantBuffer> upscalingCB;
	if (!upscalingCB)
		upscalingCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpscalingCB>());
	return upscalingCB.get();
}

void Upscaling::UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize)
{
	UpscalingCB data{};
	data.ScreenSize[0] = static_cast<uint>(a_screenSize.x);
	data.ScreenSize[1] = static_cast<uint>(a_screenSize.y);
	data.RenderSize[0] = static_cast<uint>(a_renderSize.x);
	data.RenderSize[1] = static_cast<uint>(a_renderSize.y);
	data.CameraData = float4(fo4cs::RE::GetCameraFar(), fo4cs::RE::GetCameraNear(), fo4cs::RE::GetCameraFar() - fo4cs::RE::GetCameraNear(), fo4cs::RE::GetCameraFar() * fo4cs::RE::GetCameraNear());

	auto cb = GetUpscalingCB();
	cb->Update(data);
	auto buffer = cb->CB();
	a_context->CSSetConstantBuffers(0, 1, &buffer);
}

void Upscaling::UpdateGameSettings()
{
	*fo4cs::RE::GetTAAEnableFlag() = true;
}

void Upscaling::UpdateUpscaling()
{
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:enter");
	if (pluginMode != PluginMode::kUpscaler)
	{
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:not-upscaler-mode");
		return;
	}

	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:begin");
	if (IsLoadingMenuOpen()) {
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:true");
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		postLoadingSkipUpscale = true;
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:loading-menu");
		return;
	}
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:false");

	TraceRenderBackendStage("UpdateUpscaling");
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetGraphicsState:begin");
	auto gameViewport = fo4cs::RE::GetGraphicsState();
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetGraphicsState:end");
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetRenderTargetManager:begin");
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetRenderTargetManager:end");
	if (!gameViewport || !renderTargetManager) {
		logger::warn("[Upscaler] Render backend globals are unavailable; disabling upscaling for this update");
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:missing-globals");
		return;
	}

	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:read-screen-size:begin");
	auto screenWidth = gameViewport->screenWidth;
	auto screenHeight = gameViewport->screenHeight;
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:read-screen-size:end");
	if (screenWidth == 0 || screenHeight == 0 || screenWidth > 16384 || screenHeight > 16384) {
		logger::warn("[Upscaler] Invalid viewport size {}x{}; disabling upscaling for this update", screenWidth, screenHeight);
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:invalid-screen-size");
		return;
	}

	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetUpscaleMethod:begin");
	upscaleMethodNoMenu = GetUpscaleMethod(false);
	upscaleMethod = GetUpscaleMethod(true);
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:GetUpscaleMethod:end");


	float resolutionScale = upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / GetUpscaleRatio(settings.qualityMode);
	float currentMipBias = std::log2f(resolutionScale);
	if (upscaleMethodNoMenu != UpscaleMethod::kDisabled)
		currentMipBias -= 1.0f;

	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateSamplerStates:begin");
	UpdateSamplerStates(currentMipBias);
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateSamplerStates:end");
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateRenderTargets:begin");
	UpdateRenderTargets(resolutionScale, resolutionScale);
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateRenderTargets:end");
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateGameSettings:begin");
	UpdateGameSettings();
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateGameSettings:end");

	if (upscaleMethod == UpscaleMethod::kDisabled)
		resolutionScale = 1.0f;

	if (upscaleMethod != UpscaleMethod::kDisabled) {
		const auto width = screenWidth;
		const auto height = screenHeight;
		const auto renderWidth = std::max(1u, static_cast<uint>(static_cast<float>(width) * resolutionScale));
		const auto renderHeight = std::max(1u, static_cast<uint>(static_cast<float>(height) * resolutionScale));
		auto phaseCount = std::max(1, static_cast<int>(8.0f * std::pow(static_cast<float>(width) / static_cast<float>(renderWidth), 2.0f)));
		GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);
		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(renderWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(renderHeight);
	}

	renderTargetManager->dynamicWidthRatio = resolutionScale;
	renderTargetManager->dynamicHeightRatio = resolutionScale;
	renderTargetManager->isDynamicResolutionCurrentlyActivated = renderTargetManager->dynamicWidthRatio != 1.0f || renderTargetManager->dynamicHeightRatio != 1.0f;

	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:CheckResources:begin");
	CheckResources();
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:CheckResources:end");
	if (!renderBackendEnabled || upscaleMethodNoMenu == UpscaleMethod::kDisabled) {
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:RestoreNativeRenderState:disabled:begin");
		RestoreNativeRenderState(renderTargetManager, gameViewport);
		fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:RestoreNativeRenderState:disabled:end");
	}
	fo4cs::Diagnostics::WriteHangTraceLine("UpdateUpscaling:exit");
}

bool Upscaling::Upscale()
{
	if (postLoadingSkipUpscale) {
		postLoadingSkipUpscale = false;
		return false;
	}

	if (upscaleMethod == UpscaleMethod::kDisabled || !upscalingTexture)
		return false;

	TraceRenderBackendStage("Upscale");
	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView);
	if (!frameBufferSRV) {
		logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscale dispatch");
		return false;
	}
	ID3D11Resource* frameBufferResource = nullptr;
	frameBufferSRV->GetResource(&frameBufferResource);
	if (!frameBufferResource) {
		logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscale dispatch");
		return false;
	}
	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	auto gameViewport = fo4cs::RE::GetGraphicsState();
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	const auto renderWidth = ScaleRenderExtent(static_cast<uint32_t>(gameViewport->screenWidth), renderTargetManager->dynamicWidthRatio);
	const auto renderHeight = ScaleRenderExtent(static_cast<uint32_t>(gameViewport->screenHeight), renderTargetManager->dynamicHeightRatio);
	auto renderSize = float2(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
	bool dispatchedUpscale = false;

	if (upscaleMethod == UpscaleMethod::kDLSS && d3d12Interop) {
		if (!setupBuffers)
			CreateFrameGenerationResources();

		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;

		const bool missingSharedColor =
			!upscalerInputShared[frameIndex] || !upscalerOutputShared[frameIndex] ||
			!upscalerInputShared12[frameIndex] || !upscalerOutputShared12[frameIndex];
		const bool mismatchedSharedColor =
			upscalerInputShared[frameIndex] &&
			(upscalerInputShared[frameIndex]->desc.Width != upscalingTexture->desc.Width ||
			 upscalerInputShared[frameIndex]->desc.Height != upscalingTexture->desc.Height ||
			 upscalerInputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format);

		if (missingSharedColor || mismatchedSharedColor)
			CreateUpscalingResources();

		if (upscalerInputShared[frameIndex] && upscalerOutputShared[frameIndex] &&
			upscalerInputShared12[frameIndex] && upscalerOutputShared12[frameIndex] &&
			depthBufferShared12[frameIndex] && motionVectorBufferShared12[frameIndex]) {
			context->CopyResource(upscalerInputShared[frameIndex]->resource.get(), frameBufferResource);
			CopyBuffersToSharedResources();

			auto commandList = dx12SwapChain->BeginInteropCommandList();
			const bool dispatched = Streamline::GetSingleton()->Upscale(
				commandList,
				upscalerInputShared12[frameIndex].get(),
				upscalerOutputShared12[frameIndex].get(),
				depthBufferShared12[frameIndex].get(),
				motionVectorBufferShared12[frameIndex].get(),
				jitter,
				renderSize,
				screenSize,
				settings.qualityMode);
			dx12SwapChain->ExecuteInteropCommandListAndWait();

			if (dispatched) {
				dispatchedUpscale = true;
				context->CopyResource(frameBufferResource, upscalerOutputShared[frameIndex]->resource.get());
				if (settings.qualityMode == 0 && upscalingTexture)
					CopyNativeAABorder(context, upscalingTexture->resource.get(), frameBufferResource, upscalingTexture->desc.Width, upscalingTexture->desc.Height);
			}
		}
	} else if (upscaleMethod == UpscaleMethod::kFSR && d3d12Interop) {
		if (!setupBuffers)
			CreateFrameGenerationResources();

		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;

		const bool missingSharedColor =
			!upscalerInputShared[frameIndex] || !upscalerOutputShared[frameIndex] ||
			!upscalerInputShared12[frameIndex] || !upscalerOutputShared12[frameIndex];
		const bool mismatchedSharedColor =
			(upscalerInputShared[frameIndex] &&
			 (upscalerInputShared[frameIndex]->desc.Width != renderWidth ||
			  upscalerInputShared[frameIndex]->desc.Height != renderHeight ||
			  upscalerInputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format)) ||
			(upscalerOutputShared[frameIndex] &&
			 (upscalerOutputShared[frameIndex]->desc.Width != upscalingTexture->desc.Width ||
			  upscalerOutputShared[frameIndex]->desc.Height != upscalingTexture->desc.Height ||
			  upscalerOutputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format));

		if (missingSharedColor || mismatchedSharedColor)
			CreateUpscalingResources();

		if (upscalerInputShared[frameIndex] && upscalerOutputShared[frameIndex] &&
			upscalerInputShared12[frameIndex] && upscalerOutputShared12[frameIndex] &&
			depthBufferShared12[frameIndex] && motionVectorBufferShared12[frameIndex]) {
			D3D11_BOX srcBox{ 0, 0, 0, renderWidth, renderHeight, 1 };
			context->CopySubresourceRegion(
				upscalerInputShared[frameIndex]->resource.get(),
				0,
				0,
				0,
				0,
				frameBufferResource,
				0,
				&srcBox);
			CopyBuffersToSharedResources();

			auto commandList = dx12SwapChain->BeginInteropCommandList();
			const bool dispatched = FidelityFX::GetSingleton()->Upscale(
				commandList,
				upscalerInputShared12[frameIndex].get(),
				upscalerOutputShared12[frameIndex].get(),
				depthBufferShared12[frameIndex].get(),
				motionVectorBufferShared12[frameIndex].get(),
				jitter,
				renderSize,
				screenSize,
				settings.qualityMode);
			dx12SwapChain->ExecuteInteropCommandListAndWait();

			if (dispatched) {
				dispatchedUpscale = true;
				context->CopyResource(frameBufferResource, upscalerOutputShared[frameIndex]->resource.get());
				if (settings.qualityMode == 0 && upscalingTexture)
					CopyNativeAABorder(context, upscalingTexture->resource.get(), frameBufferResource, upscalingTexture->desc.Width, upscalingTexture->desc.Height);
			}
		}
	}

	frameBufferResource->Release();
	return dispatchedUpscale;
}

bool Upscaling::CreateUpscalingResources()
{
	TraceRenderBackendStage("CreateUpscalingResources");
	auto renderer = fo4cs::GetRendererData();
	auto& main = renderer->renderTargets[(uint)RenderTarget::kMain];
	if (!main.texture) {
		static bool loggedMissingMain = false;
		if (!loggedMissingMain) {
			logger::warn("[Upscaler] Main render target is unavailable; deferring upscaling shared resources");
			loggedMissingMain = true;
		}
		return false;
	}
	const bool needsDLSSSharedResources = UsesDLSSUpscaling() && Streamline::GetSingleton()->featureDLSS;
	const bool needsFSRSharedResources = UsesFSRUpscaling() && FidelityFX::GetSingleton()->featureFSR;
	if (!needsDLSSSharedResources && !needsFSRSharedResources)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	if (!d3d12Interop || !dx12SwapChain->d3d12Device)
		return false;

	D3D11_TEXTURE2D_DESC presentationColorDesc{};
	if (upscalingTexture) {
		presentationColorDesc = upscalingTexture->desc;
	} else {
		auto& frameBuffer = renderer->renderTargets[(uint)RenderTarget::kFrameBuffer];
		auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(frameBuffer.srView);
		if (!frameBufferSRV) {
			logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscaling shared resources");
			return false;
		}

		winrt::com_ptr<ID3D11Resource> frameBufferResource;
		frameBufferSRV->GetResource(frameBufferResource.put());
		if (!frameBufferResource) {
			logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscaling shared resources");
			return false;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBufferTexture;
		if (FAILED(frameBufferResource->QueryInterface(IID_PPV_ARGS(frameBufferTexture.put()))) || !frameBufferTexture) {
			logger::warn("[Upscaler] Frame buffer resource is not a Texture2D; skipping upscaling shared resources");
			return false;
		}

		frameBufferTexture->GetDesc(&presentationColorDesc);
	}
	if (presentationColorDesc.Width == 0 || presentationColorDesc.Height == 0) {
		logger::warn("[Upscaler] Frame buffer target has invalid size {}x{}; skipping upscaling shared resources", presentationColorDesc.Width, presentationColorDesc.Height);
		return false;
	}
	presentationColorDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	presentationColorDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	presentationColorDesc.CPUAccessFlags = 0;
	presentationColorDesc.Usage = D3D11_USAGE_DEFAULT;

	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	const auto renderWidthRatio = renderTargetManager ? renderTargetManager->dynamicWidthRatio : 1.0f;
	const auto renderHeightRatio = renderTargetManager ? renderTargetManager->dynamicHeightRatio : 1.0f;
	const auto renderWidth = ScaleRenderExtent(presentationColorDesc.Width, renderWidthRatio);
	const auto renderHeight = ScaleRenderExtent(presentationColorDesc.Height, renderHeightRatio);

	D3D11_TEXTURE2D_DESC inputColorDesc = presentationColorDesc;
	if (needsFSRSharedResources) {
		inputColorDesc.Width = renderWidth;
		inputColorDesc.Height = renderHeight;
	}
	D3D11_TEXTURE2D_DESC outputColorDesc = presentationColorDesc;

	const auto openSharedTexture = [&](Texture2D* texture, winrt::com_ptr<ID3D12Resource>& outResource) {
		winrt::com_ptr<IDXGIResource1> dxgiResource;
		DX::ThrowIfFailed(texture->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));
		DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(outResource.put())));
		CloseHandle(sharedHandle);
	};

	for (int index = 0; index < 2; index++) {
		delete upscalerInputShared[index];
		delete upscalerOutputShared[index];
		upscalerInputShared[index] = new Texture2D(inputColorDesc);
		upscalerOutputShared[index] = new Texture2D(outputColorDesc);
		upscalerInputShared12[index] = nullptr;
		upscalerOutputShared12[index] = nullptr;
		openSharedTexture(upscalerInputShared[index], upscalerInputShared12[index]);
		openSharedTexture(upscalerOutputShared[index], upscalerOutputShared12[index]);
	}

	if (needsFSRSharedResources) {
		const bool fsrSetupSucceeded = FidelityFX::GetSingleton()->SetupUpscaling(
			dx12SwapChain->d3d12Device.get(),
			inputColorDesc.Width,
			inputColorDesc.Height,
			outputColorDesc.Width,
			outputColorDesc.Height);
		if (!fsrSetupSucceeded) {
			logger::error("[Upscaler] FSR upscaling context creation failed; disabling upscaling backend");
			upscaleMethodNoMenu = UpscaleMethod::kDisabled;
			upscaleMethod = UpscaleMethod::kDisabled;
			renderBackendEnabled = false;
			return false;
		}
	}

	logger::info("[Upscaler] Upscaling shared resources created (input={}x{} fmt={}, output={}x{} fmt={}, final={}x{} fmt={}, fsr={}, dlss={})",
		inputColorDesc.Width,
		inputColorDesc.Height,
		static_cast<uint32_t>(inputColorDesc.Format),
		outputColorDesc.Width,
		outputColorDesc.Height,
		static_cast<uint32_t>(outputColorDesc.Format),
		presentationColorDesc.Width,
		presentationColorDesc.Height,
		static_cast<uint32_t>(presentationColorDesc.Format),
		needsFSRSharedResources,
		needsDLSSSharedResources);
	return true;
}

void Upscaling::DestroyUpscalingResources()
{
	dilatedMotionVectorTexture.reset();
	FidelityFX::GetSingleton()->DestroyUpscaling();

	for (int index = 0; index < 2; index++) {
		delete upscalerInputShared[index];
		delete upscalerOutputShared[index];
		upscalerInputShared[index] = nullptr;
		upscalerOutputShared[index] = nullptr;
		upscalerInputShared12[index] = nullptr;
		upscalerOutputShared12[index] = nullptr;
	}
}

void Upscaling::PatchSSRShader()
{
	auto context = reinterpret_cast<ID3D11DeviceContext*>(fo4cs::GetRendererData()->context);
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}

namespace
{
	struct BSGraphics_State_UpdateDynamicResolution
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, RE::NiPoint3* a2, RE::NiPoint3* a3, RE::NiPoint3* a4, RE::NiPoint3* a5)
		{
			TraceRenderBackendStage("hook:UpdateDynamicResolution:game");
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:begin");
			func(This, a2, a3, a4, a5);
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:end");
			TraceRenderBackendStage("hook:UpdateDynamicResolution:fo4cs");
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:begin");
			Upscaling::GetSingleton()->UpdateUpscaling();
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:end");
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectTemporalAA_IsActive
	{
		static bool thunk(void* This)
		{
			return Upscaling::GetSingleton()->upscaleMethod == Upscaling::UpscaleMethod::kDisabled && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_DeferredPrePass
	{
		static void thunk(void* This)
		{
			TraceRenderBackendStage("hook:PreUI_DeferredPrePass");
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_Forward
	{
		static void thunk(void* This)
		{
			TraceRenderBackendStage("hook:PreUI_Forward");
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void InstallUpscalerRenderBackendHooks()
{
	stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

#if defined(FALLOUT_POST_NG)
	namespace F4Hooks = RE::FO4Runtime::PostNG::Hooks;
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(F4Hooks::UPSCALER_RENDER_BACKEND_DYNAMIC_RESOLUTION_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(F4Hooks::UPSCALER_RENDER_BACKEND_DEFERRED_PREPASS_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(F4Hooks::UPSCALER_RENDER_BACKEND_FORWARD_CALL.address());
#else
	namespace F4Hooks = RE::FO4Runtime::PreNG::Hooks;
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(F4Hooks::UPSCALER_RENDER_BACKEND_DYNAMIC_RESOLUTION_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(F4Hooks::UPSCALER_RENDER_BACKEND_DEFERRED_PREPASS_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(F4Hooks::UPSCALER_RENDER_BACKEND_FORWARD_CALL.address());
#endif

	logger::debug("[Upscaler] Installed render backend hooks");
}
