#include "Upscaler.h"

#include "Core/DebugSwitches.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcompiler.h>
#include <RE/FO4Runtime.h>

#include "DX12SwapChain.h"
#include "RuntimeAdapter.h"
#include "DirectXMath.h"
#include "FidelityFX.h"
#include "Streamline.h"

void InstallUpscalerRenderBackendHooks();
namespace
{
	uint64_t NextHUDLessFrameID()
	{
		static uint64_t nextFrameID = 0;
		return ++nextFrameID;
	}
}

enum class RenderTarget
{
	kFrameBuffer = 0,

	kRefractionNormal = 1,
	
	kMainPreAlpha = 2,
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

namespace
{
	struct IniSource
	{
		std::filesystem::path path;
		const char* label;
	};

	bool LoadIniIfExists(CSimpleIniA& ini, const IniSource& source, const char* component)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source.path, ec)) {
			return false;
		}

		if (ini.LoadFile(source.path.string().c_str()) < 0) {
			logger::warn("[{}] Failed to load {} settings from {}", component, source.label, source.path.string());
			return false;
		}

		logger::debug("[{}] Loaded {} settings from {}", component, source.label, source.path.string());
		return true;
	}

	int ClampIntSetting(int value, int minValue, int maxValue, const char* settingName)
	{
		const int clamped = std::clamp(value, minValue, maxValue);
		if (clamped != value) {
			logger::warn("[Upscaler] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	float NormalizeRealFrameRateLimit(float value, const char* settingName)
	{
		if (!std::isfinite(value) || value < 0.0f) {
			logger::warn("[FrameGen] {}={} is invalid, using automatic policy", settingName, value);
			return 0.0f;
		}
		if (value == 0.0f)
			return 0.0f;

		const float clamped = std::clamp(value, 30.0f, 1000.0f);
		if (clamped != value) {
			logger::warn("[FrameGen] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	std::string FormatRealFrameRate(float value)
	{
		if (value == 0.0f)
			return "0";
		if (std::floor(value) == value)
			return std::format("{:.0f}", value);
		return std::format("{:.3f}", value);
	}

	bool IsFrameGenPluginVisible()
	{
		std::error_code ec;
		if (std::filesystem::exists("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.dll", ec))
			return true;
		if (GetModuleHandleW(L"NuclearGFX.dll") != nullptr)
			return true;

		HMODULE currentModule = nullptr;
		if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&IsFrameGenPluginVisible),
				&currentModule)) {
			std::array<wchar_t, 4096> modulePath{};
			const auto length = GetModuleFileNameW(currentModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (length > 0 && length < modulePath.size()) {
				auto fileName = std::filesystem::path(modulePath.data(), modulePath.data() + length).filename().wstring();
				if (_wcsicmp(fileName.c_str(), L"CommunityShaders.dll") == 0)
					return true;
			}
		}

		return false;
	}

	bool IsLoadingMenuOpen()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			return ui->GetMenuOpen("LoadingMenu");
		}
		return false;
	}

	std::filesystem::path GetModuleDirectory(HMODULE module)
	{
		std::array<wchar_t, 4096> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
	}

	std::filesystem::path GetCurrentPluginDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentPluginDirectory),
				&module)) {
			return {};
		}

		return GetModuleDirectory(module);
	}

	bool HasStreamlineInterposer()
	{
		std::error_code ec;
		const auto exists = [&](const std::filesystem::path& directory) {
			return !directory.empty() && std::filesystem::exists(directory / L"sl.interposer.dll", ec);
		};

		if (exists(GetCurrentPluginDirectory() / L"Streamline")) {
			return true;
		}
		ec.clear();

		if (exists(GetModuleDirectory(nullptr) / L"Data" / L"F4SE" / L"Plugins" / L"Streamline")) {
			return true;
		}

		return false;
	}

#ifdef FO4CS_ENABLE_DEBUG_SETTINGS
	constexpr bool kDebugSettingsSupported = true;
#else
	constexpr bool kDebugSettingsSupported = false;
#endif

	void LoadSharedDebugSettings(Upscaling::Settings& settings, const CSimpleIniA& ini)
	{
		if constexpr (kDebugSettingsSupported) {
			settings.debugLogging = ini.GetBoolValue("Settings", "bDebugLogging", settings.debugLogging);
			settings.streamlineLogLevel = ClampIntSetting(
				static_cast<int>(ini.GetLongValue("Settings", "iStreamlineLogLevel", settings.streamlineLogLevel)),
				0,
				2,
				"iStreamlineLogLevel");
			settings.debugFrameLogCount = ClampIntSetting(
				static_cast<int>(ini.GetLongValue("Settings", "iDebugFrameLogCount", settings.debugFrameLogCount)),
				0,
				600,
				"iDebugFrameLogCount");
		} else {
			settings.debugLogging = false;
			settings.streamlineLogLevel = 0;
			settings.debugFrameLogCount = 0;
		}
	}

	void ApplyDebugEnvironmentOverrides(Upscaling::Settings& settings)
	{
		if constexpr (kDebugSettingsSupported) {
			if (CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_DEBUG_LOG")) {
				settings.debugLogging = true;
			}
			settings.streamlineLogLevel = CommunityShaders::DebugSwitches::ReadUIntClamped(
				"FO4CS_STREAMLINE_LOG_LEVEL", settings.streamlineLogLevel, 0, 2);
			settings.debugFrameLogCount = CommunityShaders::DebugSwitches::ReadUIntClamped(
				"FO4CS_DEBUG_FRAMES", settings.debugFrameLogCount, 0, 600);
		}
	}

	void ConfigureDebugLogging(const Upscaling::Settings& settings)
	{
		if (!settings.debugLogging) {
			return;
		}

		spdlog::set_level(spdlog::level::debug);
		if (auto log = spdlog::default_logger()) {
			log->flush_on(spdlog::level::warn);
		}
	}
}

ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program = "main")
{
	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Compiler setup
	uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	winrt::com_ptr<ID3DBlob> shaderBlob;
	winrt::com_ptr<ID3DBlob> shaderErrors;

	std::string str;
	std::wstring path{ FilePath };
	std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
		return (char)c;
	});
	if (!std::filesystem::exists(FilePath)) {
		logger::error("Failed to compile shader; {} does not exist", str);
		return nullptr;
	}
	if (FAILED(D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, shaderBlob.put(), shaderErrors.put()))) {
		logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
		return nullptr;
	}
	if (shaderErrors)
		logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

	ID3D11ComputeShader* regShader;
	DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
	return regShader;
}

ID3D11DeviceChild* CompileFrameGenerationShader(const wchar_t* fileName, const char* programType, const char* program = "main")
{
	static constexpr std::array<std::wstring_view, 1> shaderDirectories{
		L"Data\\F4SE\\Plugins\\FrameGen"
	};

	for (const auto directory : shaderDirectories) {
		const auto path = std::filesystem::path(directory) / fileName;
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) {
			return CompileShader(path.c_str(), programType, program);
		}
	}

	logger::error("[FrameGen] Failed to compile shader; {} was not found in FrameGen", std::filesystem::path(fileName).string());
	return nullptr;
}

void Upscaling::LoadFrameGenerationSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.ini"), "default" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Frame Generation") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[FrameGen] Settings file not found, using defaults");
	}

	settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", true);
	settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", true);
	settings.realFrameRateLimit = NormalizeRealFrameRateLimit(
		static_cast<float>(ini.GetDoubleValue("Settings", "fRealFrameRateLimit", settings.realFrameRateLimit)),
		"fRealFrameRateLimit");
	settings.frameGenerationBackend = ClampIntSetting(
		static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", kFrameGenerationBackendDLSS)),
		0,
		kFrameGenerationBackendFSR,
		"iFrameGenerationBackend");
	LoadSharedDebugSettings(settings, ini);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);
	ApplyRuntimeFallbacks();
}

void Upscaling::LoadReflexSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\Reflex\\Reflex.ini"), "default" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Reflex") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[Reflex] Settings file not found, using defaults");
	}

	settings.reflexMode = ClampIntSetting(
		static_cast<int>(ini.GetLongValue("Settings", "iReflexMode", settings.reflexMode)),
		0,
		2,
		"iReflexMode");
	settings.reflexSleepMode = ini.GetBoolValue("Settings", "bReflexSleepMode", settings.reflexSleepMode);
	LoadSharedDebugSettings(settings, ini);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);
	ApplyRuntimeFallbacks();

	logger::info(
		"[Settings] Reflex(mode={}, sleep={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.reflexMode,
		settings.reflexSleepMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

void Upscaling::LoadSettings()
{
	LoadFrameGenerationSettings();

	if (pluginMode == PluginMode::kUpscaler && !IsFrameGenPluginVisible()) {
		settings.frameGenerationMode = false;
		settings.frameLimitMode = false;
		logger::info("[FrameGen] FrameGen.dll is not visible; disabling frame generation in Upscaler mode");
	}

	const std::vector<IniSource> upscalerIniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini"), "default" }
	};

	CSimpleIniA upscalerIni;
	upscalerIni.SetUnicode();

	bool loadedUpscalerSettings = false;
	for (const auto& source : upscalerIniSources) {
		loadedUpscalerSettings = LoadIniIfExists(upscalerIni, source, "Upscaler") || loadedUpscalerSettings;
	}

	if (!loadedUpscalerSettings) {
		logger::warn("[Upscaler] Settings file not found, using defaults");
	}

	settings.upscaleMethodPreference = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iUpscaleMethodPreference", settings.upscaleMethodPreference)),
		0,
		2,
		"iUpscaleMethodPreference");
	settings.qualityMode = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iQualityMode", settings.qualityMode)),
		0,
		4,
		"iQualityMode");
	settings.dlssPreset = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iDLSSPreset", settings.dlssPreset)),
		0,
		15,
		"iDLSSPreset");
	LoadSharedDebugSettings(settings, upscalerIni);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);

	ApplyRuntimeFallbacks();

	logger::info(
		"[Settings] FrameGen(enabled={}, limiter={}, realFPS={}, backend={}), Upscaler(method={}, quality={}, dlssPreset={}), Reflex(mode={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.frameGenerationMode,
		settings.frameLimitMode,
		settings.realFrameRateLimit,
		settings.frameGenerationBackend,
		settings.upscaleMethodPreference,
		settings.qualityMode,
		settings.dlssPreset,
		settings.reflexMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

void Upscaling::ApplyRuntimeFallbacks()
{
	settings.realFrameRateLimit = NormalizeRealFrameRateLimit(settings.realFrameRateLimit, "fRealFrameRateLimit");

	if (settings.frameGenerationBackend != kFrameGenerationBackendDLSS &&
		settings.frameGenerationBackend != kFrameGenerationBackendFSR) {
		settings.frameGenerationBackend = kFrameGenerationBackendDLSS;
	}

	const bool dlssUnavailable = GetDLSSUnavailableReason() != nullptr;
	if (dlssUnavailable && settings.upscaleMethodPreference == static_cast<int>(UpscaleMethod::kDLSS)) {
		settings.upscaleMethodPreference = static_cast<int>(UpscaleMethod::kFSR);
		logger::info("[Upscaler] DLSS unavailable; switching Upscaling to FSR");
	}

	if (dlssUnavailable && settings.frameGenerationBackend == kFrameGenerationBackendDLSS) {
		settings.frameGenerationBackend = kFrameGenerationBackendFSR;
		logger::info("[FrameGen] DLSS-G unavailable; switching Frame Generation to FSR FG");
	}

	if (pluginMode == PluginMode::kUpscaler && !IsFrameGenPluginVisible()) {
		if (settings.frameGenerationMode || settings.frameLimitMode) {
			logger::info("[FrameGen] FrameGen.dll is not visible; disabling frame generation in Upscaler mode");
		}
		settings.frameGenerationMode = false;
		settings.frameLimitMode = false;
	}

	if (IsPreNGRuntime() && settings.reflexMode != 0) {
		settings.reflexMode = 0;
		logger::info("[Reflex] PreNG detected; disabling Reflex");
	}
}

const char* Upscaling::GetDLSSUnavailableReason() const
{
	if (IsPreNGRuntime()) {
		return "PreNG (1.10.163) detected: DLSS is unavailable because of an engine issue. FSR is selected automatically for Frame Generation and Upscaling.";
	}

	if (!IsStreamlineRuntimeAvailable()) {
		return "NVIDIA Streamline runtime is missing (sl.interposer.dll). FSR is selected automatically for Frame Generation and Upscaling; DLSS and Reflex are unavailable.";
	}

	return nullptr;
}

bool Upscaling::IsPreNGRuntime() noexcept
{
	return fo4cs::RuntimeAdapter::Get().GetCapabilities().flavor == fo4cs::RuntimeFlavor::kPreNG;
}

bool Upscaling::IsStreamlineRuntimeAvailable()
{
	return HasStreamlineInterposer();
}

Upscaling::UpscaleMethod Upscaling::GetPreferredUpscaleMethod() const
{
	switch (static_cast<UpscaleMethod>(settings.upscaleMethodPreference)) {
	case UpscaleMethod::kFSR:
		return UpscaleMethod::kFSR;
	case UpscaleMethod::kDLSS:
		return UpscaleMethod::kDLSS;
	default:
		return UpscaleMethod::kDisabled;
	}
}

bool Upscaling::UsesDLSSUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kDLSS;
}

bool Upscaling::UsesFSRUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kFSR;
}

bool Upscaling::UsesDLSSFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case kFrameGenerationBackendDLSS:
		return true;
	case kFrameGenerationBackendFSR:
		return false;
	default:
		return false;
	}
}

bool Upscaling::UsesFSRFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case kFrameGenerationBackendDLSS:
		return false;
	case kFrameGenerationBackendFSR:
		return true;
	default:
		return false;
	}
}

bool Upscaling::UsesReflex() const
{
	return UsesDLSSFrameGeneration() || (pluginMode == PluginMode::kReflex && settings.reflexMode > 0);
}

void Upscaling::PostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleW(L"HighFPSPhysicsFix.dll") != nullptr;
	highFPSPhysicsFixIniReadable = false;
	highFPSPhysicsFixUntiesSpeed = false;
	highFPSPhysicsFixOwnLimiterActive = false;
	highFPSPhysicsFixInGameLimit = 0.0f;
	highFPSPhysicsFixExteriorLimit = 0.0f;
	highFPSPhysicsFixInteriorLimit = 0.0f;

	if (highFPSPhysicsFixLoaded) {
		constexpr auto iniPath = "Data\\F4SE\\Plugins\\HighFPSPhysicsFix.ini";
		CSimpleIniA ini;
		ini.SetUnicode();

		std::error_code ec;
		if (!std::filesystem::exists(iniPath, ec)) {
			logger::warn("[FrameGen] High FPS Physics Fix INI is not VFS-visible at {}; using conservative limiter policy", iniPath);
		} else if (ini.LoadFile(iniPath) < 0) {
			logger::warn("[FrameGen] Failed to read {}; using conservative limiter policy", iniPath);
		} else {
			const bool hasRequiredSettings =
				ini.GetValue("Main", "UntieSpeedFromFPS") &&
				ini.GetValue("Limiter", "InGameFPS") &&
				ini.GetValue("Limiter", "ExteriorFPS") &&
				ini.GetValue("Limiter", "InteriorFPS");
			if (!hasRequiredSettings) {
				logger::warn("[FrameGen] {} is missing required physics/limiter keys; using conservative limiter policy", iniPath);
			} else {
				highFPSPhysicsFixUntiesSpeed = ini.GetBoolValue("Main", "UntieSpeedFromFPS", false);
				highFPSPhysicsFixInGameLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "InGameFPS", 0.0));
				highFPSPhysicsFixExteriorLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "ExteriorFPS", 0.0));
				highFPSPhysicsFixInteriorLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "InteriorFPS", 0.0));

				highFPSPhysicsFixIniReadable =
					std::isfinite(highFPSPhysicsFixInGameLimit) &&
					std::isfinite(highFPSPhysicsFixExteriorLimit) &&
					std::isfinite(highFPSPhysicsFixInteriorLimit);
				if (!highFPSPhysicsFixIniReadable) {
					logger::warn("[FrameGen] {} contains non-finite limiter values; using conservative limiter policy", iniPath);
					highFPSPhysicsFixUntiesSpeed = false;
					highFPSPhysicsFixInGameLimit = 0.0f;
					highFPSPhysicsFixExteriorLimit = 0.0f;
					highFPSPhysicsFixInteriorLimit = 0.0f;
				}
			}
		}
	}

	highFPSPhysicsFixOwnLimiterActive =
		highFPSPhysicsFixLoaded && highFPSPhysicsFixIniReadable &&
		(highFPSPhysicsFixInGameLimit != 0.0f ||
		 highFPSPhysicsFixExteriorLimit != 0.0f ||
		 highFPSPhysicsFixInteriorLimit != 0.0f);
	ResolveFrameLimiterPolicy(true);

	renderBackendEnabled = pluginMode == PluginMode::kUpscaler &&
		((UsesDLSSUpscaling() && Streamline::GetSingleton()->featureDLSS) ||
		 (UsesFSRUpscaling() && d3d12Interop && FidelityFX::GetSingleton()->featureFSR));
	InstallHooks();
}

void Upscaling::CreateFrameGenerationResources()
{
	logger::debug("[FrameGen] Creating resources");

	if (IsLoadingMenuOpen()) {
		setupBuffers = false;
		return;
	}
	
	setupBuffers = true;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto& main = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
	if (!main.texture || !main.srView || !main.rtView || !motionVector.texture || !depth.srViewDepth) {
		static bool loggedPendingTargets = false;
		if (!loggedPendingTargets) {
			logger::warn(
				"[FrameGen] Render targets unavailable in CreateFrameGenerationResources; deferring "
				"(mainTex={}, mainSRV={}, mainRTV={}, motionTex={}, depthSRV={})",
				main.texture != nullptr,
				main.srView != nullptr,
				main.rtView != nullptr,
				motionVector.texture != nullptr,
				depth.srViewDepth != nullptr);
			loggedPendingTargets = true;
		}
		setupBuffers = false;
		return;
	}

	for (int index = 0; index < 2; index++) {
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

		// Save the render-target dimensions (kMain / internal resolution)
		// for depth + motion-vector buffers, then override for HUDLess and
		// UI buffers which DLSS-G requires at backbuffer (swap chain) size.
		const auto renderWidth = texDesc.Width;
		const auto renderHeight = texDesc.Height;
		auto dx12SwapChain = DX12SwapChain::GetSingleton();

		// ---- HUDLess, UI, reticle: backbuffer resolution ----
		if (dx12SwapChain->swapChain) {
			texDesc.Width = dx12SwapChain->swapChainDesc.Width;
			texDesc.Height = dx12SwapChain->swapChainDesc.Height;
		} else {
			texDesc.Width = renderWidth;
			texDesc.Height = renderHeight;
		}
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		HUDLessBufferShared[index] = new Texture2D(texDesc);
		HUDLessBufferShared[index]->CreateSRV(srvDesc);
		HUDLessBufferShared[index]->CreateRTV(rtvDesc);
		HUDLessBufferShared[index]->CreateUAV(uavDesc);

		uiColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		uiColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		uiColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		uiColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		reticleColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		reticleColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		// ---- Depth: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		depthBufferShared[index] = new Texture2D(texDesc);
		depthBufferShared[index]->CreateSRV(srvDesc);
		depthBufferShared[index]->CreateRTV(rtvDesc);
		depthBufferShared[index]->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC texDescMotionVector{};
		reinterpret_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&texDescMotionVector);

		// ---- Motion vectors: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = texDescMotionVector.Format;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		motionVectorBufferShared[index] = new Texture2D(texDesc);
		motionVectorBufferShared[index]->CreateSRV(srvDesc);
		motionVectorBufferShared[index]->CreateRTV(rtvDesc);
		motionVectorBufferShared[index]->CreateUAV(uavDesc);

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(HUDLessBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&HUDLessBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(depthBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&depthBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(uiColorAndAlphaBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&uiColorAndAlphaBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(motionVectorBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&motionVectorBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearRenderTargetView(HUDLessBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(depthBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(motionVectorBufferShared[index]->rtv.get(), clearColor);
		hudLessFrameValid[index] = false;
		hudLessFrameIDs[index] = 0;
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"CopyDepthToSharedBufferCS.hlsl", "cs_5_0");
	generateSharedBuffersCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"GenerateSharedBuffersCS.hlsl", "cs_5_0");
	buildUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildUIColorAndAlphaCS.hlsl", "cs_5_0");
	buildReticleUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildReticleUIColorAndAlphaCS.hlsl", "cs_5_0");
	patchHUDLessReticleCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"PatchHUDLessReticleCS.hlsl", "cs_5_0");
	denoiseUIAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"DenoiseUIAlphaCS.hlsl", "cs_5_0");
	logger::info("[FrameGen] Shared resources created (render={}x{}, hud={}x{}, copyDepthCS={})",
		depthBufferShared[0]->desc.Width,
		depthBufferShared[0]->desc.Height,
		HUDLessBufferShared[0]->desc.Width,
		HUDLessBufferShared[0]->desc.Height,
		copyDepthToSharedBufferCS != nullptr);
}

void Upscaling::PreAlpha()
{
	if (IsLoadingMenuOpen())
		return;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto& colorMain = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

	context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(colorMain.texture), reinterpret_cast<ID3D11Texture2D*>(colorPostAlpha.texture));

	CaptureHUDLessFrame();
}


bool Upscaling::CaptureHUDLessFrame()
{
	if (IsLoadingMenuOpen())
		return false;

#if defined(FALLOUT_PRE_NG)
	constexpr uint32_t frameIndex = 0;
#else
	if (!d3d12Interop)
		return false;
	const auto frameIndex = DX12SwapChain::GetSingleton()->frameIndex;
#endif
	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData)
		return false;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return false;

	if (!HUDLessBufferShared[frameIndex] || !HUDLessBufferShared[frameIndex]->resource) {
		static bool loggedMissingHUDLessSource = false;
		if (!loggedMissingHUDLessSource) {
			logger::warn("[FrameGen] HUDLess capture waiting for shared target (hudLess={})",
				HUDLessBufferShared[frameIndex] != nullptr);
			loggedMissingHUDLessSource = true;
		}
		return false;
	}

	auto* frameBufferTexture = [&]() -> ID3D11Texture2D* {
		struct Candidate
		{
			RenderTarget target;
			const char* name;
		};
		constexpr Candidate candidates[] = {
			{ RenderTarget::kFrameBuffer, "kFrameBuffer" },
			{ RenderTarget::kMain, "kMain" },
			{ RenderTarget::kMainTemp, "kMainTemp" },
			{ RenderTarget::kMainPreAlpha, "kMainPreAlpha" },
		};

		D3D11_TEXTURE2D_DESC hudLessDesc{};
		HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);

		static bool loggedCandidateState = false;
		for (const auto& candidate : candidates) {
			auto& renderTarget = rendererData->renderTargets[static_cast<uint>(candidate.target)];
			auto* texture = reinterpret_cast<ID3D11Texture2D*>(renderTarget.texture);
			if (!texture) {
				continue;
			}

			D3D11_TEXTURE2D_DESC sourceDesc{};
			texture->GetDesc(&sourceDesc);
			if (!loggedCandidateState) {
				logger::info("[FrameGen] HUDLess candidate {}: {}x{} fmt={} target={}x{} fmt={}",
					candidate.name,
					sourceDesc.Width,
					sourceDesc.Height,
					static_cast<uint32_t>(sourceDesc.Format),
					hudLessDesc.Width,
					hudLessDesc.Height,
					static_cast<uint32_t>(hudLessDesc.Format));
			}

			if (sourceDesc.Width == hudLessDesc.Width &&
				sourceDesc.Height == hudLessDesc.Height &&
				sourceDesc.Format == hudLessDesc.Format) {
				if (!loggedCandidateState) {
					logger::info("[FrameGen] HUDLess capture source selected: {}", candidate.name);
					loggedCandidateState = true;
				}
				return texture;
			}
		}

		if (!loggedCandidateState) {
			logger::warn("[FrameGen] HUDLess capture has no compatible PreNG source target");
			loggedCandidateState = true;
		}
		return nullptr;
	}();
	if (!frameBufferTexture) {
		return false;
	}
#if defined(FALLOUT_PRE_NG)
	D3D11_TEXTURE2D_DESC sourceDesc{};
	D3D11_TEXTURE2D_DESC hudLessDesc{};
	frameBufferTexture->GetDesc(&sourceDesc);
	HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);
	if (sourceDesc.Width != hudLessDesc.Width || sourceDesc.Height != hudLessDesc.Height || sourceDesc.Format != hudLessDesc.Format) {
		static bool loggedHUDLessMismatch = false;
		if (!loggedHUDLessMismatch) {
			logger::warn("[FrameGen] HUDLess capture source mismatch (src={}x{} fmt={}, dst={}x{} fmt={})",
				sourceDesc.Width,
				sourceDesc.Height,
				static_cast<uint32_t>(sourceDesc.Format),
				hudLessDesc.Width,
				hudLessDesc.Height,
				static_cast<uint32_t>(hudLessDesc.Format));
			loggedHUDLessMismatch = true;
		}
		return false;
	}
#endif

	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), frameBufferTexture);
	hudLessFrameIDs[frameIndex] = NextHUDLessFrameID();
	hudLessFrameValid[frameIndex] = true;

	static bool loggedFirstHUDLessCapture = false;
	if (!loggedFirstHUDLessCapture) {
		logger::info("[FrameGen] First HUDLess frame captured (index={}, id={})", frameIndex, hudLessFrameIDs[frameIndex]);
		loggedFirstHUDLessCapture = true;
	}
	return true;
}

void Upscaling::PostAlpha()
{
	if (IsLoadingMenuOpen())
		return;

	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;

	auto rendererData = fo4cs::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	{
		auto& colorPreAlpha = rendererData->renderTargets[(uint)RenderTarget::kMain];
		auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);

			ID3D11ShaderResourceView* views[4] = { 
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(motionVector.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth)
			};

			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[2] = { motionVectorBufferShared[dx12SwapChain->frameIndex]->uav.get(), depthBufferShared[dx12SwapChain->frameIndex]->uav.get()};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(generateSharedBuffersCS, nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		if (reticleColorAndAlphaBufferShared[frameIndex] && buildReticleUIColorAndAlphaCS) {
			const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Width) / 8.0f));
			const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Height) / 8.0f));

			ID3D11ShaderResourceView* reticleViews[2] = {
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView)
			};
			context->CSSetShaderResources(0, ARRAYSIZE(reticleViews), reticleViews);

			ID3D11UnorderedAccessView* reticleUAVs[1] = { reticleColorAndAlphaBufferShared[frameIndex]->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(reticleUAVs), reticleUAVs, nullptr);

			context->CSSetShader(buildReticleUIColorAndAlphaCS, nullptr, 0);
			context->Dispatch(dispatchX, dispatchY, 1);

			ID3D11ShaderResourceView* nullReticleViews[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(nullReticleViews), nullReticleViews);

			ID3D11UnorderedAccessView* nullReticleUAVs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullReticleUAVs), nullReticleUAVs, nullptr);

			context->CSSetShader(shader, nullptr, 0);
		}
	}
}

void Upscaling::CopyBuffersToSharedResources()
{
	if (IsLoadingMenuOpen())
		return;

#if defined(FALLOUT_PRE_NG)
	constexpr uint32_t frameIndex = 0;
#else
	if (!d3d12Interop)
		return;
#endif

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;
	auto rendererData = fo4cs::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

#if !defined(FALLOUT_PRE_NG)
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
#endif
	if (!motionVectorBufferShared[frameIndex] || !depthBufferShared[frameIndex] || !copyDepthToSharedBufferCS)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	context->CopyResource(motionVectorBufferShared[frameIndex]->resource.get(), reinterpret_cast<ID3D11Texture2D*>(motionVector.texture));

	{
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
		const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(depthBufferShared[frameIndex]->desc.Width) / 8.0f));
		const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(depthBufferShared[frameIndex]->desc.Height) / 8.0f));

		ID3D11ShaderResourceView* views[1] = { reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared[frameIndex]->uav.get() };
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

bool Upscaling::BuildUIColorAndAlphaResource(ID3D11Texture2D* a_finalFrame)
{
	if (IsLoadingMenuOpen())
		return false;

	if (!d3d12Interop || !a_finalFrame)
		return false;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!hudLessFrameValid[frameIndex] || hudLessFrameIDs[frameIndex] == 0)
		return false;
	if (!HUDLessBufferShared[frameIndex] || !uiColorAndAlphaBufferShared[frameIndex] || !reticleColorAndAlphaBufferShared[frameIndex] || !buildUIColorAndAlphaCS)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	D3D11_TEXTURE2D_DESC finalDesc{};
	a_finalFrame->GetDesc(&finalDesc);
	if (finalDesc.Width == 0 || finalDesc.Height == 0)
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC finalSrvDesc{};
	finalSrvDesc.Format = finalDesc.Format;
	finalSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	finalSrvDesc.Texture2D.MostDetailedMip = 0;
	finalSrvDesc.Texture2D.MipLevels = 1;

	winrt::com_ptr<ID3D11ShaderResourceView> finalFrameSRV;
	if (FAILED(device->CreateShaderResourceView(a_finalFrame, &finalSrvDesc, finalFrameSRV.put()))) {
		static bool loggedSRVFailure = false;
		if (!loggedSRVFailure) {
			logger::warn("[FrameGen] Could not create final-frame SRV; DLSS-G UI alpha tag is unavailable");
			loggedSRVFailure = true;
		}
		return false;
	}

	context->OMSetRenderTargets(0, nullptr, nullptr);

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Height) / 8.0f));

	ID3D11ShaderResourceView* views[3] = {
		finalFrameSRV.get(),
		HUDLessBufferShared[frameIndex]->srv.get(),
		reticleColorAndAlphaBufferShared[frameIndex]->srv.get()
	};
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(buildUIColorAndAlphaCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullViews[3] = { nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
	return true;
}

void Upscaling::DenoiseUIAlphaResource()
{
	if (IsLoadingMenuOpen())
		return;

	if (!d3d12Interop || !denoiseUIAlphaCS)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!uiColorAndAlphaBufferShared[frameIndex])
		return;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	D3D11_TEXTURE2D_DESC desc{};
	uiColorAndAlphaBufferShared[frameIndex]->resource->GetDesc(&desc);
	if (desc.Width == 0 || desc.Height == 0)
		return;

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(desc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(desc.Height) / 8.0f));

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShader(denoiseUIAlphaCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* nullShader = nullptr;
	context->CSSetShader(nullShader, nullptr, 0);
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::ResolveFrameLimiterPolicy(bool a_startup)
{
	settings.realFrameRateLimit = NormalizeRealFrameRateLimit(settings.realFrameRateLimit, "fRealFrameRateLimit");
	const bool physicsUntiedVerified =
		highFPSPhysicsFixLoaded && highFPSPhysicsFixIniReadable && highFPSPhysicsFixUntiesSpeed;

	FrameLimiterPolicy nextPolicy = FrameLimiterPolicy::kDisabled;
	float nextLimit = 0.0f;
	const char* reason = "user-override";
	if (settings.frameLimitMode) {
		if (physicsUntiedVerified && highFPSPhysicsFixOwnLimiterActive) {
			nextPolicy = FrameLimiterPolicy::kExternal;
			reason = "HighFPSPhysicsFix-own-limiter";
		} else if (physicsUntiedVerified && settings.realFrameRateLimit == 0.0f) {
			nextPolicy = FrameLimiterPolicy::kUnlimited;
			reason = "physics-untied";
		} else if (physicsUntiedVerified) {
			nextPolicy = FrameLimiterPolicy::kLimited;
			nextLimit = settings.realFrameRateLimit;
			reason = "user-real-fps";
		} else {
			nextPolicy = FrameLimiterPolicy::kLimited;
			nextLimit = settings.realFrameRateLimit > 0.0f ? std::min(settings.realFrameRateLimit, 60.0f) : 60.0f;
			reason = "physics-unverified-conservative";
		}
	}

	const bool changed =
		!frameLimiterPolicyInitialized ||
		frameLimiterPolicy != nextPolicy ||
		resolvedRealFrameRateLimit != nextLimit ||
		frameLimiterConfiguredRealFrameRateLimit != settings.realFrameRateLimit;
	if (!a_startup && !changed)
		return;

	frameLimiterPolicy = nextPolicy;
	resolvedRealFrameRateLimit = nextLimit;
	frameLimiterConfiguredRealFrameRateLimit = settings.realFrameRateLimit;
	frameLimiterPolicyInitialized = true;

	const char* resolvedMode = "disabled";
	std::string resolvedValue;
	switch (frameLimiterPolicy) {
	case FrameLimiterPolicy::kExternal:
		resolvedMode = "held";
		break;
	case FrameLimiterPolicy::kUnlimited:
		resolvedMode = "unlimited";
		break;
	case FrameLimiterPolicy::kLimited:
		resolvedValue = FormatRealFrameRate(resolvedRealFrameRateLimit);
		resolvedMode = resolvedValue.c_str();
		break;
	default:
		break;
	}

	logger::info(
		"[FrameGen] limiter policy physicsFixLoaded={} iniReadable={} untie={} externalLimiter={} configuredRealFPS={} resolved={} reason={} inGame={} exterior={} interior={}",
		highFPSPhysicsFixLoaded,
		highFPSPhysicsFixIniReadable,
		highFPSPhysicsFixUntiesSpeed,
		highFPSPhysicsFixOwnLimiterActive,
		FormatRealFrameRate(settings.realFrameRateLimit),
		resolvedMode,
		reason,
		FormatRealFrameRate(highFPSPhysicsFixInGameLimit),
		FormatRealFrameRate(highFPSPhysicsFixExteriorLimit),
		FormatRealFrameRate(highFPSPhysicsFixInteriorLimit));

	if (frameLimiterPolicy == FrameLimiterPolicy::kDisabled && !physicsUntiedVerified) {
		logger::warn("[FrameGen] bFrameLimitMode=false while physics untie is unverified; game speed safety is user-controlled");
	}
}

void Upscaling::FrameLimiter(std::uint32_t a_syncInterval)
{
	ResolveFrameLimiterPolicy(false);

	const bool vsyncHeld = a_syncInterval != 0;
	if (vsyncHeld != frameLimiterVSyncHeld) {
		if (vsyncHeld) {
			logger::info("[FrameGen] limiter held reason=vsync-present syncInterval={}", a_syncInterval);
		} else {
			logger::info("[FrameGen] limiter released reason=vsync-present syncInterval=0");
		}
		frameLimiterVSyncHeld = vsyncHeld;
	}

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	if (vsyncHeld || frameLimiterPolicy != FrameLimiterPolicy::kLimited || resolvedRealFrameRateLimit <= 0.0f) {
		lastFrame = timeNow;
		return;
	}

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	const int64_t targetFrameTicks =
		static_cast<int64_t>(static_cast<double>(qpf.QuadPart) / static_cast<double>(resolvedRealFrameRateLimit));
	const int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (lastFrame.QuadPart != 0 && delta < targetFrameTicks) {
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
	if (IsLoadingMenuOpen())
		return;

#if !defined(FALLOUT_PRE_NG)
	if (!d3d12Interop)
		return;
#endif

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;
	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		return;
	}

	auto& swapChain = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	auto* swapChainRTV = reinterpret_cast<ID3D11RenderTargetView*>(swapChain.rtView);
	if (!swapChainRTV) {
		static bool loggedMissingSwapChainRTV = false;
		if (!loggedMissingSwapChainRTV) {
			logger::warn("[FrameGen] HUDLess post-display capture waiting for frame buffer RTV");
			loggedMissingSwapChainRTV = true;
		}
		return;
	}

	winrt::com_ptr<ID3D11Resource> swapChainResource;
	swapChainRTV->GetResource(swapChainResource.put());
	if (!swapChainResource) {
		static bool loggedMissingSwapChainResource = false;
		if (!loggedMissingSwapChainResource) {
			logger::warn("[FrameGen] HUDLess post-display capture waiting for frame buffer resource");
			loggedMissingSwapChainResource = true;
		}
		return;
	}

#if defined(FALLOUT_PRE_NG)
	constexpr uint32_t frameIndex = 0;
#else
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
#endif
	if (!HUDLessBufferShared[frameIndex] || !HUDLessBufferShared[frameIndex]->resource) {
		return;
	}

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		return;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	D3D11_TEXTURE2D_DESC hudLessDesc{};
	winrt::com_ptr<ID3D11Texture2D> sourceTexture;
	if (FAILED(swapChainResource->QueryInterface(IID_PPV_ARGS(sourceTexture.put()))) || !sourceTexture) {
		static bool loggedPostDisplayNotTexture = false;
		if (!loggedPostDisplayNotTexture) {
			logger::warn("[FrameGen] HUDLess post-display source is not a Texture2D");
			loggedPostDisplayNotTexture = true;
		}
		return;
	}
	sourceTexture->GetDesc(&sourceDesc);
	HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);
	if (sourceDesc.Width != hudLessDesc.Width || sourceDesc.Height != hudLessDesc.Height || sourceDesc.Format != hudLessDesc.Format) {
		static bool loggedPostDisplayMismatch = false;
		if (!loggedPostDisplayMismatch) {
			logger::warn("[FrameGen] HUDLess post-display source mismatch (src={}x{} fmt={}, dst={}x{} fmt={})",
				sourceDesc.Width,
				sourceDesc.Height,
				static_cast<uint32_t>(sourceDesc.Format),
				hudLessDesc.Width,
				hudLessDesc.Height,
				static_cast<uint32_t>(hudLessDesc.Format));
			loggedPostDisplayMismatch = true;
		}
		return;
	}

	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), swapChainResource.get());
	hudLessFrameIDs[frameIndex] = NextHUDLessFrameID();
	hudLessFrameValid[frameIndex] = true;

	static bool loggedFirstPostDisplayHUDLessCapture = false;
	if (!loggedFirstPostDisplayHUDLessCapture) {
		logger::info("[FrameGen] First HUDLess post-display frame captured (index={}, id={})",
			frameIndex,
			hudLessFrameIDs[frameIndex]);
		loggedFirstPostDisplayHUDLessCapture = true;
	}
}

void Upscaling::Reset()
{
	if (IsLoadingMenuOpen())
		return;

	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HUDLessBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	hudLessFrameValid[dx12SwapChain->frameIndex] = false;
	hudLessFrameIDs[dx12SwapChain->frameIndex] = 0;
	if (uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	if (reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(depthBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(motionVectorBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
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
		func(This, a_true);
		if (!a_true) {
			auto* upscaling = Upscaling::GetSingleton();
			upscaling->Upscale();
			upscaling->PostDisplay();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool reticleFix = false;

struct DrawWorld_Forward
{
	static void thunk(void* a1)
	{		
		func(a1);

		if (!reticleFix)
			Upscaling::GetSingleton()->CopyBuffersToSharedResources();

		reticleFix = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct DrawWorld_Reticle
{
	static void thunk(void* a1)
	{
		auto upscaling = Upscaling::GetSingleton();
		upscaling->PreAlpha();
		func(a1);
		reticleFix = true;
		upscaling->PostAlpha();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	if (GetSingleton()->pluginMode == PluginMode::kUpscaler)
		InstallUpscalerRenderBackendHooks();

#if defined(FALLOUT_POST_NG)
	namespace F4Hooks = RE::FO4Runtime::PostNG::Hooks;
	stl::detour_thunk<WindowSizeChanged>(F4Hooks::UPSCALER_WINDOW_SIZE_CHANGED);
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(F4Hooks::UPSCALER_SET_DEFAULT_VIEWPORT_CALL.address());
	stl::detour_thunk<DrawWorld_Forward>(F4Hooks::UPSCALER_DRAW_WORLD_FORWARD);
	stl::write_thunk_call<DrawWorld_Reticle>(F4Hooks::UPSCALER_DRAW_WORLD_RETICLE_CALL.address());
#else
	namespace F4Hooks = RE::FO4Runtime::PreNG::Hooks;
	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(F4Hooks::UPSCALER_WINDOW_SIZE_CHANGED);

	// Watch frame presentation
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(F4Hooks::UPSCALER_SET_DEFAULT_VIEWPORT_CALL.address());

	// Fix reticles on motion vectors and depth
	stl::detour_thunk<DrawWorld_Forward>(F4Hooks::UPSCALER_DRAW_WORLD_FORWARD);
	stl::write_thunk_call<DrawWorld_Reticle>(F4Hooks::UPSCALER_DRAW_WORLD_RETICLE_CALL.address());
#endif

	logger::debug("[Upscaler] Installed hooks");
}
