#include "Features/Overlay.h"
#include "Core/Globals.h"
#include <OverlayAPI.h>

#include <imgui.h>

#include "DX12SwapChain.h"
#include "Overlay/Overlay.h"


	static int UnifiedRender(void*) {
		// Overlay self-settings merged into the unified panel
		Overlay::GetSingleton()->DrawOverlaySettings();

		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->loaded && feature->IsInMenu()) {
				feature->DrawSettings();
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Save All Settings")) {
			Overlay::GetSingleton()->SaveOverlaySelfSettings();
			for (auto* feature : Feature::GetFeatureList()) {
				if (feature->loaded) {
					feature->SaveSettings();
				}
			}
		}
		return 0;
	}
void FeatureOverlay::Load()
{
	overlay = Overlay::GetSingleton();

	// Register callbacks BEFORE DX12SwapChain creation.
	// PostPostLoad is too late — D3D11 device init happens before kPostPostLoad.
	auto* swapChain = DX12SwapChain::GetSingleton();
	swapChain->RegisterOverlayInitCallback(&Overlay::OnSwapChainCreated);
	swapChain->RegisterOverlayPresentCallback(&Overlay::OnPresent);
	swapChain->RegisterOverlayPollCallback(&Overlay::OnPollHotkey);

	// Register unified settings panel with D3D12 Overlay
	{
		OverlayPanelCallbacks cbs{};
		cbs.render = UnifiedRender;
		Overlay_RegisterPanel("Community Shaders", kOverlayCategory_Rendering, &cbs);
	}

	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::Overlay] Loaded with DX12SwapChain callbacks");
}

void FeatureOverlay::PostPostLoad()
{
	if (!loaded || !overlay) return;

	logger::info("[Feature::Overlay] PostPostLoad — {}",
		overlay->IsInitialized() ? "panels ready" : "waiting for D3D12 device");
}

void FeatureOverlay::SetupResources()
{
	// Overlay D3D12 ImGui backend is initialized lazily when DX12SwapChain
	// calls overlayInitCallback (overlay->Initialize) from OnSwapChainCreated.
}

void FeatureOverlay::DrawOverlay()
{
	if (!loaded || !overlay) return;

	// Overlay rendering is handled by the DX12SwapChain Present callback.
	// Overlay::OnPresent is called from DX12SwapChain::Present with the
	// D3D12 command list and back buffer, so we don't render directly here.
}

void FeatureOverlay::Reset()
{
	// Reset is called every frame from OnFrame. Do NOT call Shutdown() here —
	// it destroys the ImGui context and D3D12 backend, crashing the next frame.
}
