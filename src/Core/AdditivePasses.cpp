#include "Core/HooksInternal.h"

#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"
#include "Features/LightLimitFix.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace CommunityShaders::Hooks
{
#if defined(FALLOUT_PRE_NG)
	namespace
	{

		constexpr const char* kPreNGDFLightZeroAdditiveSource = "LightLimitFix\\DFLightZeroAdditivePS.hlsl";
		constexpr const char* kPreNGDFLightResourceNoOpSource = "LightLimitFix\\DFLightResourceNoOpPS.hlsl";
		constexpr const char* kPreNGDFLightFullContractNoOpSource = "LightLimitFix\\DFLightFullContractNoOpPS.hlsl";
		constexpr const char* kPreNGDFLightLLFAdditiveSource = "LightLimitFix\\DFLightLLFAdditivePS.hlsl";

		std::mutex dflightZeroAdditivePassLock;
		winrt::com_ptr<ID3D11PixelShader> dflightZeroAdditivePixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightResourceNoOpPixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightFullContractNoOpPixelShader;
		winrt::com_ptr<ID3D11PixelShader> dflightLLFAdditivePixelShader;
		winrt::com_ptr<ID3D11Buffer> dflightLLFAdditiveControlsCB;
		winrt::com_ptr<ID3D11BlendState> dflightZeroAdditiveBlendState;
		winrt::com_ptr<ID3D11DepthStencilState> dflightZeroAdditiveDepthState;
		bool dflightZeroAdditivePassResourcesAttempted = false;
		bool dflightResourceNoOpPassResourcesAttempted = false;
		bool dflightFullContractNoOpPassResourcesAttempted = false;
		bool dflightLLFAdditivePassResourcesAttempted = false;
		bool dflightLLFAdditiveControlsAttempted = false;
		bool dflightResourceNoOpPassResourcesReadyLogged = false;
		bool dflightFullContractNoOpPassResourcesReadyLogged = false;
		bool dflightLLFAdditivePassResourcesReadyLogged = false;
		bool dflightAdditiveDrawStatesAttempted = false;
		std::size_t dflightResourceNoOpPixelShaderBytecodeSize = 0;
		std::size_t dflightFullContractNoOpPixelShaderBytecodeSize = 0;
		std::size_t dflightLLFAdditivePixelShaderBytecodeSize = 0;
		std::atomic_uint32_t dflightZeroAdditivePassDrawCount = 0;
		std::atomic_uint32_t dflightResourceNoOpPassDrawCount = 0;
		std::atomic_uint32_t dflightFullContractNoOpPassDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameOrdinal = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameDrawCount = 0;
		std::atomic_uint32_t dflightLLFAdditivePassFrameBudgetSkipCount = 0;
		std::atomic_bool dflightZeroAdditivePassLimitLogged = false;
		std::atomic_bool dflightResourceNoOpPassLimitLogged = false;
		std::atomic_bool dflightFullContractNoOpPassLimitLogged = false;
		std::atomic_bool dflightLLFAdditivePassLimitLogged = false;

		constexpr std::uint32_t kMaxDFLightZeroAdditivePassDraws = 8;
		constexpr std::uint32_t kMaxDFLightResourceNoOpPassDraws = 8;
		constexpr std::uint32_t kMaxDFLightFullContractNoOpPassDraws = 8;
		constexpr std::uint32_t kDefaultDFLightLLFAdditivePassDraws = 8;
		constexpr std::uint32_t kMinDFLightLLFAdditivePassDraws = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassDraws = 4096;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassDrawLogs = 32;
		constexpr std::uint32_t kDFLightLLFAdditivePassDrawLogInterval = 256;
		constexpr std::uint32_t kDefaultDFLightLLFAdditivePassFrameDraws = 1;
		constexpr std::uint32_t kMinDFLightLLFAdditivePassFrameDraws = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditivePassFrameDraws = 8;
		constexpr UINT kDFLightLLFAdditiveControlsCBSlot = 13;
		constexpr std::uint32_t kDefaultDFLightLLFAdditiveScale1024 = 64;
		constexpr std::uint32_t kMinDFLightLLFAdditiveScale1024 = 0;
		constexpr std::uint32_t kMaxDFLightLLFAdditiveScale1024 = 512;
		constexpr std::uint32_t kDefaultDFLightLLFAdditiveMaxLights = 32;
		constexpr std::uint32_t kMinDFLightLLFAdditiveMaxLights = 1;
		constexpr std::uint32_t kMaxDFLightLLFAdditiveMaxLights = 64;

		struct alignas(16) DFLightLLFAdditiveControls
		{
			float scale = static_cast<float>(kDefaultDFLightLLFAdditiveScale1024) / 1024.0f;
			std::uint32_t maxLights = kDefaultDFLightLLFAdditiveMaxLights;
			std::uint32_t flags = 0;
			std::uint32_t pad = 0;
		};
		static_assert(sizeof(DFLightLLFAdditiveControls) == 16);





		DFLightLLFAdditiveControls GetPreNGDFLightLLFAdditiveControls()
		{
			DFLightLLFAdditiveControls controls{};
			controls.scale = static_cast<float>(GetPreNGDFLightLLFAdditiveScale1024()) / 1024.0f;
			controls.maxLights = GetPreNGDFLightLLFAdditiveMaxLights();
			return controls;
		}

		bool TryReservePreNGDFLightLLFAdditivePassFrameDraw()
		{
			const auto frameBudget = GetPreNGDFLightLLFAdditivePassFrameBudget();
			auto current = dflightLLFAdditivePassFrameDrawCount.load(std::memory_order_relaxed);
			while (current < frameBudget) {
				if (dflightLLFAdditivePassFrameDrawCount.compare_exchange_weak(
						current,
						current + 1,
						std::memory_order_relaxed,
						std::memory_order_relaxed)) {
					return true;
				}
			}

			const auto skipIndex = dflightLLFAdditivePassFrameBudgetSkipCount.fetch_add(1, std::memory_order_relaxed) + 1;
			if (skipIndex <= 8 || skipIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass per-frame draw budget reached skips={} frame={} frameBudget={} env={}",
					skipIndex,
					dflightLLFAdditivePassFrameOrdinal.load(std::memory_order_relaxed),
					frameBudget,
					kPreNGDFLightLLFAdditiveFrameBudgetEnv);
			}
			return false;
		}


		bool ShouldLogPreNGDFLightLLFAdditivePassDraw(std::uint32_t a_drawIndex, std::uint32_t a_drawLimit, bool a_persistent)
		{
			const auto oneBasedDrawIndex = a_drawIndex + 1;
			return oneBasedDrawIndex <= kMaxDFLightLLFAdditivePassDrawLogs ||
			       (!a_persistent && oneBasedDrawIndex == a_drawLimit) ||
			       (kDFLightLLFAdditivePassDrawLogInterval > 0 &&
			           oneBasedDrawIndex % kDFLightLLFAdditivePassDrawLogInterval == 0);
		}

		bool EnsurePreNGDFLightAdditiveNoOpDrawStates(ID3D11Device* a_device, const char* a_label)
		{
			if (!a_device) {
				return false;
			}

			if (dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				return true;
			}
			if (dflightAdditiveDrawStatesAttempted) {
				return false;
			}
			dflightAdditiveDrawStatesAttempted = true;

			D3D11_BLEND_DESC blendDesc{};
			auto& targetBlend = blendDesc.RenderTarget[0];
			targetBlend.BlendEnable = TRUE;
			targetBlend.SrcBlend = D3D11_BLEND_ONE;
			targetBlend.DestBlend = D3D11_BLEND_ONE;
			targetBlend.BlendOp = D3D11_BLEND_OP_ADD;
			targetBlend.SrcBlendAlpha = D3D11_BLEND_ZERO;
			targetBlend.DestBlendAlpha = D3D11_BLEND_ONE;
			targetBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			targetBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			auto hr = a_device->CreateBlendState(&blendDesc, dflightZeroAdditiveBlendState.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight {} additive blend state create failed hr=0x{:08X} pass=held",
					a_label,
					static_cast<std::uint32_t>(hr));
				return false;
			}

			D3D11_DEPTH_STENCIL_DESC depthDesc{};
			depthDesc.DepthEnable = FALSE;
			depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
			depthDesc.StencilEnable = FALSE;
			depthDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			depthDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			depthDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			depthDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			depthDesc.BackFace = depthDesc.FrontFace;

			hr = a_device->CreateDepthStencilState(&depthDesc, dflightZeroAdditiveDepthState.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight {} additive depth state create failed hr=0x{:08X} pass=held",
					a_label,
					static_cast<std::uint32_t>(hr));
				return false;
			}

			return true;
		}

		bool EnsurePreNGDFLightZeroAdditivePassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightZeroAdditivePixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				return true;
			}
			if (dflightZeroAdditivePassResourcesAttempted) {
				return false;
			}
			dflightZeroAdditivePassResourcesAttempted = true;

			auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
				kPreNGDFLightZeroAdditiveSource,
				"ps_5_0",
				nullptr,
				"main");
			if (!bytecode) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight zero-additive pass shader compile failed source={} pass=held",
					kPreNGDFLightZeroAdditiveSource);
				return false;
			}

			auto hr = createPixelShader ?
				createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightZeroAdditivePixelShader.put()) :
				a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightZeroAdditivePixelShader.put());
			if (FAILED(hr)) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight zero-additive pass pixel shader create failed hr=0x{:08X} pass=held",
					static_cast<std::uint32_t>(hr));
				return false;
			}
			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "zero-additive pass")) {
				return false;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight zero-additive pass resources ready source={} bytecode={}",
				kPreNGDFLightZeroAdditiveSource,
				bytecode->size());
			return true;
		}

		bool EnsurePreNGDFLightResourceNoOpPassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightResourceNoOpPixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				if (!dflightResourceNoOpPassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight resource no-op pass resources ready source={} bytecode={}",
						kPreNGDFLightResourceNoOpSource,
						dflightResourceNoOpPixelShaderBytecodeSize);
					dflightResourceNoOpPassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightResourceNoOpPixelShader) {
				if (dflightResourceNoOpPassResourcesAttempted) {
					return false;
				}
				dflightResourceNoOpPassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightResourceNoOpSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight resource no-op pass shader compile failed source={} pass=held",
						kPreNGDFLightResourceNoOpSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightResourceNoOpPixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightResourceNoOpPixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight resource no-op pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightResourceNoOpPixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "resource no-op pass")) {
				return false;
			}
			if (!dflightResourceNoOpPassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight resource no-op pass resources ready source={} bytecode={}",
					kPreNGDFLightResourceNoOpSource,
					dflightResourceNoOpPixelShaderBytecodeSize);
				dflightResourceNoOpPassResourcesReadyLogged = true;
			}
			return true;
		}

		bool EnsurePreNGDFLightFullContractNoOpPassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightFullContractNoOpPixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState) {
				if (!dflightFullContractNoOpPassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight full contract no-op pass resources ready source={} bytecode={}",
						kPreNGDFLightFullContractNoOpSource,
						dflightFullContractNoOpPixelShaderBytecodeSize);
					dflightFullContractNoOpPassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightFullContractNoOpPixelShader) {
				if (dflightFullContractNoOpPassResourcesAttempted) {
					return false;
				}
				dflightFullContractNoOpPassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightFullContractNoOpSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight full contract no-op pass shader compile failed source={} pass=held",
						kPreNGDFLightFullContractNoOpSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightFullContractNoOpPixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightFullContractNoOpPixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight full contract no-op pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightFullContractNoOpPixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "full contract no-op pass")) {
				return false;
			}
			if (!dflightFullContractNoOpPassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight full contract no-op pass resources ready source={} bytecode={}",
					kPreNGDFLightFullContractNoOpSource,
					dflightFullContractNoOpPixelShaderBytecodeSize);
				dflightFullContractNoOpPassResourcesReadyLogged = true;
			}
			return true;
		}

		bool EnsurePreNGDFLightLLFAdditivePassResources(ID3D11Device* a_device)
		{
			if (!a_device) {
				return false;
			}

			std::scoped_lock lock(dflightZeroAdditivePassLock);
			if (dflightLLFAdditivePixelShader && dflightZeroAdditiveBlendState && dflightZeroAdditiveDepthState && dflightLLFAdditiveControlsCB) {
				if (!dflightLLFAdditivePassResourcesReadyLogged) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass resources ready source={} bytecode={}",
						kPreNGDFLightLLFAdditiveSource,
						dflightLLFAdditivePixelShaderBytecodeSize);
					dflightLLFAdditivePassResourcesReadyLogged = true;
				}
				return true;
			}

			if (!dflightLLFAdditivePixelShader) {
				if (dflightLLFAdditivePassResourcesAttempted) {
					return false;
				}
				dflightLLFAdditivePassResourcesAttempted = true;

				auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
					kPreNGDFLightLLFAdditiveSource,
					"ps_5_0",
					nullptr,
					"main");
				if (!bytecode) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass shader compile failed source={} pass=held",
						kPreNGDFLightLLFAdditiveSource);
					return false;
				}

				const auto hr = createPixelShader ?
					createPixelShader(a_device, bytecode->data(), bytecode->size(), nullptr, dflightLLFAdditivePixelShader.put()) :
					a_device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, dflightLLFAdditivePixelShader.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass pixel shader create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
				dflightLLFAdditivePixelShaderBytecodeSize = bytecode->size();
			}

			if (!EnsurePreNGDFLightAdditiveNoOpDrawStates(a_device, "LLF additive pass")) {
				return false;
			}
			if (!dflightLLFAdditiveControlsCB) {
				if (dflightLLFAdditiveControlsAttempted) {
					return false;
				}
				dflightLLFAdditiveControlsAttempted = true;

				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(DFLightLLFAdditiveControls);
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

				const auto controls = GetPreNGDFLightLLFAdditiveControls();
				D3D11_SUBRESOURCE_DATA initialData{};
				initialData.pSysMem = &controls;
				const auto hr = a_device->CreateBuffer(&desc, &initialData, dflightLLFAdditiveControlsCB.put());
				if (FAILED(hr)) {
					logger::warn(
						"[LightLimitFix] PreNG DFLight LLF additive pass controls CB create failed hr=0x{:08X} pass=held",
						static_cast<std::uint32_t>(hr));
					return false;
				}
			}
			if (!dflightLLFAdditivePassResourcesReadyLogged) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass resources ready source={} bytecode={}",
					kPreNGDFLightLLFAdditiveSource,
					dflightLLFAdditivePixelShaderBytecodeSize);
				dflightLLFAdditivePassResourcesReadyLogged = true;
			}
			return true;
		}
	}
	bool ShouldRunPreNGDFLightZeroAdditivePass()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightZeroAdditivePassEnv);
		return enabled;
	}
	bool ShouldRunPreNGDFLightResourceNoOpPass()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
		return enabled;
	}
	bool ShouldRunPreNGDFLightFullContractNoOpPass()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
		return enabled;
	}
	bool ShouldRunPreNGDFLightLLFAdditivePass()
	{
		static const bool enabled = [] {
			const bool requested = ReadPreNGEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
			const bool legacyProofAllowed = ReadPreNGEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
			if (requested && !legacyProofAllowed) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight LLF additive pass held in draw hooks; this legacy proof path is not the Skyrim-CS LLF direction. Set {}=1 only to reproduce the old additive proof.",
					kPreNGDFLightLegacyAdditiveProofEnv);
			} else if (requested) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight LLF additive legacy proof enabled in draw hooks; do not use this path for final LLF validation.");
			}
			return requested && legacyProofAllowed;
		}();
		return enabled;
	}
	bool ShouldPersistPreNGDFLightLLFAdditivePass()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightLLFAdditivePersistentEnv);
		return ShouldRunPreNGDFLightLLFAdditivePass() && enabled;
	}
	std::uint32_t GetPreNGDFLightLLFAdditivePassDrawBudget()
	{
		static const auto budget = []() {
			const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveBudgetEnv);
			auto resolved = configured.value_or(kDefaultDFLightLLFAdditivePassDraws);
			if (resolved < kMinDFLightLLFAdditivePassDraws) {
				resolved = kMinDFLightLLFAdditivePassDraws;
			} else if (resolved > kMaxDFLightLLFAdditivePassDraws) {
				resolved = kMaxDFLightLLFAdditivePassDraws;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive pass draw budget resolved budget={} configured={} default={} min={} max={} env={}",
				resolved,
				configured ? std::to_string(*configured) : std::string("unset"),
				kDefaultDFLightLLFAdditivePassDraws,
				kMinDFLightLLFAdditivePassDraws,
				kMaxDFLightLLFAdditivePassDraws,
				kPreNGDFLightLLFAdditiveBudgetEnv);
			return resolved;
		}();
		return budget;
	}
	std::uint32_t GetPreNGDFLightLLFAdditivePassFrameBudget()
	{
		static const auto budget = []() {
			const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveFrameBudgetEnv);
			auto resolved = configured.value_or(kDefaultDFLightLLFAdditivePassFrameDraws);
			if (resolved < kMinDFLightLLFAdditivePassFrameDraws) {
				resolved = kMinDFLightLLFAdditivePassFrameDraws;
			} else if (resolved > kMaxDFLightLLFAdditivePassFrameDraws) {
				resolved = kMaxDFLightLLFAdditivePassFrameDraws;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive pass per-frame draw budget resolved frameBudget={} configured={} default={} min={} max={} env={}",
				resolved,
				configured ? std::to_string(*configured) : std::string("unset"),
				kDefaultDFLightLLFAdditivePassFrameDraws,
				kMinDFLightLLFAdditivePassFrameDraws,
				kMaxDFLightLLFAdditivePassFrameDraws,
				kPreNGDFLightLLFAdditiveFrameBudgetEnv);
			return resolved;
		}();
		return budget;
	}
	std::uint32_t GetPreNGDFLightLLFAdditiveScale1024()
	{
		static const auto scale1024 = []() {
			const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveScale1024Env);
			auto resolved = configured.value_or(kDefaultDFLightLLFAdditiveScale1024);
			if (resolved < kMinDFLightLLFAdditiveScale1024) {
				resolved = kMinDFLightLLFAdditiveScale1024;
			} else if (resolved > kMaxDFLightLLFAdditiveScale1024) {
				resolved = kMaxDFLightLLFAdditiveScale1024;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive pass scale resolved scale1024={} configured={} default={} min={} max={} env={}",
				resolved,
				configured ? std::to_string(*configured) : std::string("unset"),
				kDefaultDFLightLLFAdditiveScale1024,
				kMinDFLightLLFAdditiveScale1024,
				kMaxDFLightLLFAdditiveScale1024,
				kPreNGDFLightLLFAdditiveScale1024Env);
			return resolved;
		}();
		return scale1024;
	}
	std::uint32_t GetPreNGDFLightLLFAdditiveMaxLights()
	{
		static const auto maxLights = []() {
			const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightLLFAdditiveMaxLightsEnv);
			auto resolved = configured.value_or(kDefaultDFLightLLFAdditiveMaxLights);
			if (resolved < kMinDFLightLLFAdditiveMaxLights) {
				resolved = kMinDFLightLLFAdditiveMaxLights;
			} else if (resolved > kMaxDFLightLLFAdditiveMaxLights) {
				resolved = kMaxDFLightLLFAdditiveMaxLights;
			}

			logger::info(
				"[LightLimitFix] PreNG DFLight LLF additive pass max lights resolved maxLights={} configured={} default={} min={} max={} env={}",
				resolved,
				configured ? std::to_string(*configured) : std::string("unset"),
				kDefaultDFLightLLFAdditiveMaxLights,
				kMinDFLightLLFAdditiveMaxLights,
				kMaxDFLightLLFAdditiveMaxLights,
				kPreNGDFLightLLFAdditiveMaxLightsEnv);
			return resolved;
		}();
		return maxLights;
	}
	void AdvancePreNGDFLightLLFAdditivePassFrame()
	{
		if (!ShouldRunPreNGDFLightLLFAdditivePass()) {
			return;
		}

		dflightLLFAdditivePassFrameOrdinal.fetch_add(1, std::memory_order_relaxed);
		dflightLLFAdditivePassFrameDrawCount.store(0, std::memory_order_relaxed);
	}
	void RunPreNGDFLightZeroAdditivePass(
		ID3D11DeviceContext* a_context,
		DrawIndexedFn a_originalDrawIndexed,
		UINT a_indexCount,
		UINT a_startIndexLocation,
		INT a_baseVertexLocation)
	{
		if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightZeroAdditivePass()) {
			return;
		}

		const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
		if (!metadata) {
			return;
		}

		winrt::com_ptr<ID3D11Device> device;
		a_context->GetDevice(device.put());
		if (!device || !EnsurePreNGDFLightZeroAdditivePassResources(device.get())) {
			return;
		}

		const auto drawIndex = dflightZeroAdditivePassDrawCount.fetch_add(1);
		if (drawIndex >= kMaxDFLightZeroAdditivePassDraws) {
			if (!dflightZeroAdditivePassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight zero-additive pass draw limit reached limit={}",
					kMaxDFLightZeroAdditivePassDraws);
			}
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
		UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
		a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

		winrt::com_ptr<ID3D11BlendState> oldBlendState;
		FLOAT oldBlendFactor[4]{};
		UINT oldSampleMask = 0;
		a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

		winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
		UINT oldStencilRef = 0;
		a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

		FLOAT additiveBlendFactor[4]{};
		a_context->PSSetShader(dflightZeroAdditivePixelShader.get(), nullptr, 0);
		a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
		a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

		a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);

		a_context->PSSetShader(
			oldPixelShader.get(),
			oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
			oldClassInstanceCount);
		a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
		a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);

		for (UINT index = 0; index < oldClassInstanceCount; ++index) {
			if (oldClassInstances[index]) {
				oldClassInstances[index]->Release();
			}
		}

		logger::info(
			"[LightLimitFix] PreNG DFLight zero-additive pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			drawIndex + 1,
			a_indexCount,
			a_startIndexLocation,
			a_baseVertexLocation,
			ToAddress(a_context),
			GetContextVTablePointer(a_context));
	}
	void RunPreNGDFLightResourceNoOpPass(
		ID3D11DeviceContext* a_context,
		DrawIndexedFn a_originalDrawIndexed,
		UINT a_indexCount,
		UINT a_startIndexLocation,
		INT a_baseVertexLocation)
	{
		if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightResourceNoOpPass()) {
			return;
		}

		const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
		if (!metadata || !globals::features::lightLimitFix.loaded) {
			return;
		}

		winrt::com_ptr<ID3D11Device> device;
		a_context->GetDevice(device.put());
		if (!device || !EnsurePreNGDFLightResourceNoOpPassResources(device.get())) {
			return;
		}
		if (dflightResourceNoOpPassDrawCount.load(std::memory_order_relaxed) >= kMaxDFLightResourceNoOpPassDraws) {
			if (!dflightResourceNoOpPassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight resource no-op pass draw limit reached limit={}",
					kMaxDFLightResourceNoOpPassDraws);
			}
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
		UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
		a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

		winrt::com_ptr<ID3D11BlendState> oldBlendState;
		FLOAT oldBlendFactor[4]{};
		UINT oldSampleMask = 0;
		a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

		winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
		UINT oldStencilRef = 0;
		a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

		winrt::com_ptr<ID3D11Buffer> oldStrictCB;
		a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
		std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
		a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		FLOAT additiveBlendFactor[4]{};
		a_context->PSSetShader(dflightResourceNoOpPixelShader.get(), nullptr, 0);
		a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
		a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

		const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightResourceNoOpPass(a_context);
		if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
			const auto drawIndex = dflightResourceNoOpPassDrawCount.fetch_add(1);
			if (drawIndex < kMaxDFLightResourceNoOpPassDraws) {
				a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				logger::info(
					"[LightLimitFix] PreNG DFLight resource no-op pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					drawIndex + 1,
					a_indexCount,
					a_startIndexLocation,
					a_baseVertexLocation,
					ToAddress(a_context),
					GetContextVTablePointer(a_context),
					resourceState.lightCount,
					resourceState.strictLightCount,
					resourceState.shadowBitMask);
			} else if (!dflightResourceNoOpPassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight resource no-op pass draw limit reached limit={}",
					kMaxDFLightResourceNoOpPassDraws);
			}
		}

		a_context->PSSetShader(
			oldPixelShader.get(),
			oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
			oldClassInstanceCount);
		a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
		a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
		ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
		a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
		a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		for (UINT index = 0; index < oldClassInstanceCount; ++index) {
			if (oldClassInstances[index]) {
				oldClassInstances[index]->Release();
			}
		}
		for (auto* oldSRV : oldClusterSRVs) {
			if (oldSRV) {
				oldSRV->Release();
			}
		}
	}
	void RunPreNGDFLightFullContractNoOpPass(
		ID3D11DeviceContext* a_context,
		DrawIndexedFn a_originalDrawIndexed,
		UINT a_indexCount,
		UINT a_startIndexLocation,
		INT a_baseVertexLocation)
	{
		if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightFullContractNoOpPass()) {
			return;
		}

		const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
		if (!metadata || !globals::features::lightLimitFix.loaded) {
			return;
		}

		winrt::com_ptr<ID3D11Device> device;
		a_context->GetDevice(device.put());
		if (!device || !EnsurePreNGDFLightFullContractNoOpPassResources(device.get())) {
			return;
		}
		if (dflightFullContractNoOpPassDrawCount.load(std::memory_order_relaxed) >= kMaxDFLightFullContractNoOpPassDraws) {
			if (!dflightFullContractNoOpPassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight full contract no-op pass draw limit reached limit={}",
					kMaxDFLightFullContractNoOpPassDraws);
			}
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
		UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
		a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

		winrt::com_ptr<ID3D11BlendState> oldBlendState;
		FLOAT oldBlendFactor[4]{};
		UINT oldSampleMask = 0;
		a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

		winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
		UINT oldStencilRef = 0;
		a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

		winrt::com_ptr<ID3D11Buffer> oldStrictCB;
		a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
		std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
		a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		FLOAT additiveBlendFactor[4]{};
		a_context->PSSetShader(dflightFullContractNoOpPixelShader.get(), nullptr, 0);
		a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
		a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

		const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightFullContractNoOpPass(a_context);
		if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
			const auto drawIndex = dflightFullContractNoOpPassDrawCount.fetch_add(1);
			if (drawIndex < kMaxDFLightFullContractNoOpPassDraws) {
				a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				logger::info(
					"[LightLimitFix] PreNG DFLight full contract no-op pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					drawIndex + 1,
					a_indexCount,
					a_startIndexLocation,
					a_baseVertexLocation,
					ToAddress(a_context),
					GetContextVTablePointer(a_context),
					resourceState.lightCount,
					resourceState.strictLightCount,
					resourceState.shadowBitMask);
			} else if (!dflightFullContractNoOpPassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight full contract no-op pass draw limit reached limit={}",
					kMaxDFLightFullContractNoOpPassDraws);
			}
		}

		a_context->PSSetShader(
			oldPixelShader.get(),
			oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
			oldClassInstanceCount);
		a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
		a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
		ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
		a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
		a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		for (UINT index = 0; index < oldClassInstanceCount; ++index) {
			if (oldClassInstances[index]) {
				oldClassInstances[index]->Release();
			}
		}
		for (auto* oldSRV : oldClusterSRVs) {
			if (oldSRV) {
				oldSRV->Release();
			}
		}
	}
	void RunPreNGDFLightLLFAdditivePass(
		ID3D11DeviceContext* a_context,
		DrawIndexedFn a_originalDrawIndexed,
		UINT a_indexCount,
		UINT a_startIndexLocation,
		INT a_baseVertexLocation)
	{
		if (!a_context || !a_originalDrawIndexed || !ShouldRunPreNGDFLightLLFAdditivePass()) {
			return;
		}

		const auto drawLimit = GetPreNGDFLightLLFAdditivePassDrawBudget();
		const bool persistent = ShouldPersistPreNGDFLightLLFAdditivePass();
		const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
		if (!metadata || !globals::features::lightLimitFix.loaded) {
			return;
		}

		if (!persistent && dflightLLFAdditivePassDrawCount.load(std::memory_order_relaxed) >= drawLimit) {
			if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
					drawLimit);
			}
			return;
		}

		if (!TryReservePreNGDFLightLLFAdditivePassFrameDraw()) {
			return;
		}

		winrt::com_ptr<ID3D11Device> device;
		a_context->GetDevice(device.put());
		if (!device || !EnsurePreNGDFLightLLFAdditivePassResources(device.get())) {
			return;
		}
		if (!persistent && dflightLLFAdditivePassDrawCount.load(std::memory_order_relaxed) >= drawLimit) {
			if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
					drawLimit);
			}
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> oldPixelShader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> oldClassInstances{};
		UINT oldClassInstanceCount = static_cast<UINT>(oldClassInstances.size());
		a_context->PSGetShader(oldPixelShader.put(), oldClassInstances.data(), &oldClassInstanceCount);

		winrt::com_ptr<ID3D11BlendState> oldBlendState;
		FLOAT oldBlendFactor[4]{};
		UINT oldSampleMask = 0;
		a_context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

		winrt::com_ptr<ID3D11DepthStencilState> oldDepthState;
		UINT oldStencilRef = 0;
		a_context->OMGetDepthStencilState(oldDepthState.put(), &oldStencilRef);

		winrt::com_ptr<ID3D11Buffer> oldStrictCB;
		a_context->PSGetConstantBuffers(3, 1, oldStrictCB.put());
		winrt::com_ptr<ID3D11Buffer> oldControlsCB;
		a_context->PSGetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, oldControlsCB.put());
		std::array<ID3D11ShaderResourceView*, 3> oldClusterSRVs{};
		a_context->PSGetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		FLOAT additiveBlendFactor[4]{};
		a_context->PSSetShader(dflightLLFAdditivePixelShader.get(), nullptr, 0);
		a_context->OMSetBlendState(dflightZeroAdditiveBlendState.get(), additiveBlendFactor, 0xFFFFFFFFu);
		a_context->OMSetDepthStencilState(dflightZeroAdditiveDepthState.get(), 0);

		const auto controls = GetPreNGDFLightLLFAdditiveControls();
		a_context->UpdateSubresource(dflightLLFAdditiveControlsCB.get(), 0, nullptr, &controls, 0, 0);
		ID3D11Buffer* controlsCB = dflightLLFAdditiveControlsCB.get();
		a_context->PSSetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, &controlsCB);

		const auto resourceState = globals::features::lightLimitFix.BindPreNGDFLightLLFAdditivePass(a_context);
		if (resourceState.strictCBBound && resourceState.clusterSRVsBound) {
			const auto drawIndex = dflightLLFAdditivePassDrawCount.fetch_add(1);
			if (persistent || drawIndex < drawLimit) {
				a_originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				if (ShouldLogPreNGDFLightLLFAdditivePassDraw(drawIndex, drawLimit, persistent)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight LLF additive pass draw asmHash=0x{:08X} hash=0x{:08X} uid={} drawIndex={} indexCount={} startIndex={} baseVertex={} context=0x{:X} vtable=0x{:X} lights={} strict={} shadowMask=0x{:08X}",
						metadata->asmHash,
						metadata->hash,
						metadata->uid,
						drawIndex + 1,
						a_indexCount,
						a_startIndexLocation,
						a_baseVertexLocation,
						ToAddress(a_context),
						GetContextVTablePointer(a_context),
						resourceState.lightCount,
						resourceState.strictLightCount,
						resourceState.shadowBitMask);
				}
			} else if (!dflightLLFAdditivePassLimitLogged.exchange(true)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight LLF additive pass draw limit reached limit={}",
					drawLimit);
			}
		}

		a_context->PSSetShader(
			oldPixelShader.get(),
			oldClassInstanceCount > 0 ? oldClassInstances.data() : nullptr,
			oldClassInstanceCount);
		a_context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
		a_context->OMSetDepthStencilState(oldDepthState.get(), oldStencilRef);
		ID3D11Buffer* oldStrictCBRaw = oldStrictCB.get();
		a_context->PSSetConstantBuffers(3, 1, &oldStrictCBRaw);
		ID3D11Buffer* oldControlsCBRaw = oldControlsCB.get();
		a_context->PSSetConstantBuffers(kDFLightLLFAdditiveControlsCBSlot, 1, &oldControlsCBRaw);
		a_context->PSSetShaderResources(35, static_cast<UINT>(oldClusterSRVs.size()), oldClusterSRVs.data());

		for (UINT index = 0; index < oldClassInstanceCount; ++index) {
			if (oldClassInstances[index]) {
				oldClassInstances[index]->Release();
			}
		}
		for (auto* oldSRV : oldClusterSRVs) {
			if (oldSRV) {
				oldSRV->Release();
			}
		}
	}
#endif
}
