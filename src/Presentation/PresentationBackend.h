#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace CommunityShaders::Presentation {
enum class BackendKind { kD3D11, kD3D12Proxy };

class IPresentationBackend {
public:
  virtual ~IPresentationBackend() = default;
  [[nodiscard]] virtual BackendKind GetKind() const noexcept = 0;
  virtual void Present(IDXGISwapChain *a_swapChain, ID3D11Device *a_device) = 0;
};

class D3D11PresentationBackend final : public IPresentationBackend {
public:
  [[nodiscard]] BackendKind GetKind() const noexcept override {
    return BackendKind::kD3D11;
  }
  void Present(IDXGISwapChain *a_swapChain, ID3D11Device *a_device) override;
};

class D3D12PresentationBackend final : public IPresentationBackend {
public:
  [[nodiscard]] BackendKind GetKind() const noexcept override {
    return BackendKind::kD3D12Proxy;
  }
  void Present(IDXGISwapChain *a_swapChain, ID3D11Device *a_device) override;
};

class Coordinator {
public:
  [[nodiscard]] static Coordinator &Get() noexcept;

  void OnD3D11DeviceCreated(ID3D11Device *a_device);
  void OnD3D11Present(IDXGISwapChain *a_swapChain);
  void OnD3D12ProxyFrame();
  bool TryRecoverExistingRenderer();

  [[nodiscard]] BackendKind GetActiveBackendKind() const noexcept;

private:
  Coordinator() = default;

  std::atomic<ID3D11Device *> device{nullptr};
  std::atomic_bool deviceReady{false};
  std::atomic_bool existingRendererRecoveryComplete{false};
  D3D11PresentationBackend d3d11;
  D3D12PresentationBackend d3d12;
};
} // namespace CommunityShaders::Presentation
