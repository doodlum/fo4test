#include "Core/Deferred.h"

#include <d3d11.h>
#include <d3d11_1.h>

#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"
#include "Core/Feature.h"
#include "Core/ShaderCache.h"
#include "Core/State.h"
#include <RE/FO4Runtime.h>
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <Windows.h>

namespace
{
	// Check if the F4SE Address Library version file exists.
	// Without it, REL::ID resolution crashes via REX::FAIL -> std::terminate.
	// MO2 USVFS does not hook GetModuleFileNameW or directory_iterator,
	// so we first search via Win32 FindFirstFile (USVFS-compatible) in Data/F4SE/Plugins.
	[[nodiscard]] bool HasAddressLibrary()
	{
		// Method 1: Win32 API search in game Data dir (MO2 USVFS compatible)
		wchar_t exePath[MAX_PATH];
		if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
			auto searchPath = std::filesystem::path(exePath).parent_path() / "Data" / "F4SE" / "Plugins" / "version-*.bin";
			WIN32_FIND_DATAW findData;
			HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
			if (hFind != INVALID_HANDLE_VALUE) {
				FindClose(hFind);
				return true;
			}
		}

		// Method 2: fallback - check DLL directory (non-MO2 installs)
		wchar_t modulePath[MAX_PATH];
		if (GetModuleFileNameW(GetModuleHandleW(L"NuclearGFX.dll"), modulePath, MAX_PATH)) {
			std::filesystem::path pluginDir = std::filesystem::path(modulePath).parent_path();
			for (auto& entry : std::filesystem::directory_iterator(pluginDir)) {
				auto name = entry.path().filename().string();
				if (name.starts_with("version-") && name.ends_with(".bin"))
					return true;
			}
		}
		return false;
	}
}

// FO4 GBuffer layout (Creation Engine shared architecture):
//   RT20 = kGbufferNormal     — Normal + roughness
//   RT22 = kGbufferAlbedo     — Albedo (diffuse)
//   RT23 = kGbufferEmissive   — Emissive
//   RT24 = kGbufferMaterial   — Glossiness, Specular, SSS, Backlighting
//
// These slots replicate the Skyrim CS deferred pattern:
//   ALBEDO     → FO4 RT22
//   SPECULAR   → FO4 RT24 (specular channel)
//   MASKS      → FO4 RT20 (normal channel doubles as mask carrier)

namespace
{
	const Deferred::GBufferTargetBindings kGBufferTargets{ {
		{ Deferred::GBufferTarget::kNormal, RE::FO4Runtime::RenderTargetIndex::kGBufferNormal, DXGI_FORMAT_R10G10B10A2_UNORM, "NormalRoughness" },
		{ Deferred::GBufferTarget::kAlbedo, RE::FO4Runtime::RenderTargetIndex::kGBufferAlbedo, DXGI_FORMAT_R10G10B10A2_UNORM, "Albedo" },
		{ Deferred::GBufferTarget::kEmissive, RE::FO4Runtime::RenderTargetIndex::kGBufferEmissive, DXGI_FORMAT_R11G11B10_FLOAT, "Emissive" },
		{ Deferred::GBufferTarget::kMaterial, RE::FO4Runtime::RenderTargetIndex::kGBufferMaterial, DXGI_FORMAT_R11G11B10_FLOAT, "Material" },
	} };

	const Deferred::DeferredRenderTargetBindings kDeferredRenderTargets{ {
		{ 2, Deferred::GBufferTarget::kNormal, "NormalRoughness" },
		{ 3, Deferred::GBufferTarget::kAlbedo, "Albedo" },
		{ 4, Deferred::GBufferTarget::kEmissive, "Emissive" },
		{ 5, Deferred::GBufferTarget::kMaterial, "Material" },
	} };

	[[nodiscard]] std::size_t ToGBufferIndex(Deferred::GBufferTarget a_target) noexcept
	{
		return static_cast<std::size_t>(a_target);
	}

	[[nodiscard]] const Deferred::GBufferTargetBinding* GetGBufferBinding(Deferred::GBufferTarget a_target) noexcept
	{
		const auto index = ToGBufferIndex(a_target);
		return index < kGBufferTargets.size() ? &kGBufferTargets[index] : nullptr;
	}

	using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
	using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
	using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
	using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
	using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
	using DrawIndexedInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
	using DrawInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);

	DrawIndexedFn originalDrawIndexed = nullptr;
	DrawFn originalDraw = nullptr;
	DrawIndexedInstancedFn originalDrawIndexedInstanced = nullptr;
	DrawInstancedFn originalDrawInstanced = nullptr;
	DrawAutoFn originalDrawAuto = nullptr;
	DrawIndexedInstancedIndirectFn originalDrawIndexedInstancedIndirect = nullptr;
	DrawInstancedIndirectFn originalDrawInstancedIndirect = nullptr;
	std::mutex drawHookInstallLock;
	std::uintptr_t installedDrawHookVTable = 0;

	struct LightingDrawState
	{
		bool active = false;
		UINT renderTargetCount = 0;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, Deferred::kMaxBoundRenderTargetCount> renderTargets;
		winrt::com_ptr<ID3D11DepthStencilView> depthStencil;
		winrt::com_ptr<ID3D11BlendState> blendState;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
	};

	std::mutex deferredTraceLock;

	[[nodiscard]] bool IsDeferredTraceEnabled() noexcept
	{
		static const bool enabled = CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_TRACE_DEFERRED");
		return enabled;
	}

	[[nodiscard]] bool IsGBufferDumpEnabled() noexcept
	{
		static const bool enabled = CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_DUMP_GBUFFER");
		return enabled;
	}

	template <class... Args>
	void TraceDeferred(std::format_string<Args...> a_format, Args&&... a_args)
	{
		try {
			if (!IsDeferredTraceEnabled()) {
				return;
			}
			auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
			std::error_code ec;
			std::filesystem::create_directories(traceDir, ec);
			if (ec) {
				return;
			}
			auto* runtime = CommunityShaders::Runtime::GetSingleton();
			const auto frame = runtime ? runtime->GetFrameCount() : 0;
			std::scoped_lock lock(deferredTraceLock);
			std::ofstream out(traceDir / "deferred_trace.txt", std::ios::app);
			if (out) {
				out << std::format("[frame={}] {}\n", frame, std::format(a_format, std::forward<Args>(a_args)...));
			}
		} catch (...) {
			// Diagnostic output must never terminate a render hook.
		}
	}

	[[nodiscard]] std::string DescribeRTV(ID3D11RenderTargetView* a_rtv)
	{
		if (!a_rtv) {
			return "null";
		}
		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		a_rtv->GetDesc(&viewDesc);
		ID3D11Resource* resource = nullptr;
		a_rtv->GetResource(&resource);
		const auto resourceAddress = reinterpret_cast<std::uintptr_t>(resource);
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;
		if (resource) {
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				width = textureDesc.Width;
				height = textureDesc.Height;
				textureFormat = textureDesc.Format;
				texture->Release();
			}
			resource->Release();
		}
		try {
			return std::format("view=0x{:X},resource=0x{:X},viewFmt={},texFmt={},size={}x{}",
				reinterpret_cast<std::uintptr_t>(a_rtv), resourceAddress,
				static_cast<std::uint32_t>(viewDesc.Format), static_cast<std::uint32_t>(textureFormat), width, height);
		} catch (...) {
			return "<rtv-format-error>";
		}
	}

	[[nodiscard]] std::string DescribeDSV(ID3D11DepthStencilView* a_dsv)
	{
		if (!a_dsv) {
			return "null";
		}
		D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
		a_dsv->GetDesc(&viewDesc);
		try {
			return std::format("view=0x{:X},fmt={},dimension={}", reinterpret_cast<std::uintptr_t>(a_dsv),
				static_cast<std::uint32_t>(viewDesc.Format), static_cast<std::uint32_t>(viewDesc.ViewDimension));
		} catch (...) {
			return "<dsv-format-error>";
		}
	}

	void TraceOMState(ID3D11DeviceContext* a_context, std::string_view a_phase)
	{
		if (!IsDeferredTraceEnabled() || !a_context) {
			return;
		}
		std::array<ID3D11RenderTargetView*, Deferred::kMaxBoundRenderTargetCount> rtvs{};
		ID3D11DepthStencilView* dsv = nullptr;
		a_context->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
		ID3D11BlendState* blend = nullptr;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		a_context->OMGetBlendState(&blend, blendFactor.data(), &sampleMask);
		TraceDeferred("om phase={} dsv={} blend=0x{:X} sampleMask=0x{:X} blendFactor=[{:.3f},{:.3f},{:.3f},{:.3f}]",
			a_phase, DescribeDSV(dsv), reinterpret_cast<std::uintptr_t>(blend), sampleMask,
			blendFactor[0], blendFactor[1], blendFactor[2], blendFactor[3]);
		for (std::size_t i = 0; i < rtvs.size(); ++i) {
			TraceDeferred("om phase={} rtv[{}] {}", a_phase, i, DescribeRTV(rtvs[i]));
		}
		for (auto* rtv : rtvs) {
			if (rtv) {
				rtv->Release();
			}
		}
		if (dsv) {
			dsv->Release();
		}
		if (blend) {
			blend->Release();
		}
	}

	void TraceRestoreCheck(ID3D11DeviceContext* a_context, const LightingDrawState& a_state)
	{
		if (!IsDeferredTraceEnabled() || !a_context) {
			return;
		}
		std::array<ID3D11RenderTargetView*, Deferred::kMaxBoundRenderTargetCount> rtvs{};
		ID3D11DepthStencilView* dsv = nullptr;
		a_context->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
		ID3D11BlendState* blend = nullptr;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		a_context->OMGetBlendState(&blend, blendFactor.data(), &sampleMask);
		bool match = dsv == a_state.depthStencil.get() && blend == a_state.blendState.get() && sampleMask == a_state.sampleMask;
		for (std::size_t i = 0; i < rtvs.size(); ++i) {
			match = match && rtvs[i] == a_state.renderTargets[i].get();
		}
		for (std::size_t i = 0; i < blendFactor.size(); ++i) {
			match = match && blendFactor[i] == a_state.blendFactor[i];
		}
		TraceDeferred("restore check match={} expectedRTCount={} actualDSV=0x{:X} actualBlend=0x{:X}", match,
			a_state.renderTargetCount, reinterpret_cast<std::uintptr_t>(dsv), reinterpret_cast<std::uintptr_t>(blend));
		for (auto* rtv : rtvs) {
			if (rtv) {
				rtv->Release();
			}
		}
		if (dsv) {
			dsv->Release();
		}
		if (blend) {
			blend->Release();
		}
	}

	void DumpGBufferSnapshot()
	{
		static std::atomic_bool dumped = false;
		if (!IsGBufferDumpEnabled() || !Deferred::GetSingleton()->AreGBufferResourcesReady() || dumped.exchange(true)) {
			return;
		}
		auto* rendererData = fo4cs::GetRendererData();
		if (!rendererData || !rendererData->device || !rendererData->context) {
			TraceDeferred("gbuffer dump skipped missing renderer device/context");
			return;
		}
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);
		if (ec) {
			TraceDeferred("gbuffer dump skipped create_directories error={}", ec.message());
			return;
		}
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		for (const auto& binding : kGBufferTargets) {
			auto* texture = Deferred::GetSingleton()->GetGBufferTexture(binding.target);
			if (!texture) {
				TraceDeferred("gbuffer dump target={} skipped missing texture", binding.name);
				continue;
			}
			D3D11_TEXTURE2D_DESC sourceDesc{};
			texture->GetDesc(&sourceDesc);
			if (sourceDesc.SampleDesc.Count != 1) {
				TraceDeferred("gbuffer dump target={} skipped multisample count={}", binding.name, sourceDesc.SampleDesc.Count);
				continue;
			}
			D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			winrt::com_ptr<ID3D11Texture2D> staging;
			const auto createResult = device->CreateTexture2D(&stagingDesc, nullptr, staging.put());
			if (FAILED(createResult)) {
				TraceDeferred("gbuffer dump target={} CreateTexture2D failed hr=0x{:08X}", binding.name, static_cast<std::uint32_t>(createResult));
				continue;
			}
			context->CopyResource(staging.get(), texture);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto mapResult = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
			if (FAILED(mapResult)) {
				TraceDeferred("gbuffer dump target={} Map failed hr=0x{:08X}", binding.name, static_cast<std::uint32_t>(mapResult));
				continue;
			}
			const auto stem = std::format("gbuffer_{}_frame_{}", binding.name, frame);
			std::ofstream out(traceDir / (stem + ".bin"), std::ios::binary);
			if (out) {
				for (UINT row = 0; row < sourceDesc.Height; ++row) {
					out.write(static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, mapped.RowPitch);
				}
			}
			context->Unmap(staging.get(), 0);
			std::ofstream metadata(traceDir / (stem + ".txt"));
			if (metadata) {
				metadata << std::format("target={} rendererIndex={} format={} width={} height={} rowPitch={} frame={}\n",
					binding.name, binding.rendererTargetIndex, static_cast<std::uint32_t>(sourceDesc.Format), sourceDesc.Width,
					sourceDesc.Height, mapped.RowPitch, frame);
			}
			TraceDeferred("gbuffer dump target={} path={} format={} size={}x{} rowPitch={}", binding.name,
				(traceDir / (stem + ".bin")).string(), static_cast<std::uint32_t>(sourceDesc.Format), sourceDesc.Width, sourceDesc.Height, mapped.RowPitch);
		}
	}

	thread_local LightingDrawState lightingDrawState;

	class LightingDrawGuard
	{
	public:
		explicit LightingDrawGuard(ID3D11DeviceContext* a_context) :
			context(a_context),
			active(Deferred::GetSingleton()->BeginLightingDraw(a_context))
		{}

		~LightingDrawGuard()
		{
			if (active) {
				Deferred::GetSingleton()->EndLightingDraw(context);
			}
		}

	private:
		ID3D11DeviceContext* context;
		bool active;
	};

	class DeferredWorldEpochGuard
	{
	public:
		explicit DeferredWorldEpochGuard(Deferred* a_deferred) :
			deferred(a_deferred)
		{}

		~DeferredWorldEpochGuard()
		{
			deferred->EndDeferred();
		}

	private:
		Deferred* deferred;
	};

	void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation)
	{
		LightingDrawGuard guard(a_context);
		originalDrawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
	}

	void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation)
	{
		LightingDrawGuard guard(a_context);
		originalDraw(a_context, a_vertexCount, a_startVertexLocation);
	}

	void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation)
	{
		LightingDrawGuard guard(a_context);
		originalDrawIndexedInstanced(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, a_startInstanceLocation);
	}

	void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance, UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
	{
		LightingDrawGuard guard(a_context);
		originalDrawInstanced(a_context, a_vertexCountPerInstance, a_instanceCount, a_startVertexLocation, a_startInstanceLocation);
	}

	void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* a_context)
	{
		LightingDrawGuard guard(a_context);
		originalDrawAuto(a_context);
	}

	void STDMETHODCALLTYPE DrawIndexedInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
	{
		LightingDrawGuard guard(a_context);
		originalDrawIndexedInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
	}

	void STDMETHODCALLTYPE DrawInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
	{
		LightingDrawGuard guard(a_context);
		originalDrawInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
	}
}

const Deferred::GBufferTargetBindings& Deferred::GetGBufferTargetBindings() noexcept
{
	return kGBufferTargets;
}

const Deferred::DeferredRenderTargetBindings& Deferred::GetDeferredRenderTargetBindings() noexcept
{
	return kDeferredRenderTargets;
}

Deferred::ShaderLookupDescriptorState Deferred::BuildShaderLookupDescriptorState(
	const RE::BSShader& a_shader,
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_forceDeferred) const noexcept
{
	ShaderLookupDescriptorState state{};
	state.shaderType = a_shader.shaderType;
	state.originalVertexDescriptor = a_vertexDescriptor;
	state.originalPixelDescriptor = a_pixelDescriptor;
	state.vertexDescriptor = a_vertexDescriptor;
	state.pixelDescriptor = a_pixelDescriptor;
	state.deferredRequested = a_forceDeferred || deferredPass;
	state.reason = state.deferredRequested ? "unsupported-shader-type" : "forward-pass";

	if (!state.deferredRequested) {
		return state;
	}
	if (!a_forceDeferred && !gBufferResourcesReady) {
		state.reason = "gbuffer-not-ready";
		return state;
	}

	if (RE::FO4Runtime::IsLightingShaderType(a_shader.shaderType)) {
		state.deferredSupported = true;
		state.pixelDescriptor = RE::FO4Runtime::AddDeferredLightingPixelDescriptor(state.pixelDescriptor);
		state.modified =
			state.vertexDescriptor != state.originalVertexDescriptor ||
			state.pixelDescriptor != state.originalPixelDescriptor;
		state.reason = state.modified ? "lighting-deferred-flag-added" : "lighting-deferred-flag-present";
	}

	return state;
}

D3D11_TEXTURE2D_DESC Deferred::GetGBufferDesc(GBufferTarget a_target) const noexcept
{
	const auto index = ToGBufferIndex(a_target);
	return index < gBufferDescriptions.size() ? gBufferDescriptions[index] : D3D11_TEXTURE2D_DESC{};
}

ID3D11Texture2D* Deferred::GetGBufferTexture(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[binding->rendererTargetIndex].texture);
}

ID3D11ShaderResourceView* Deferred::GetGBufferSRV(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[binding->rendererTargetIndex].srView);
}

ID3D11RenderTargetView* Deferred::GetGBufferRTV(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11RenderTargetView*>(rendererData->renderTargets[binding->rendererTargetIndex].rtView);
}

void Deferred::SetupResources()
{
	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData || !rendererData->device) {
		gBufferResourcesReady = false;
		return;
	}

	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	InstallDrawHooks(reinterpret_cast<ID3D11DeviceContext*>(rendererData->context));
	auto createSampler = [&](D3D11_FILTER a_filter, const char* a_name, winrt::com_ptr<ID3D11SamplerState>& a_sampler) {
		if (a_sampler) {
			return true;
		}

		D3D11_SAMPLER_DESC desc{};
		desc.Filter = a_filter;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0.0f;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		const auto hr = device->CreateSamplerState(&desc, a_sampler.put());
		if (FAILED(hr)) {
			logger::warn("[Deferred] CreateSamplerState({}) failed hr=0x{:08X}", a_name, static_cast<std::uint32_t>(hr));
			return false;
		}

		return true;
	};

	bool ready = createSampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, "linear", linearSampler) &&
	             createSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, "point", pointSampler);

	for (const auto& binding : kGBufferTargets) {
		auto* texture = GetGBufferTexture(binding.target);
		auto* srv = GetGBufferSRV(binding.target);
		auto* rtv = GetGBufferRTV(binding.target);
		if (!texture || !srv || !rtv) {
			ready = false;
			continue;
		}

			texture->GetDesc(&gBufferDescriptions[ToGBufferIndex(binding.target)]);
			if (IsDeferredTraceEnabled()) {
				const auto& desc = gBufferDescriptions[ToGBufferIndex(binding.target)];
				TraceDeferred("gbuffer target={} rendererIndex={} expectedFmt={} actualFmt={} size={}x{} texture=0x{:X} srv=0x{:X} rtv={}",
					binding.name, binding.rendererTargetIndex, static_cast<std::uint32_t>(binding.expectedFormat),
					static_cast<std::uint32_t>(desc.Format), desc.Width, desc.Height,
					reinterpret_cast<std::uintptr_t>(texture), reinterpret_cast<std::uintptr_t>(srv), DescribeRTV(rtv));
			}
		}

	const bool wasReady = gBufferResourcesReady;
	gBufferResourcesReady = ready;

	static bool loggedPending = false;
	if (gBufferResourcesReady) {
		loggedPending = false;
		if (!wasReady) {
			const auto normalDesc = GetGBufferDesc(GBufferTarget::kNormal);
			logger::info(
				"[Deferred] GBuffer resources ready targets={} size={}x{} linearSampler={} pointSampler={}",
				kGBufferTargets.size(),
				normalDesc.Width,
				normalDesc.Height,
				linearSampler != nullptr,
				pointSampler != nullptr);
		}
	} else if (!loggedPending) {
		logger::warn("[Deferred] GBuffer resources pending; deferred consumers remain disabled until renderer targets are available");
		loggedPending = true;
	}
}

void Deferred::InstallDrawHooks(ID3D11DeviceContext* a_context)
{
	if (!a_context) {
		return;
	}

	std::scoped_lock lock(drawHookInstallLock);
	const auto vtable = *reinterpret_cast<std::uintptr_t*>(a_context);
	if (!vtable || installedDrawHookVTable == vtable) {
		return;
	}
	if (installedDrawHookVTable != 0) {
		logger::warn(
			"[Deferred] D3D11 draw hooks already own vtable=0x{:X}; refusing second vtable=0x{:X}",
			installedDrawHookVTable,
			vtable);
		return;
	}

	originalDrawIndexed = reinterpret_cast<DrawIndexedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedHook, 12));
	originalDraw = reinterpret_cast<DrawFn>(Detours::X64::DetourClassVTable(vtable, &DrawHook, 13));
	originalDrawIndexedInstanced = reinterpret_cast<DrawIndexedInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedHook, 20));
	originalDrawInstanced = reinterpret_cast<DrawInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedHook, 21));
	originalDrawAuto = reinterpret_cast<DrawAutoFn>(Detours::X64::DetourClassVTable(vtable, &DrawAutoHook, 38));
	originalDrawIndexedInstancedIndirect = reinterpret_cast<DrawIndexedInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedIndirectHook, 39));
	originalDrawInstancedIndirect = reinterpret_cast<DrawInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedIndirectHook, 40));

	if (!originalDrawIndexed || !originalDraw || !originalDrawIndexedInstanced || !originalDrawInstanced ||
		!originalDrawAuto || !originalDrawIndexedInstancedIndirect || !originalDrawInstancedIndirect) {
		logger::critical("[Deferred] Failed to install the complete D3D11 draw hook set");
		return;
	}

	installedDrawHookVTable = vtable;
	logger::info("[Deferred] Shared D3D11 draw-scoped MRT hooks installed vtable=0x{:X}", vtable);
}

void Deferred::RegisterLightingPixelShader(ID3D11PixelShader* a_shader)
{
	if (!a_shader) {
		return;
	}

	std::unique_lock lock(lightingShaderLock);
	if (lightingPixelShaders.contains(a_shader)) {
		return;
	}

	winrt::com_ptr<ID3D11PixelShader> retained;
	retained.copy_from(a_shader);
	lightingPixelShaders.emplace(a_shader, std::move(retained));
	hasLightingPixelShaders.store(true, std::memory_order_release);
	logger::debug(
		"[Deferred] Registered lighting pixel shader ps=0x{:X} total={}",
		reinterpret_cast<std::uintptr_t>(a_shader),
		lightingPixelShaders.size());
	TraceDeferred("lighting shader registered ps=0x{:X} total={}",
		reinterpret_cast<std::uintptr_t>(a_shader), lightingPixelShaders.size());
}

bool Deferred::IsRegisteredLightingPixelShader(ID3D11PixelShader* a_shader) const
{
	if (!a_shader) {
		return false;
	}

	std::shared_lock lock(lightingShaderLock);
	return lightingPixelShaders.contains(a_shader);
}

bool Deferred::BeginLightingDraw(ID3D11DeviceContext* a_context)
{
	if (!a_context || lightingDrawState.active || !IsDeferredPassActive() || !gBufferResourcesReady ||
		!hasLightingPixelShaders.load(std::memory_order_acquire)) {
		return false;
	}

	winrt::com_ptr<ID3D11PixelShader> pixelShader;
	a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
	if (!IsRegisteredLightingPixelShader(pixelShader.get())) {
		return false;
	}

	std::array<ID3D11RenderTargetView*, kMaxBoundRenderTargetCount> currentRTVs{};
	ID3D11DepthStencilView* currentDSV = nullptr;
	a_context->OMGetRenderTargets(static_cast<UINT>(currentRTVs.size()), currentRTVs.data(), &currentDSV);

	if (!currentRTVs[0]) {
		for (auto* rtv : currentRTVs) {
			if (rtv) {
				rtv->Release();
			}
		}
		if (currentDSV) {
			currentDSV->Release();
		}
		return false;
	}

	auto& state = lightingDrawState;
	state.renderTargetCount = 0;
	for (std::size_t i = 0; i < currentRTVs.size(); ++i) {
		state.renderTargets[i].attach(currentRTVs[i]);
		if (currentRTVs[i]) {
			state.renderTargetCount = static_cast<UINT>(i + 1);
		}
	}
	state.depthStencil.attach(currentDSV);

	ID3D11BlendState* currentBlendState = nullptr;
	a_context->OMGetBlendState(&currentBlendState, state.blendFactor.data(), &state.sampleMask);
	state.blendState.attach(currentBlendState);
	state.active = true;
	TraceDeferred("lighting begin shader=0x{:X} originalRTCount={} originalDSV={} originalBlend=0x{:X}",
		reinterpret_cast<std::uintptr_t>(pixelShader.get()), state.renderTargetCount,
		DescribeDSV(state.depthStencil.get()), reinterpret_cast<std::uintptr_t>(state.blendState.get()));
	TraceOMState(a_context, "before");

	std::array<ID3D11RenderTargetView*, kDeferredRenderTargetCount> deferredRTVs{};
	for (std::size_t i = 0; i < kForwardRenderTargetPreserveCount; ++i) {
		deferredRTVs[i] = state.renderTargets[i].get();
	}

	for (const auto& binding : kDeferredRenderTargets) {
		if (binding.outputSlot >= deferredRTVs.size()) {
			EndLightingDraw(a_context);
			return false;
		}

		auto* rtv = GetGBufferRTV(binding.target);
		if (!rtv) {
			EndLightingDraw(a_context);
			return false;
		}

		deferredRTVs[binding.outputSlot] = rtv;
	}

	a_context->OMSetRenderTargets(
		static_cast<UINT>(deferredRTVs.size()),
		deferredRTVs.data(),
		state.depthStencil.get());

	if (auto* mrtBlendState = GetOrCreateMRTBlendState(state.blendState.get()); mrtBlendState != state.blendState.get()) {
		a_context->OMSetBlendState(mrtBlendState, state.blendFactor.data(), state.sampleMask);
	}
	TraceOMState(a_context, "override");
	if (IsDeferredTraceEnabled()) {
		if (winrt::com_ptr<ID3DUserDefinedAnnotation> annotation; SUCCEEDED(a_context->QueryInterface(IID_PPV_ARGS(annotation.put()))) && annotation) {
			annotation->BeginEvent(L"FO4CS Deferred Lighting MRT");
			state.annotation = std::move(annotation);
		}
	}

	static std::atomic_bool loggedReady = false;
	if (!loggedReady.exchange(true, std::memory_order_relaxed)) {
		logger::info(
			"[Deferred] First draw-scoped MRT bind forwardSlots={} gbufferSlots={} boundTargets={} depthStencil={}",
			kForwardRenderTargetPreserveCount,
			kDeferredRenderTargets.size(),
			kDeferredRenderTargetCount,
			state.depthStencil != nullptr);
	}
	return true;
}

void Deferred::EndLightingDraw(ID3D11DeviceContext* a_context) noexcept
{
	auto& state = lightingDrawState;
	if (!a_context || !state.active) {
		return;
	}
	if (state.annotation) {
		state.annotation->EndEvent();
		state.annotation = nullptr;
	}

	std::array<ID3D11RenderTargetView*, kMaxBoundRenderTargetCount> restoreRTVs{};
	for (std::size_t i = 0; i < restoreRTVs.size(); ++i) {
		restoreRTVs[i] = state.renderTargets[i].get();
	}
	a_context->OMSetRenderTargets(
		state.renderTargetCount,
		state.renderTargetCount > 0 ? restoreRTVs.data() : nullptr,
		state.depthStencil.get());
	a_context->OMSetBlendState(state.blendState.get(), state.blendFactor.data(), state.sampleMask);
	TraceOMState(a_context, "restore");
	TraceRestoreCheck(a_context, state);
	TraceDeferred("lighting end restoredRTCount={} restoredDSV={} restoredBlend=0x{:X}",
		state.renderTargetCount, DescribeDSV(state.depthStencil.get()), reinterpret_cast<std::uintptr_t>(state.blendState.get()));

	state.active = false;
	state.renderTargetCount = 0;
	for (auto& rtv : state.renderTargets) {
		rtv = nullptr;
	}
	state.depthStencil = nullptr;
	state.blendState = nullptr;
}

void Deferred::ReflectionsPrepasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("ReflectionsPrepass", [](Feature* a_feature) {
		try { a_feature->ReflectionsPrepass(); } catch (...) {}
	});
}

void Deferred::EarlyPrepasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("EarlyPrepass", [](Feature* a_feature) {
		try { a_feature->EarlyPrepass(); } catch (...) {}
	});
}

void Deferred::PrepassPasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("Prepass", [](Feature* a_feature) {
		try {
			a_feature->Prepass();
		} catch (const std::exception& e) {
			logger::error("[Deferred] Feature '{}' Prepass exception: {}", a_feature->GetName(), e.what());
		} catch (...) {
			logger::error("[Deferred] Feature '{}' Prepass unknown exception", a_feature->GetName());
		}
	});
}

void Deferred::StartDeferred()
{
	if (!gBufferResourcesReady) {
		SetupResources();
	}

	deferredPass = true;
	std::size_t registeredShaderCount = 0;
	{
		std::scoped_lock lock(lightingShaderLock);
		registeredShaderCount = lightingPixelShaders.size();
	}
	TraceDeferred("epoch begin context=0x{:X} gbufferReady={} registeredShaders={}",
		reinterpret_cast<std::uintptr_t>(fo4cs::GetRendererData() ? fo4cs::GetRendererData()->context : nullptr),
		gBufferResourcesReady, registeredShaderCount);
	try {
		PrepassPasses();
	} catch (const std::exception& e) {
		logger::error("[Deferred] StartDeferred exception: {}", e.what());
	} catch (...) {
		logger::error("[Deferred] StartDeferred unknown exception");
	}
}

void Deferred::EndDeferred()
{
	try {
		DeferredPasses();
	} catch (const std::exception& e) {
		logger::error("[Deferred] EndDeferred exception: {}", e.what());
	} catch (...) {
		logger::error("[Deferred] EndDeferred unknown exception");
	}

	deferredPass.store(false, std::memory_order_release);
	try {
		DumpGBufferSnapshot();
	} catch (...) {
		logger::error("[Deferred] GBuffer diagnostic dump failed");
	}
	TraceDeferred("epoch end");
}

void Deferred::DeferredPasses()
{
	// Deferred composite pass — dispatches to Features.
	// Full implementation will:
	//   1. Copy main + normal-roughness to backup RTs
	//   2. Bind GBuffer SRVs (t0-t5: main, spec, normal, depth, albedo, masks)
	//   3. Bind lighting/reflection SRVs from loaded Features
	//   4. Composite PS with interior/exterior variant
	//   5. Draw fullscreen triangle (3 verts, no input layout)
	//
	// Feature dispatch:
	//   SSGI → screenSpaceGI.DrawSSGI()
	//   SSS  → subsurfaceScattering.DrawSSS()
	//   Cubemaps → dynamicCubemaps.UpdateCubemap()
	//
	// After composite:
	//   dynamicCubemaps.PostDeferred()
}

ID3D11BlendState* Deferred::GetOrCreateMRTBlendState(ID3D11BlendState* a_original)
{
	if (!a_original) return nullptr;

	std::scoped_lock lock(blendStateLock);

	auto it = blendStateCache.find(a_original);
	if (it != blendStateCache.end())
		return it->second ? it->second.get() : a_original;

	D3D11_BLEND_DESC desc;
	a_original->GetDesc(&desc);

	if (desc.IndependentBlendEnable) {
		blendStateCache[a_original].attach(nullptr);  // mark as already MRT
		return a_original;
	}

	// Extend: copy RT[0] blend settings to RTs [1..7]
	desc.IndependentBlendEnable = TRUE;
	for (int i = 1; i < 8; i++) {
		desc.RenderTarget[i] = desc.RenderTarget[0];
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData || !rendererData->device) return a_original;

	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!device) return a_original;

	winrt::com_ptr<ID3D11BlendState> extended;
	if (FAILED(device->CreateBlendState(&desc, extended.put()))) {
		logger::warn("[Deferred] Failed to create MRT-extended blend state");
		return a_original;
	}

	blendStateCache[a_original] = extended;
	logger::info("[Deferred] Created MRT blend state for 0x{:X} → 0x{:X}",
	             reinterpret_cast<std::uintptr_t>(a_original),
	             reinterpret_cast<std::uintptr_t>(extended.get()));
	return extended.get();
}

void Deferred::ClearShaderCache()
{
	std::scoped_lock lock(lightingShaderLock, blendStateLock);
	lightingPixelShaders.clear();
	hasLightingPixelShaders.store(false, std::memory_order_release);
	blendStateCache.clear();
}

	// --- Hook 触发验证日志 ---
	// 将 hook 名称和当前 shader 计数写入 hook_fire_log.txt，
	// 由 Python 验证脚本与 pipeline_trace.txt 交叉对比。
	void LogHookFire(const char* a_hookName)
	{
		auto shaderCount = CommunityShaders::ShaderCache::GetSingleton()->GetShaderCreationCount();
		auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "ShaderDump" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);
		auto logFile = traceDir / "hook_fire_log.txt";
		std::ofstream out(logFile, std::ios::app);
		auto frameCount = CommunityShaders::Runtime::GetSingleton()->GetFrameCount();
		out << std::format("[HOOK] {} at frame={} shader_index={}\n", a_hookName, frameCount, shaderCount);
	}

	// --- Hooks into FO4 Creation Engine render pipeline ---
	// REL::IDs resolved via cross-reference with F4SE Address Library + decompiled export.
	// Cross-version analysis: PreNG 1.10.163, PostNG 1.10.984, PostAE 1.11.221 (2026-07-21).
	// See .codex/docs/static-deferred-scope-cross-version.md for the static-analysis report.

// Detour a function at a raw absolute address (bypasses REL::ID resolution).
// Template type T provides T::thunk and T::func (REL::Relocation stored as uintptr_t).
template <class T>
static void detour_thunk_at(uintptr_t a_address) {
	*(uintptr_t*)&T::func = Detours::X64::DetourFunction(a_address, (uintptr_t)&T::thunk);
}

void Deferred::Hooks::Install()
{
	// Load the version-*.bin Address Library directly.
	// Returns the RVA offset for a given ID, or std::nullopt if not found.
	// Direct .bin access avoids REL::IDDB::id2offset() which calls TerminateProcess on miss.
	[[nodiscard]] static auto GetRELOffset = [](std::uint64_t a_id) -> std::optional<std::uint64_t> {
		struct mapping_t { std::uint64_t id; std::uint64_t offset; };
		static std::vector<mapping_t> s_id2offset;

		if (s_id2offset.empty()) {
#if defined(FALLOUT_POST_AE)
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
#else
				const auto version = REL::Module::get().version();
#endif
#if defined(FALLOUT_POST_NG)
			const auto path = std::format("Data/F4SE/Plugins/version-{}.bin", version.string("-"sv));
#else
			const auto path = std::format("Data/F4SE/Plugins/version-{}.bin", version.string());
#endif
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				logger::warn("[Deferred] Failed to open {}", path);
				return std::nullopt;
			}
			std::uint64_t count = 0;
			file.read(reinterpret_cast<char*>(&count), sizeof(count));
			if (!file || count == 0 || count > 5000000) {
				logger::warn("[Deferred] Invalid .bin count: {}", count);
				return std::nullopt;
			}
			s_id2offset.resize(count);
			file.read(reinterpret_cast<char*>(s_id2offset.data()), count * sizeof(mapping_t));
			if (!file) {
				logger::warn("[Deferred] Failed to read .bin entries");
				return std::nullopt;
			}
		}

		const mapping_t elem{ a_id, 0 };
		const auto it = std::lower_bound(
			s_id2offset.begin(), s_id2offset.end(), elem,
			[](auto& a_lhs, auto& a_rhs) { return a_lhs.id < a_rhs.id; });
		if (it != s_id2offset.end() && it->id == a_id)
			return it->offset;
		return std::nullopt;
	};



	if (!HasAddressLibrary()) {
		logger::info("[Deferred] Address Library not found — skipping pipeline hooks");
		return;
	}
#if defined(FALLOUT_POST_NG)
	namespace F4Hooks = RE::FO4Runtime::PostNG::Hooks;

	// World_Start is the only PostNG/PostAE entry point required by Deferred: it
	// bounds the world epoch while the D3D11 draw hooks provide the narrow MRT
	// scope. Do not detour Main_RenderWorld here; Upscaler owns the same REL ID
	// (2318315), and stacking two entry detours corrupts the trampoline chain.
	// ShadowMaps has no PostNG/PostAE EarlyPrepass consumer, while BlendedDecals
	// and Renderer_Begin were diagnostic-only hooks.
	const auto worldStartID = F4Hooks::DEFERRED_MAIN_RENDER_WORLD_START.id();
	if (!GetRELOffset(worldStartID).has_value()) {
		logger::warn(
			"[Deferred] REL::ID({}) not in address library — World_Start hook skipped",
			worldStartID);
	} else {
		logger::info("[Deferred] Found REL::ID({}) for Main_RenderWorld_Start hook", worldStartID);
		stl::detour_thunk<Main_RenderWorld_Start>(F4Hooks::DEFERRED_MAIN_RENDER_WORLD_START);
		logger::info(
			"[Deferred] PostNG/PostAE World_Start epoch installed; Main_RenderWorld ownership remains with Upscaler");
	}

#else
		namespace F4Hooks = RE::FO4Runtime::PreNG::Hooks;

		// PreNG (1.10.163) — REL::ID offsets resolved from version-1-10-163-0.bin.
		// Uses GetRELOffset() + detour_thunk_at() to bypass F4SE runtime ID resolution,
		// which returns incorrect addresses for PreNG (different ID→RVA mapping internally).
		const auto base = RE::FO4Runtime::ModuleBase();
		int installed = 0, skipped = 0;

		for (const auto& hook : F4Hooks::DEFERRED_PIPELINE) {
			const auto name = hook.name;
			const auto id = hook.id.id();
			auto rva = GetRELOffset(id);
			if (!rva) {
				logger::warn("[Deferred] PreNG REL::ID({}) not in .bin — skipping {}", id, name);
				skipped++;
				continue;
			}
			auto addr = base + *rva;
			logger::info("[Deferred] PreNG {}: ID({}) -> base+0x{:X} -> 0x{:X}", name, id, *rva, addr);
			installed++;
		}

			if (installed > 0) {
				// --- Runtime CALL instruction scanner ---
				// Scans +/-128 bytes around each target RVA for 0xE8 (CALL) opcodes.
				// Logs exact CALL addresses for future write_thunk_call hooks.
				logger::info("[Deferred] --- CALL scanner (target area) ---");
				for (const auto& target : F4Hooks::DEFERRED_SCAN_TARGETS) {
					const auto name = target.name;
					const auto rva_val = target.location.rva;
					logger::info("[Deferred]   {} (@+0x{:X}):", name, rva_val);
					for (int off = -512; off <= 512; off++) {
						auto ptr = reinterpret_cast<const uint8_t*>(base + rva_val + off);
						if (*ptr == 0xE8 || *ptr == 0xE9) {
							int32_t rel = *reinterpret_cast<const int32_t*>(ptr + 1);
							auto jumpTarget = base + rva_val + off + 5 + rel;
							const char* kind = (*ptr == 0xE8) ? "CALL" : "JMP ";
							logger::info("[Deferred]       +{: 4d} | 0x{:X} -> {} 0x{:X}",
								off, base + rva_val + off, kind, jumpTarget);
						}
					}
				}
				logger::info("[Deferred] --- end scanner ---");
				
				auto resolvePreNGRelocation = [&](const RE::FO4Runtime::RelocationID& a_location) -> std::optional<std::uintptr_t> {
					const auto rva = GetRELOffset(a_location.id.id());
					if (!rva) {
						return std::nullopt;
					}

					const auto address = base + *rva;
					if (a_location.offset < 0) {
						return address - static_cast<std::uintptr_t>(-a_location.offset);
					}
					return address + static_cast<std::uintptr_t>(a_location.offset);
				};

				// World_Start: detour at function entry
				const auto worldStart = resolvePreNGRelocation(
					RE::FO4Runtime::RelocationID{ F4Hooks::DEFERRED_MAIN_RENDER_WORLD_START });
				if (worldStart) {
					detour_thunk_at<Main_RenderWorld_Start>(*worldStart);
				} else {
					logger::warn("[Deferred] PreNG: Main_RenderWorld_Start missing; deferred start hook skipped");
				}

				// ShadowMaps: write_thunk_call at CALL -50 (verified)
				const auto shadowMapsCall = resolvePreNGRelocation(F4Hooks::DEFERRED_MAIN_RENDER_SHADOW_MAPS_CALL);
				if (shadowMapsCall) {
					stl::write_thunk_call<Main_RenderShadowMaps>(*shadowMapsCall);
				} else {
					logger::warn("[Deferred] PreNG: Main_RenderShadowMaps callsite missing; early prepass hook skipped");
				}

				// BlendedDecals is intentionally not hooked on PreNG. Crash logs showed an AV
				// inside the original decal call chain after this callsite hook was installed,
				// while the thunk only added logging and no required rendering work.
				// World_Start defines the world epoch. MRT binding is scoped to registered
				// BSLighting draws, and EndDeferred() runs when the function returns.
				// ResetState: no standalone function in PreNG (inline D3D11 state changes)
				logger::info("[Deferred] PreNG: World_Start epoch + draw-scoped MRT + ShadowMaps installed; BlendedDecals skipped");
		} else {
			logger::warn("[Deferred] PreNG: no hooks installed ({} IDs missing from .bin)", skipped);
		}

	#endif
	logger::info("[Deferred] Pipeline hooks installed");
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	try {
		func();  // call original first (preserves regs for write_thunk_call)
		LogHookFire("Main_RenderShadowMaps");
		GetSingleton()->EarlyPrepasses();
	} catch (...) {
		logger::error("[Deferred] Main_RenderShadowMaps exception");
	}
}

void Deferred::Hooks::Main_RenderWorld::thunk()
{
	try {
		LogHookFire("Main_RenderWorld");
		func();
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld exception");
	}
}

void Deferred::Hooks::Main_RenderWorld_Start::thunk()
{
	try {
		LogHookFire("Main_RenderWorld_Start");
		auto* deferred = GetSingleton();
		deferred->StartDeferred();
		DeferredWorldEpochGuard epochGuard(deferred);
		func();
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld_Start exception");
	}
}

void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk()
{
	try {
		func();  // call original first (preserves regs for write_thunk_call)
		LogHookFire("Main_RenderWorld_BlendedDecals");
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld_BlendedDecals exception");
	}
}

void Deferred::Hooks::Renderer_Begin::thunk(void* a_this, std::uint32_t a_windowID)
{
	try {
		LogHookFire("Renderer_Begin");
		func(a_this, a_windowID);
	} catch (...) {
		logger::error("[Deferred] Renderer_Begin exception (windowID={})", a_windowID);
	}
}
