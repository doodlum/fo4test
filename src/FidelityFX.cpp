#include "FidelityFX.h"

#include "Upscaling.h"

#include "DX12SwapChain.h"
#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

void FidelityFX::LoadFFX()
{
	module = LoadLibrary(L"Data\\F4SE\\Plugins\\FrameGeneration\\FidelityFX\\amd_fidelityfx_dx12.dll");

	if (module)
		ffxLoadFunctions(&ffxModule, module);
}

void FidelityFX::SetupFrameGeneration()
{
	auto swapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = 0;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain->swapChainDesc.Format);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = swapChain->d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create frame generation context!");
	}
}

void FidelityFX::Present(bool a_useFrameGeneration)
{
	auto upscaling = Upscaling::GetSingleton();
	auto swapChain = DX12SwapChain::GetSingleton();
	auto commandList = swapChain->commandLists[swapChain->frameIndex].get();
	
	auto HUDLessColor = upscaling->HUDLessBufferShared12.get();
	auto depth = upscaling->depthBufferShared12.get();
	auto motionVectors = upscaling->motionVectorBufferShared12.get();

	FfxApiSwapchainFramePacingTuning framePacingTuning{ 0.1f, 0.1f, true, 2, false };

	ffx::ConfigureDescFrameGenerationSwapChainKeyValueDX12 framePacingTuningParameters{};
	framePacingTuningParameters.key = FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING;
	framePacingTuningParameters.ptr = &framePacingTuning;

	if (ffx::Configure(swapChainContext, framePacingTuningParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame pacing tuning!");
	}

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
			};
		configParameters.frameGenerationCallbackUserContext = &frameGenContext;

		configParameters.HUDLessColor = ffxApiGetResourceDX12(HUDLessColor);

	}
	else {
		configParameters.frameGenerationEnabled = false;

		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;

		configParameters.HUDLessColor = FfxApiResource({});
	}

	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain->swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.allowAsyncWorkloads = false;
	configParameters.flags = 0;

	configParameters.generationRect.left = (swapChain->swapChainDesc.Width - swapChain->swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain->swapChainDesc.Height - swapChain->swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain->swapChainDesc.Width;
	configParameters.generationRect.height = swapChain->swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	static LARGE_INTEGER frequency = []() {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return freq;
		}();

	static LARGE_INTEGER lastFrameTime = []() {
		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		return time;
		}();

	LARGE_INTEGER currentFrameTime;
	QueryPerformanceCounter(&currentFrameTime);

	float deltaTime = static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart) / static_cast<float>(frequency.QuadPart);
	
	lastFrameTime = currentFrameTime;

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = (float)swapChain->swapChainDesc.Width;
		dispatchParameters.motionVectorScale.y = (float)swapChain->swapChainDesc.Height;
		dispatchParameters.renderSize.width = swapChain->swapChainDesc.Width;
		dispatchParameters.renderSize.height = swapChain->swapChainDesc.Height;
		
		dispatchParameters.jitterOffset.x = 0;
		dispatchParameters.jitterOffset.y = 0;

		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

#if defined(FALLOUT_POST_NG)
		dispatchParameters.cameraNear = *(float*)REL::ID(2712882).address();
		dispatchParameters.cameraFar = *(float*)REL::ID(2712883).address();
#else
		dispatchParameters.cameraNear = *(float*)REL::ID(57985).address();
		dispatchParameters.cameraFar = *(float*)REL::ID(958877).address();
#endif

		dispatchParameters.cameraFovAngleVertical = 1.0f;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(depth);
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(motionVectors);

		if (ffx::Dispatch(frameGenContext, dispatchParameters) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
		}
	}

	frameID++;
}
