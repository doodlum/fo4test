#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <optional>

namespace fo4cs {
enum class RuntimeFlavor { kPreNG, kPostNG, kPostAE };

struct RuntimeCapabilities {
  RuntimeFlavor flavor;
  const char *name;
  bool supportsStreamline;
  bool supportsD3D12Proxy;
  bool requiresExistingRendererRecovery;
  bool supportsDeferredPipeline;
};

struct ExistingRenderer {
  ID3D11Device *device = nullptr;
  IDXGISwapChain *swapChain = nullptr;
};

class RuntimeAdapter {
public:
  [[nodiscard]] static const RuntimeAdapter &Get() noexcept;
  [[nodiscard]] const RuntimeCapabilities &GetCapabilities() const noexcept {
    return capabilities;
  }
  [[nodiscard]] std::optional<ExistingRenderer>
  FindExistingRenderer() const noexcept;

private:
  RuntimeAdapter() noexcept;

  RuntimeCapabilities capabilities{};
};
} // namespace fo4cs
