#include "Upscaling.h"

#include <d3dcompiler.h>

#include "DX12SwapChain.h"
#include "DirectXMath.h"

enum class RenderTarget
{
	kFrameBuffer = 0,

	kRefractionNormal = 1,

	kMain = 3,
	kMainTemp = 4,

	kSSRRaw = 7,
	kSSRBlurred = 8,
	kSSRBlurredExtra = 9,

	kMainVerticalBlur = 14,
	kMainHorizontalBlur = 15,

	kSSRDirection = 10,
	kSSRMask = 11,

	kUI = 17,
	kUITemp = 18,

	kGbufferNormal = 20,
	kGbufferNormalSwap = 21,
	kGbufferAlbedo = 22,
	kGbufferEmissive = 23,
	kGbufferMaterial = 24, //  Glossiness, Specular, Backlighting, SSS

	kSSAO = 28,

	kTAAAccumulation = 26,
	kTAAAccumulationSwap = 27,

	kMotionVectors = 29,

	kUIDownscaled = 36,
	kUIDownscaledComposite = 37,

	kMainDepthMips = 39,

	kUnkMask = 57,

	kSSAOTemp = 48,
	kSSAOTemp2 = 49,
	kSSAOTemp3 = 50,

	kDiffuseBuffer = 58,
	kSpecularBuffer = 59,

	kDownscaledHDR = 64,
	kDownscaledHDRLuminance2 = 65,
	kDownscaledHDRLuminance3 = 66,
	kDownscaledHDRLuminance4 = 67,
	kDownscaledHDRLuminance5Adaptation = 68,
	kDownscaledHDRLuminance6AdaptationSwap = 69,
	kDownscaledHDRLuminance6 = 70,

	kCount = 101
};

enum class DepthStencilTarget
{
	kMainOtherOther = 0,
	kMainOther = 1,
	kMain = 2,
	kMainCopy = 3,
	kMainCopyCopy = 4,

	kShadowMap = 8,

	kCount = 13
};

ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program = "main")
{
	auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Compiler setup
	uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	ID3DBlob* shaderBlob;
	ID3DBlob* shaderErrors;

	std::string str;
	std::wstring path{ FilePath };
	std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
		return (char)c;
	});
	if (!std::filesystem::exists(FilePath)) {
		logger::error("Failed to compile shader; {} does not exist", str);
		return nullptr;
	}
	if (FAILED(D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, &shaderBlob, &shaderErrors))) {
		logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
		return nullptr;
	}
	if (shaderErrors)
		logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

	ID3D11ComputeShader* regShader;
	DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
	return regShader;
}

void Upscaling::LoadSettings()
{
	logger::info("[Frame Generation] Loading settings");

	CSimpleIniA ini;
	ini.SetUnicode();
	ini.LoadFile("Data\\F4SE\\Plugins\\FrameGeneration.ini");

	settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", true);
	settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", true);
	settings.frameGenerationForceEnable = ini.GetBoolValue("Settings", "bFrameGenerationForceEnable", false);

	logger::info("[Frame Generation] bFrameGenerationMode: {}", settings.frameGenerationMode);
	logger::info("[Frame Generation] bFrameLimitMode: {}", settings.frameLimitMode);
	logger::info("[Frame Generation] bFrameGenerationForceEnable: {}", settings.frameGenerationForceEnable);
}

void Upscaling::PostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleA("Data\\F4SE\\Plugins\\HighFPSPhysicsFix.dll") != nullptr;

	if (highFPSPhysicsFixLoaded)
		logger::info("[Frame Generation] HighFPSPhysicsFix.dll is loaded");
	else
		logger::info("[Frame Generation] HighFPSPhysicsFix.dll is not loaded");
}

void Upscaling::CreateFrameGenerationResources()
{
	logger::info("[Frame Generation] Creating resources");
	
	setupBuffers = true;

	auto rendererData = RE::BSGraphics::RendererData::GetSingleton();
	auto& main = rendererData->renderTargets[(uint)RenderTarget::kMain];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
	reinterpret_cast<ID3D11ShaderResourceView*>(main.srView)->GetDesc(&srvDesc);
	reinterpret_cast<ID3D11RenderTargetView*>(main.rtView)->GetDesc(&rtvDesc);

	texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	depthBufferShared = new Texture2D(texDesc);
	depthBufferShared->CreateSRV(srvDesc);
	depthBufferShared->CreateRTV(rtvDesc);
	depthBufferShared->CreateUAV(uavDesc);

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	D3D11_TEXTURE2D_DESC texDescMotionVector{};
	reinterpret_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&texDescMotionVector);

	texDesc.Format = texDescMotionVector.Format;
	srvDesc.Format = texDesc.Format;
	rtvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	motionVectorBufferShared = new Texture2D(texDesc);
	motionVectorBufferShared->CreateSRV(srvDesc);
	motionVectorBufferShared->CreateRTV(rtvDesc);
	motionVectorBufferShared->CreateUAV(uavDesc);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(depthBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		if (dx12SwapChain->swapChain) {
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&sharedHandle));

			DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
				sharedHandle,
				IID_PPV_ARGS(&depthBufferShared12)));

			CloseHandle(sharedHandle);
		}
	}

	{
		IDXGIResource1* dxgiResource = nullptr;
		DX::ThrowIfFailed(motionVectorBufferShared->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

		if (dx12SwapChain->swapChain) {
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&sharedHandle));

			DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
				sharedHandle,
				IID_PPV_ARGS(&motionVectorBufferShared12)));

			CloseHandle(sharedHandle);
		}
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)CompileShader(L"Data\\F4SE\\Plugins\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", "cs_5_0");	
}

struct WindowSizeChanged
{
	static void thunk(RE::BSGraphics::Renderer*, unsigned int)
	{
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SetUseDynamicResolutionViewportAsDefaultViewport
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
	{
		if (!a_true)
			Upscaling::GetSingleton()->PostDisplay();
		func(This, a_true);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

thread_local bool fixWeaponModel = false;
thread_local bool fixAnimation = false;

struct BSGeometry_UpdateWorldData
{
	static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_updateData)
	{
		auto prevWorld = a_object->world;

		func(a_object, a_updateData);

		if (fixWeaponModel)
			a_object->world = prevWorld;

		if (fixAnimation)
			a_object->previousWorld = a_object->world;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct PlayerCharacter_UpdateScenegraph
{
	static void thunk(RE::NiAVObject* NiAVObject, RE::NiUpdateData* NiUpdateData)
	{
		fixWeaponModel = true;
		func(NiAVObject, NiUpdateData);
		fixWeaponModel = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESObjectREFR_SetSequencePosition
{
	static void thunk(void* This, RE::NiControllerManager* a2,
		const char* a3,
		float a4)
	{
		fixAnimation = true;
		func(This, a2, a3, a4);
		fixAnimation = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
#if defined(FALLOUT_POST_NG)
	stl::detour_thunk<WindowSizeChanged>(REL::ID(2276824));
	stl::detour_thunk<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(2277194));
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(2270409));
	stl::write_thunk_call<PlayerCharacter_UpdateScenegraph>(REL::ID(2233006).address() + 0x286);
	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(2200766));
#else
	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(REL::ID(212827));

	// Watch frame presentation
	stl::detour_thunk<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(676851));
	
	// Fix broken motion vectors from bad geometry updates
	stl::detour_thunk<BSGeometry_UpdateWorldData>(REL::ID(551661));
	stl::write_thunk_call<PlayerCharacter_UpdateScenegraph>(REL::ID(978934).address() + 0x2ED);
	stl::detour_thunk<TESObjectREFR_SetSequencePosition>(REL::ID(854236));
#endif

	logger::info("[Upscaling] Installed hooks");
}

void Upscaling::CopyBuffersToSharedResources()
{
	if (!d3d12Interop)
		return;

	auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	context->CopyResource(motionVectorBufferShared->resource.get(), reinterpret_cast<ID3D11Texture2D*>(motionVector.texture));
		
	{
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);


			ID3D11ShaderResourceView* views[1] = { reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);

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

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter(bool a_useFrameGeneration)
{
	static LARGE_INTEGER lastFrame = {};

	if (d3d12Interop && settings.frameLimitMode) {
		double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;

		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * (a_useFrameGeneration ? 0.5 : 1.0)));

		LARGE_INTEGER timeNow;
		QueryPerformanceCounter(&timeNow);
		int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
		if (delta < targetFrameTicks) {
			TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
		}
	}

	QueryPerformanceCounter(&lastFrame);
}

void Upscaling::GameFrameLimiter()
{
	double bestRefreshRate = 60.0f;

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);

	int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / bestRefreshRate);

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (delta < targetFrameTicks) {
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	}
	QueryPerformanceCounter(&lastFrame);	
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

void Upscaling::PostDisplay()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	
	CopyBuffersToSharedResources();

	auto rendererData = RE::BSGraphics::RendererData::GetSingleton();

	auto& swapChain = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	auto dx12SwapChain = DX12SwapChain::GetSingleton();

#if defined(FALLOUT_POST_NG)
	swapChain.rtView = reinterpret_cast<REX::W32::ID3D11RenderTargetView*>(dx12SwapChain->uiBufferWrapped->rtv);
#else
	swapChain.rtView = dx12SwapChain->uiBufferWrapped->rtv;
#endif

	inGame = true;
}