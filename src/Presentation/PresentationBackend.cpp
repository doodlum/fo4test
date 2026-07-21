#include "Presentation/PresentationBackend.h"

#include "Core/CommunityShaders.h"
#include "Core/Menu.h"
#include "DX11Hooks.h"
#include "DX12SwapChain.h"
#include "Diagnostics/HangTrace.h"
#include "RuntimeAdapter.h"

namespace CommunityShaders::Presentation {
namespace {
void AdvanceFeatureCore() {
  auto *runtime = Runtime::GetSingleton();
  if (runtime->IsLoaded()) {
    runtime->OnFrame();
  }
}
} // namespace

void D3D11PresentationBackend::Present(IDXGISwapChain *a_swapChain,
                                       ID3D11Device *a_device) {
  AdvanceFeatureCore();
  if (!a_swapChain || !a_device) {
    fo4cs::Diagnostics::WriteHangTraceLine(
        "Present:D3D11:exit:no-swapchain-or-device");
    return;
  }

  ID3D11DeviceContext *context = nullptr;
  fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:begin");
  a_device->GetImmediateContext(&context);
  fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:end");
  if (!context) {
    return;
  }

  fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:begin");
  Menu::Render(a_device, context, a_swapChain);
  fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:end");
  context->Release();
}

void D3D12PresentationBackend::Present(IDXGISwapChain *, ID3D11Device *) {
  AdvanceFeatureCore();
}

Coordinator &Coordinator::Get() noexcept {
  static Coordinator coordinator;
  return coordinator;
}

void Coordinator::OnD3D11DeviceCreated(ID3D11Device *a_device) {
  fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:enter");
  if (!a_device) {
    fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:skip");
    return;
  }

  ID3D11Device *expected = nullptr;
  if (!device.compare_exchange_strong(expected, a_device,
                                      std::memory_order_acq_rel)) {
    fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:skip");
    return;
  }

  fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:begin");
  Runtime::GetSingleton()->OnD3D11DeviceCreated(a_device);
  deviceReady.store(true, std::memory_order_release);
  fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:end");
}

void Coordinator::OnD3D11Present(IDXGISwapChain *a_swapChain) {
  fo4cs::Diagnostics::WriteHangTraceLine("Present:enter");
  if (GetActiveBackendKind() == BackendKind::kD3D12Proxy) {
    fo4cs::Diagnostics::WriteHangTraceLine("Present:skip-d3d11-dx12-proxy");
    return;
  }

  auto *currentDevice = deviceReady.load(std::memory_order_acquire)
                            ? device.load(std::memory_order_acquire)
                            : nullptr;
  d3d11.Present(a_swapChain, currentDevice);
  fo4cs::Diagnostics::WriteHangTraceLine("Present:exit");
}

void Coordinator::OnD3D12ProxyFrame() {
  fo4cs::Diagnostics::WriteHangTraceLine("DX12ProxyFrame:enter");
  d3d12.Present(nullptr, device.load(std::memory_order_acquire));
  fo4cs::Diagnostics::WriteHangTraceLine("DX12ProxyFrame:exit");
}

bool Coordinator::TryRecoverExistingRenderer() {
  const auto &adapter = fo4cs::RuntimeAdapter::Get();
  if (!adapter.GetCapabilities().requiresExistingRendererRecovery) {
    return false;
  }
  if (existingRendererRecoveryComplete.load(std::memory_order_acquire)) {
    return true;
  }
  if (GetActiveBackendKind() == BackendKind::kD3D12Proxy) {
    logger::debug("[CommunityShaders] Existing-renderer recovery skipped; "
                  "D3D12 proxy is active");
    return false;
  }

  const auto renderer = adapter.FindExistingRenderer();
  if (!renderer) {
    logger::warn("[CommunityShaders] {} existing-renderer recovery unavailable "
                 "(no valid engine renderer window)",
                 adapter.GetCapabilities().name);
    return false;
  }

  if (!DX11Hooks::RecoverExistingRenderer(renderer->device,
                                          renderer->swapChain)) {
    return false;
  }

  existingRendererRecoveryComplete.store(true, std::memory_order_release);
  return true;
}

BackendKind Coordinator::GetActiveBackendKind() const noexcept {
  return DX12SwapChain::GetSingleton()->swapChain ? BackendKind::kD3D12Proxy
                                                  : BackendKind::kD3D11;
}
} // namespace CommunityShaders::Presentation
