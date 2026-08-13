#pragma once

#include "Core/ShaderCache.h"

#include <cstdint>
#include <string>

#include <d3d11.h>

namespace CommunityShaders::Hooks
{
	struct RenderTargetInfo
	{
		bool valid = false;
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};

	std::string FormatBufferSlots(const ShaderCache::ShaderMetadata& a_metadata);
	std::string FormatTextureSlots(const ShaderCache::ShaderMetadata& a_metadata);
	std::string FormatTextureDimensions(const ShaderCache::ShaderMetadata& a_metadata);
	std::string FormatTextureSampleCounts(const ShaderCache::ShaderMetadata& a_metadata);
	std::string FormatDrawVTableFunctions(std::uintptr_t a_vtable);

	RenderTargetInfo GetRenderTargetInfo(ID3D11RenderTargetView* a_renderTargetView);
	std::string FormatRenderTargetInfo(const RenderTargetInfo& a_info);
	std::string FormatViewport(const D3D11_VIEWPORT& a_viewport, UINT a_viewportCount);
	std::string FormatShaderResourceViewInfo(ID3D11ShaderResourceView* a_shaderResourceView);
	std::string FormatShaderResourceViews(UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews);
	std::string FormatConstantBufferInfo(ID3D11Buffer* a_buffer);
	std::string FormatCurrentPixelShaderConstantBuffers(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata);
	std::string FormatCurrentPixelShaderResourceViews(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata);
}
