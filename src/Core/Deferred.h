#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <winrt/base.h>

namespace RE
{
	class BSBatchRenderer;
	class BSShader;
	class BSShaderAccumulator;
	class NiAVObject;
}

class Deferred
{
public:
	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return std::addressof(singleton);
	}

	void SetupResources();
	void ReflectionsPrepasses();
	void EarlyPrepasses();
	void StartDeferred();
	void EndDeferred();
	void PrepassPasses();
	void DeferredPasses();

	[[nodiscard]] bool IsDeferredPassActive() const noexcept { return deferredPass.load(std::memory_order_acquire); }
	void RegisterLightingPixelShader(ID3D11PixelShader* a_shader);
	bool BeginLightingDraw(ID3D11DeviceContext* a_context);
	void EndLightingDraw(ID3D11DeviceContext* a_context) noexcept;

	enum class GBufferTarget : std::uint8_t
	{
		kNormal,
		kAlbedo,
		kEmissive,
		kMaterial,
		kTotal
	};

	struct GBufferTargetBinding
	{
		GBufferTarget target;
		std::uint32_t rendererTargetIndex;
		DXGI_FORMAT expectedFormat;
		const char* name;
	};

	static constexpr auto kGBufferTargetCount = static_cast<std::size_t>(GBufferTarget::kTotal);
	static constexpr auto kForwardRenderTargetPreserveCount = std::size_t{ 2 };
	static constexpr auto kDeferredRenderTargetCount = kForwardRenderTargetPreserveCount + kGBufferTargetCount;
	static constexpr auto kMaxBoundRenderTargetCount = std::size_t{ D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT };
	using GBufferTargetBindings = std::array<GBufferTargetBinding, kGBufferTargetCount>;

	struct DeferredRenderTargetBinding
	{
		std::uint32_t outputSlot;
		GBufferTarget target;
		const char* name;
	};

	struct ShaderLookupDescriptorState
	{
		std::int32_t shaderType = 0;
		std::uint32_t originalVertexDescriptor = 0;
		std::uint32_t originalPixelDescriptor = 0;
		std::uint32_t vertexDescriptor = 0;
		std::uint32_t pixelDescriptor = 0;
		bool deferredRequested = false;
		bool deferredSupported = false;
		bool modified = false;
		const char* reason = "uninitialized";
	};

	using DeferredRenderTargetBindings = std::array<DeferredRenderTargetBinding, kGBufferTargetCount>;

	[[nodiscard]] static const GBufferTargetBindings& GetGBufferTargetBindings() noexcept;
	[[nodiscard]] static const DeferredRenderTargetBindings& GetDeferredRenderTargetBindings() noexcept;
	[[nodiscard]] bool AreGBufferResourcesReady() const noexcept { return gBufferResourcesReady; }
	[[nodiscard]] D3D11_TEXTURE2D_DESC GetGBufferDesc(GBufferTarget a_target) const noexcept;
	[[nodiscard]] ID3D11Texture2D* GetGBufferTexture(GBufferTarget a_target) const noexcept;
	[[nodiscard]] ID3D11ShaderResourceView* GetGBufferSRV(GBufferTarget a_target) const noexcept;
	[[nodiscard]] ID3D11RenderTargetView* GetGBufferRTV(GBufferTarget a_target) const noexcept;
	[[nodiscard]] ID3D11SamplerState* GetLinearSampler() const noexcept { return linearSampler.get(); }
	[[nodiscard]] ID3D11SamplerState* GetPointSampler() const noexcept { return pointSampler.get(); }
	[[nodiscard]] ShaderLookupDescriptorState BuildShaderLookupDescriptorState(
		const RE::BSShader& a_shader,
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_forceDeferred = false) const noexcept;
	// Blend state extension for the draw-scoped MRT bind.
	[[nodiscard]] ID3D11BlendState* GetOrCreateMRTBlendState(ID3D11BlendState* a_original);

	void ClearShaderCache();

	// --- Hooks into FO4 rendering pipeline ---
	// REL::ID offsets need to be resolved per runtime flavor (PreNG/PostNG/PostAE).
	// These slots correspond to the Skyrim CS counterpart hooks:
	//   Main_RenderShadowMaps    → FO4's shadow map rendering dispatch
	//   Main_RenderWorld         → FO4's main world render entry
	//   Main_RenderWorld_Start   → FO4's opaque geometry batch start
	//   Main_RenderWorld_BlendedDecals → FO4's blend/decals post-pass
	//   Renderer_Begin           → FO4's renderer-window begin
	struct Hooks
	{
		struct Main_RenderShadowMaps
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_Begin
		{
			static void thunk(void* a_this, std::uint32_t a_windowID);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

private:
	Deferred() = default;
	void InstallDrawHooks(ID3D11DeviceContext* a_context);
	[[nodiscard]] bool IsRegisteredLightingPixelShader(ID3D11PixelShader* a_shader) const;

	std::atomic_bool deferredPass = false;
	bool gBufferResourcesReady = false;
	std::array<D3D11_TEXTURE2D_DESC, kGBufferTargetCount> gBufferDescriptions{};
	winrt::com_ptr<ID3D11SamplerState> linearSampler;
	winrt::com_ptr<ID3D11SamplerState> pointSampler;
	mutable std::shared_mutex lightingShaderLock;
	std::unordered_map<ID3D11PixelShader*, winrt::com_ptr<ID3D11PixelShader>> lightingPixelShaders;
	std::atomic_bool hasLightingPixelShaders = false;
	std::mutex blendStateLock;
	std::unordered_map<ID3D11BlendState*, winrt::com_ptr<ID3D11BlendState>> blendStateCache;
};
