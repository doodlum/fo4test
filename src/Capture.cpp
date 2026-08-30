// <d3d11.h> drags in windows.h, which defines an ERROR macro that would eat
// REX::ERROR.  Keep GDI out and undefine defensively before anything else in
// this translation unit uses the REX logging names.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <d3d11.h>
#include <dxgi.h>
#ifdef ERROR
#	undef ERROR
#endif

#include "PCH.h"

#include "Capture.h"

#include <bit>
#include <cstring>

namespace
{
	// IEEE half -> float.  The back buffer is R16G16B16A16_FLOAT on some
	// display paths, and we would rather write a slightly flat image than
	// refuse to capture.
	[[nodiscard]] float HalfToFloat(std::uint16_t a_half) noexcept
	{
		const std::uint32_t sign = static_cast<std::uint32_t>(a_half >> 15) << 31;
		std::uint32_t       exponent = (a_half >> 10) & 0x1F;
		std::uint32_t       mantissa = a_half & 0x3FF;

		if (exponent == 0) {
			if (mantissa == 0) {
				return std::bit_cast<float>(sign);
			}
			// Subnormal: renormalise.
			exponent = 1;
			while ((mantissa & 0x400) == 0) {
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x3FF;
			return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
		}

		if (exponent == 0x1F) {
			// Inf / NaN -> saturate; we only ever clamp to [0,1] afterwards.
			return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
		}

		return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
	}

	[[nodiscard]] std::uint8_t ToByte(float a_value) noexcept
	{
		if (!(a_value > 0.0f)) {  // also catches NaN
			return 0;
		}
		if (a_value >= 1.0f) {
			return 255;
		}
		return static_cast<std::uint8_t>(a_value * 255.0f + 0.5f);
	}

	// Convert one source row into BGRA8.  Returns false for formats we do not
	// know how to read, so the caller can report the format instead of writing
	// a garbage image.
	[[nodiscard]] bool ConvertRow(
		DXGI_FORMAT a_format, const std::uint8_t* a_src, std::uint8_t* a_dst,
		std::uint32_t a_width) noexcept
	{
		switch (a_format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			for (std::uint32_t x = 0; x < a_width; ++x) {
				a_dst[x * 4 + 0] = a_src[x * 4 + 0];
				a_dst[x * 4 + 1] = a_src[x * 4 + 1];
				a_dst[x * 4 + 2] = a_src[x * 4 + 2];
				a_dst[x * 4 + 3] = 255;
			}
			return true;

		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			for (std::uint32_t x = 0; x < a_width; ++x) {
				a_dst[x * 4 + 0] = a_src[x * 4 + 2];
				a_dst[x * 4 + 1] = a_src[x * 4 + 1];
				a_dst[x * 4 + 2] = a_src[x * 4 + 0];
				a_dst[x * 4 + 3] = 255;
			}
			return true;

		case DXGI_FORMAT_R10G10B10A2_UNORM:
			for (std::uint32_t x = 0; x < a_width; ++x) {
				std::uint32_t packed{};
				std::memcpy(&packed, a_src + x * 4, sizeof(packed));
				a_dst[x * 4 + 0] = static_cast<std::uint8_t>(((packed >> 20) & 0x3FF) >> 2);
				a_dst[x * 4 + 1] = static_cast<std::uint8_t>(((packed >> 10) & 0x3FF) >> 2);
				a_dst[x * 4 + 2] = static_cast<std::uint8_t>(((packed >> 0) & 0x3FF) >> 2);
				a_dst[x * 4 + 3] = 255;
			}
			return true;

		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			for (std::uint32_t x = 0; x < a_width; ++x) {
				std::uint16_t rgba[4]{};
				std::memcpy(rgba, a_src + x * 8, sizeof(rgba));
				a_dst[x * 4 + 0] = ToByte(HalfToFloat(rgba[2]));
				a_dst[x * 4 + 1] = ToByte(HalfToFloat(rgba[1]));
				a_dst[x * 4 + 2] = ToByte(HalfToFloat(rgba[0]));
				a_dst[x * 4 + 3] = 255;
			}
			return true;

		default:
			return false;
		}
	}

	template <class T>
	void Release(T*& a_com) noexcept
	{
		if (a_com) {
			a_com->Release();
			a_com = nullptr;
		}
	}

	[[nodiscard]] bool WriteBMP(
		const std::filesystem::path& a_path, const std::vector<std::uint8_t>& a_bgra,
		std::uint32_t a_width, std::uint32_t a_height, std::string& a_error)
	{
		std::ofstream file{ a_path, std::ios::binary | std::ios::trunc };
		if (!file) {
			a_error = "could not open " + a_path.string() + " for writing";
			return false;
		}

		const std::uint32_t pixelBytes = a_width * a_height * 4;
		const std::uint32_t headerSize = 14 + 40;

		// BITMAPFILEHEADER, written field by field so we do not depend on
		// struct packing.
		const auto u16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
		const auto u32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
		const auto i32 = [&](std::int32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };

		file.put('B').put('M');
		u32(headerSize + pixelBytes);  // bfSize
		u16(0);                        // bfReserved1
		u16(0);                        // bfReserved2
		u32(headerSize);               // bfOffBits

		// BITMAPINFOHEADER.  Negative height => top-down rows, which is the
		// order the mapped texture gives us.
		u32(40);                                     // biSize
		i32(static_cast<std::int32_t>(a_width));     // biWidth
		i32(-static_cast<std::int32_t>(a_height));   // biHeight
		u16(1);                                      // biPlanes
		u16(32);                                     // biBitCount
		u32(0);                                      // biCompression = BI_RGB
		u32(pixelBytes);                             // biSizeImage
		i32(2835);                                   // biXPelsPerMeter (72 dpi)
		i32(2835);                                   // biYPelsPerMeter
		u32(0);                                      // biClrUsed
		u32(0);                                      // biClrImportant

		file.write(reinterpret_cast<const char*>(a_bgra.data()),
			static_cast<std::streamsize>(a_bgra.size()));

		if (!file) {
			a_error = "write failed part-way through " + a_path.string();
			return false;
		}

		return true;
	}
}

namespace Capture
{
	Result ToBMP(const std::filesystem::path& a_path)
	{
		Result result;

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* rendererWindow = RE::BSGraphics::GetCurrentRendererWindow();
		if (!rendererData || !rendererWindow || !rendererWindow->swapChain) {
			result.error = "renderer is not initialised yet";
			return result;
		}

		// CommonLibF4 declares the D3D types in its own REX::W32 namespace to
		// avoid dragging windows.h into every header; they are the same COM
		// objects, so a reinterpret_cast across is the intended bridge.
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		auto* swapChain = reinterpret_cast<IDXGISwapChain*>(rendererWindow->swapChain);
		if (!device || !context) {
			result.error = "no D3D11 device/context";
			return result;
		}

		ID3D11Texture2D* backBuffer{ nullptr };
		if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
				reinterpret_cast<void**>(&backBuffer))) ||
			!backBuffer) {
			result.error = "IDXGISwapChain::GetBuffer failed";
			return result;
		}

		D3D11_TEXTURE2D_DESC desc{};
		backBuffer->GetDesc(&desc);

		result.width = desc.Width;
		result.height = desc.Height;

		ID3D11Texture2D* resolved{ nullptr };
		ID3D11Texture2D* staging{ nullptr };
		ID3D11Texture2D* source = backBuffer;

		const auto cleanup = [&]() noexcept {
			Release(staging);
			Release(resolved);
			Release(backBuffer);
		};

		// A multisampled back buffer cannot be copied to a staging texture
		// directly; resolve it down first.
		if (desc.SampleDesc.Count > 1) {
			auto resolveDesc = desc;
			resolveDesc.SampleDesc.Count = 1;
			resolveDesc.SampleDesc.Quality = 0;
			resolveDesc.Usage = D3D11_USAGE_DEFAULT;
			resolveDesc.BindFlags = 0;
			resolveDesc.CPUAccessFlags = 0;
			resolveDesc.MiscFlags = 0;

			if (FAILED(device->CreateTexture2D(&resolveDesc, nullptr, &resolved)) || !resolved) {
				result.error = "could not create the resolve texture";
				cleanup();
				return result;
			}

			context->ResolveSubresource(resolved, 0, backBuffer, 0, desc.Format);
			source = resolved;
		}

		auto stagingDesc = desc;
		stagingDesc.SampleDesc.Count = 1;
		stagingDesc.SampleDesc.Quality = 0;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)) || !staging) {
			result.error = "could not create the staging texture";
			cleanup();
			return result;
		}

		context->CopyResource(staging, source);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
			result.error = "could not map the staging texture";
			cleanup();
			return result;
		}

		std::vector<std::uint8_t> bgra(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
		bool                      converted = true;
		for (std::uint32_t y = 0; y < desc.Height && converted; ++y) {
			const auto* src = static_cast<const std::uint8_t*>(mapped.pData) +
			                  static_cast<std::size_t>(y) * mapped.RowPitch;
			auto* dst = bgra.data() + static_cast<std::size_t>(y) * desc.Width * 4;
			converted = ConvertRow(desc.Format, src, dst, desc.Width);
		}

		context->Unmap(staging, 0);
		cleanup();

		if (!converted) {
			result.error = "unsupported back buffer format " +
			               std::to_string(static_cast<int>(desc.Format));
			return result;
		}

		if (!WriteBMP(a_path, bgra, desc.Width, desc.Height, result.error)) {
			return result;
		}

		result.ok = true;
		return result;
	}
}
