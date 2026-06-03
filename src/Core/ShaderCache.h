#pragma once

#include <d3d11.h>
#include "RE/Bethesda/BSGraphics.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <winrt/base.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace RE
{
	class BSShader;
	namespace BSGraphics
	{
		class PixelShader;
		class VertexShader;
	}
}

namespace CommunityShaders
{
	enum class ShaderStage
	{
		Vertex,
		Pixel,
		Compute,
		Geometry,
		Hull,
		Domain
	};

	class ShaderCache
	{
	public:
		static ShaderCache* GetSingleton();

		void SetDumpAllShaders(bool a_enabled) noexcept { dumpAllShaders = a_enabled; }
		[[nodiscard]] bool ShouldDumpAllShaders() const noexcept { return dumpAllShaders; }

		void ObserveShader(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength);
		[[nodiscard]] std::string HashShaderBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength) const;
		[[nodiscard]] std::optional<std::uint32_t> GetAsmHashForBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength);

		struct DescriptorShaderState
		{
			ShaderStage stage = ShaderStage::Pixel;
			std::int32_t shaderType = 0;
			std::uint32_t descriptor = 0;
			std::string fxpFilename;
			bool found = false;
			std::uintptr_t shaderEntry = 0;
			std::uintptr_t d3dObject = 0;
			std::uint32_t hits = 0;
		};

		void ObserveDescriptorShader(
			ShaderStage a_stage,
			const RE::BSShader& a_shader,
			std::uint32_t a_descriptor,
			std::string_view a_fxpFilename,
			bool a_found,
			std::uintptr_t a_shaderEntry,
			std::uintptr_t a_d3dObject);
		[[nodiscard]] std::optional<DescriptorShaderState> GetDescriptorShaderState(
			ShaderStage a_stage,
			const RE::BSShader& a_shader,
			std::uint32_t a_descriptor) const;
		[[nodiscard]] std::optional<DescriptorShaderState> GetDescriptorShaderState(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::uint32_t a_descriptor,
			std::string_view a_fxpFilename) const;
		[[nodiscard]] RE::BSGraphics::VertexShader* GetVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor);
		[[nodiscard]] RE::BSGraphics::PixelShader* GetPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor);
		[[nodiscard]] RE::BSGraphics::VertexShader* MakeAndAddVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor);
		[[nodiscard]] RE::BSGraphics::PixelShader* MakeAndAddPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor);

		void SetTracePipeline(bool a_enabled) noexcept { tracePipeline = a_enabled; }
		[[nodiscard]] bool ShouldTracePipeline() const noexcept { return tracePipeline; }

		// Shader 创建计数器 — 用于验证 Deferred hook 触发时序
		// 每创建新 shader 递增（去重前），由 hook 日志读取
		[[nodiscard]] uint32_t GetShaderCreationCount() const noexcept { return m_hookVerifyCounter.load(); }

	public:
		struct ShaderMetadata
		{
			std::string uid;
			std::uint32_t hash = 0;
			std::uint32_t asmHash = 0;
			std::uint32_t size = 0;
			std::array<std::uint32_t, 14> constantBufferSizes{};
			std::vector<std::uint32_t> textureSlots;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> textureDimensions;
			std::array<std::uint32_t, 64> textureSampleCounts{};
			std::uint32_t textureSlotMask = 0;
			std::uint32_t textureDimensionMask = 0;
			std::uint32_t inputTextureCount = 0;
			std::uint32_t inputCount = 0;
			std::uint32_t inputMask = 0;
			std::uint32_t outputCount = 0;
			std::uint32_t outputMask = 0;
			std::uint32_t instructionCount = 0;
			std::uint32_t sampleInstructionCount = 0;
			std::uint32_t immediateConstantBufferRows = 0;
			bool hasDiscard = false;
			bool hasImmediateConstantBuffer = false;
		};

		[[nodiscard]] std::optional<ShaderMetadata> GetMetadataForBytecode(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength);
		void ObserveD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata);
		void ObserveD3DShaderObjectBytecode(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata, const void* a_bytecode, SIZE_T a_bytecodeLength);
		[[nodiscard]] std::optional<ShaderMetadata> GetMetadataForD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject);
		[[nodiscard]] bool DumpObservedD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject, std::string_view a_label);

	private:

		void DumpShader(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_hash);
		[[nodiscard]] ShaderMetadata BuildMetadata(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_disassembly) const;
		void WriteMetadataFiles(const std::filesystem::path& a_dumpDirectory, ShaderStage a_stage, const ShaderMetadata& a_metadata) const;
		[[nodiscard]] std::filesystem::path GetDumpDirectory() const;
		[[nodiscard]] static std::string_view GetStageName(ShaderStage a_stage) noexcept;
		[[nodiscard]] static std::string_view GetStageTypeName(ShaderStage a_stage) noexcept;
		[[nodiscard]] static std::uint32_t HashText(std::string_view a_text) noexcept;
		[[nodiscard]] static std::uint32_t GetResourceDimension(std::string_view a_resourceType) noexcept;

		struct DescriptorShaderKey
		{
			ShaderStage stage = ShaderStage::Pixel;
			std::int32_t shaderType = 0;
			std::uint32_t descriptor = 0;
			std::string fxpFilename;

			[[nodiscard]] bool operator==(const DescriptorShaderKey& a_rhs) const noexcept
			{
				return stage == a_rhs.stage &&
				       shaderType == a_rhs.shaderType &&
				       descriptor == a_rhs.descriptor &&
				       fxpFilename == a_rhs.fxpFilename;
			}
		};

		struct DescriptorShaderKeyHash
		{
			[[nodiscard]] std::size_t operator()(const DescriptorShaderKey& a_key) const noexcept
			{
				std::size_t hash = std::hash<std::string>{}(a_key.fxpFilename);
				hash ^= (static_cast<std::size_t>(a_key.descriptor) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2));
				hash ^= (static_cast<std::size_t>(a_key.shaderType) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2));
				hash ^= (static_cast<std::size_t>(a_key.stage) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2));
				return hash;
			}
		};

		struct D3DShaderObjectKey
		{
			ShaderStage stage = ShaderStage::Pixel;
			std::uintptr_t d3dObject = 0;

			[[nodiscard]] bool operator==(const D3DShaderObjectKey& a_rhs) const noexcept
			{
				return stage == a_rhs.stage &&
				       d3dObject == a_rhs.d3dObject;
			}
		};

		struct D3DShaderObjectKeyHash
		{
			[[nodiscard]] std::size_t operator()(const D3DShaderObjectKey& a_key) const noexcept
			{
				std::size_t hash = static_cast<std::size_t>(a_key.d3dObject);
				hash ^= (static_cast<std::size_t>(a_key.stage) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2));
				return hash;
			}
		};

		struct D3DShaderObjectBytecode
		{
			ShaderMetadata metadata;
			std::string bytecodeHash;
			std::vector<std::byte> bytecode;
		};

		struct OwnedDescriptorVertexShader
		{
			RE::BSGraphics::VertexShader entry{};
			winrt::com_ptr<ID3D11VertexShader> d3dShader;
			std::vector<std::byte> bytecode;
		};

		struct OwnedDescriptorPixelShader
		{
			RE::BSGraphics::PixelShader entry{};
			winrt::com_ptr<ID3D11PixelShader> d3dShader;
			std::vector<std::byte> bytecode;
		};

		[[nodiscard]] static DescriptorShaderKey MakeDescriptorShaderKey(
			ShaderStage a_stage,
			const RE::BSShader& a_shader,
			std::uint32_t a_descriptor);
		[[nodiscard]] static std::string NormalizeFxpFilename(std::string_view a_fxpFilename);
		[[nodiscard]] static std::string NormalizeFxpFilename(const char* a_fxpFilename);
		[[nodiscard]] static const char* GetDescriptorCacheState(const std::optional<DescriptorShaderState>& a_state) noexcept;
		[[nodiscard]] static bool ShouldCompileDescriptorShaders();
		[[nodiscard]] static std::optional<std::string> ResolveDescriptorShaderSource(const DescriptorShaderKey& a_key);
		void LogDescriptorBridgeHeld(ShaderStage a_stage, const RE::BSShader& a_shader, std::uint32_t a_descriptor, std::string_view a_operation);
		void LogDescriptorCompileEvent(
			ShaderStage a_stage,
			const RE::BSShader& a_shader,
			std::uint32_t a_descriptor,
			std::string_view a_operation,
			std::string_view a_compileState,
			std::string_view a_reason,
			std::string_view a_extra = {});

		bool dumpAllShaders = false;
		std::unordered_set<std::string> observedHashes;
		std::mutex observedLock;
		std::unordered_map<std::string, std::uint32_t> bytecodeToAsmHash;
		std::unordered_map<std::string, ShaderMetadata> bytecodeToMetadata;
		std::unordered_map<D3DShaderObjectKey, ShaderMetadata, D3DShaderObjectKeyHash> d3dShaderObjectToMetadata;
		std::unordered_map<D3DShaderObjectKey, D3DShaderObjectBytecode, D3DShaderObjectKeyHash> d3dShaderObjectToBytecode;
		std::unordered_set<std::string> dumpedD3DShaderObjectKeys;
		mutable std::mutex descriptorLock;
		std::unordered_map<DescriptorShaderKey, DescriptorShaderState, DescriptorShaderKeyHash> descriptorShaders;
		std::unordered_map<DescriptorShaderKey, std::unique_ptr<OwnedDescriptorVertexShader>, DescriptorShaderKeyHash> descriptorVertexShaders;
		std::unordered_map<DescriptorShaderKey, std::unique_ptr<OwnedDescriptorPixelShader>, DescriptorShaderKeyHash> descriptorPixelShaders;
		std::unordered_set<std::string> descriptorHeldLogs;
		std::unordered_set<std::string> descriptorCompileLogs;
		bool tracePipeline = false;
		std::unordered_set<std::string> traceStackHashes;
		void TraceShaderCreation(ShaderStage a_stage, SIZE_T a_len, std::string_view a_hash);
		std::atomic<uint32_t> m_hookVerifyCounter{ 0 };
	};
}
