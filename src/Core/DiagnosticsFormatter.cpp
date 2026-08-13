#include "Core/DiagnosticsFormatter.h"

#include <format>
#include <sstream>
#include <string>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
	constexpr UINT kMaxLLFLoggedShaderResourceViews = 8;

	std::string FormatBufferSlots(const ShaderCache::ShaderMetadata& a_metadata)
	{
		std::ostringstream result;
		bool first = true;
		for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
			const auto size = a_metadata.constantBufferSizes[slot];
			if (size == 0) {
				continue;
			}

			if (!first) {
				result << ',';
			}
			result << slot << ':' << size;
			first = false;
		}

		return first ? "none" : result.str();
	}

	std::string FormatTextureSlots(const ShaderCache::ShaderMetadata& a_metadata)
	{
		std::ostringstream result;
		for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index) {
			if (index > 0) {
				result << ',';
			}
			result << a_metadata.textureSlots[index];
		}

		return a_metadata.textureSlots.empty() ? "none" : result.str();
	}

	std::string FormatTextureDimensions(const ShaderCache::ShaderMetadata& a_metadata)
	{
		std::ostringstream result;
		for (std::size_t index = 0; index < a_metadata.textureDimensions.size(); ++index) {
			if (index > 0) {
				result << ',';
			}
			const auto [dimension, slot] = a_metadata.textureDimensions[index];
			result << dimension << '@' << slot;
		}

		return a_metadata.textureDimensions.empty() ? "none" : result.str();
	}

	std::string FormatTextureSampleCounts(const ShaderCache::ShaderMetadata& a_metadata)
	{
		std::ostringstream result;
		bool first = true;
		for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot) {
			const auto count = a_metadata.textureSampleCounts[slot];
			if (count == 0) {
				continue;
			}
			if (!first) {
				result << ',';
			}
			result << slot << ':' << count;
			first = false;
		}

		return first ? "none" : result.str();
	}

	std::string FormatDrawVTableFunctions(std::uintptr_t a_vtable)
	{
		if (!a_vtable) {
			return "none";
		}

		const auto* entries = reinterpret_cast<const std::uintptr_t*>(a_vtable);
		return std::format(
			"8=0x{:X},9=0x{:X},12=0x{:X},13=0x{:X},20=0x{:X},21=0x{:X},24=0x{:X},33=0x{:X},38=0x{:X},39=0x{:X},40=0x{:X},44=0x{:X},47=0x{:X},58=0x{:X}",
			entries[8],
			entries[9],
			entries[12],
			entries[13],
			entries[20],
			entries[21],
			entries[24],
			entries[33],
			entries[38],
			entries[39],
			entries[40],
			entries[44],
			entries[47],
			entries[58]);
	}

	RenderTargetInfo GetRenderTargetInfo(ID3D11RenderTargetView* a_renderTargetView)
	{
		RenderTargetInfo result;
		if (!a_renderTargetView) {
			return result;
		}

		result.valid = true;

		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		a_renderTargetView->GetDesc(&viewDesc);
		result.format = viewDesc.Format;

		ID3D11Resource* resource = nullptr;
		a_renderTargetView->GetResource(&resource);
		if (!resource) {
			return result;
		}

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			texture->GetDesc(&textureDesc);
			result.width = textureDesc.Width;
			result.height = textureDesc.Height;
			if (result.format == DXGI_FORMAT_UNKNOWN) {
				result.format = textureDesc.Format;
			}
			texture->Release();
		}
		resource->Release();

		return result;
	}

	std::string FormatRenderTargetInfo(const RenderTargetInfo& a_info)
	{
		if (!a_info.valid) {
			return "none";
		}

		return std::format("{}x{}:fmt{}", a_info.width, a_info.height, static_cast<std::uint32_t>(a_info.format));
	}

	std::string FormatViewport(const D3D11_VIEWPORT& a_viewport, UINT a_viewportCount)
	{
		if (a_viewportCount == 0) {
			return "none";
		}

		return std::format(
			"{:.0f}x{:.0f}+{:.0f},{:.0f}",
			a_viewport.Width,
			a_viewport.Height,
			a_viewport.TopLeftX,
			a_viewport.TopLeftY);
	}

	std::string FormatShaderResourceViewInfo(ID3D11ShaderResourceView* a_shaderResourceView)
	{
		if (!a_shaderResourceView) {
			return "null";
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_shaderResourceView->GetDesc(&viewDesc);

		ID3D11Resource* resource = nullptr;
		a_shaderResourceView->GetResource(&resource);
		if (!resource) {
			return std::format("viewDim{}:no-resource", static_cast<std::uint32_t>(viewDesc.ViewDimension));
		}

		std::string result;
		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			texture->GetDesc(&textureDesc);
			result = std::format(
				"viewDim{}:{}x{}:fmt{}",
				static_cast<std::uint32_t>(viewDesc.ViewDimension),
				textureDesc.Width,
				textureDesc.Height,
				static_cast<std::uint32_t>(textureDesc.Format));
			texture->Release();
		} else {
			D3D11_RESOURCE_DIMENSION resourceDimension{};
			resource->GetType(&resourceDimension);
			result = std::format(
				"viewDim{}:resourceDim{}:fmt{}",
				static_cast<std::uint32_t>(viewDesc.ViewDimension),
				static_cast<std::uint32_t>(resourceDimension),
				static_cast<std::uint32_t>(viewDesc.Format));
		}

		resource->Release();
		return result;
	}

	std::string FormatShaderResourceViews(UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews)
	{
		if (!a_shaderResourceViews || a_viewCount == 0) {
			return "none";
		}

		std::ostringstream result;
		const auto loggedViewCount = a_viewCount < kMaxLLFLoggedShaderResourceViews ? a_viewCount : kMaxLLFLoggedShaderResourceViews;
		for (UINT index = 0; index < loggedViewCount; ++index) {
			if (index > 0) {
				result << ',';
			}
			result << (a_startSlot + index) << '=' << FormatShaderResourceViewInfo(a_shaderResourceViews[index]);
		}
		if (a_viewCount > loggedViewCount) {
			result << ",...+" << (a_viewCount - loggedViewCount);
		}
		return result.str();
	}

	std::string FormatConstantBufferInfo(ID3D11Buffer* a_buffer)
	{
		if (!a_buffer) {
			return "null";
		}

		D3D11_BUFFER_DESC desc{};
		a_buffer->GetDesc(&desc);
		return std::format(
			"{}:usage{}:bind0x{:X}:cpu0x{:X}",
			desc.ByteWidth,
			static_cast<std::uint32_t>(desc.Usage),
			desc.BindFlags,
			desc.CPUAccessFlags);
	}

	std::string FormatCurrentPixelShaderConstantBuffers(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
	{
		if (!a_context) {
			return "none";
		}

		std::ostringstream result;
		bool first = true;
		for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
			if (a_metadata.constantBufferSizes[slot] == 0 || slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
				continue;
			}

			ID3D11Buffer* buffer = nullptr;
			a_context->PSGetConstantBuffers(static_cast<UINT>(slot), 1, &buffer);
			if (!first) {
				result << ',';
			}
			result << "cb" << slot << '=' << FormatConstantBufferInfo(buffer);
			if (buffer) {
				buffer->Release();
			}
			first = false;
		}

		return first ? "none" : result.str();
	}

	std::string FormatCurrentPixelShaderResourceViews(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
	{
		if (!a_context || a_metadata.textureSlots.empty()) {
			return "none";
		}

		std::ostringstream result;
		bool first = true;
		std::unordered_set<std::uint32_t> seenSlots;
		for (const auto slot : a_metadata.textureSlots) {
			if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT || !seenSlots.insert(slot).second) {
				continue;
			}

			ID3D11ShaderResourceView* view = nullptr;
			a_context->PSGetShaderResources(slot, 1, &view);
			if (!first) {
				result << ',';
			}
			result << slot << '=' << FormatShaderResourceViewInfo(view);
			if (view) {
				view->Release();
			}
			first = false;
		}

		return first ? "none" : result.str();
	}
}
