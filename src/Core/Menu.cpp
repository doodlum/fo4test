#include "Core/Menu.h"

#include "Core/Globals.h"

#include "Overlay/Overlay.h"
#include "Overlay/OverlayWndProc.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>
#include <SimpleIni.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>

namespace CommunityShaders::Menu
{
	namespace
	{
		constexpr const char* kSettingsSection = "Settings";
		const std::filesystem::path kOverlaySelfSettingsPath{ "Data\\F4SE\\Plugins\\Overlay\\Overlay.ini" };

		bool initialized = false;
		bool menuOpen = false;
		bool loggedFirstRender = false;
		uint64_t initTimestamp = 0;
		HWND hwnd = nullptr;
		WNDPROC previousWndProc = nullptr;
		ImGuiContext* imguiContext = nullptr;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		float d3d11DpiScale = 1.0f;
		uint32_t d3d11BackBufferWidth = 0;
		uint32_t d3d11BackBufferHeight = 0;
		int forcedCursorShowCount = 0;
		std::mutex renderMutex;

		void RestoreD3D11CursorShowCount()
		{
			while (forcedCursorShowCount > 0) {
				ShowCursor(FALSE);
				--forcedCursorShowCount;
			}
		}

		void ForceD3D11HardwareCursorVisible()
		{
			int showResult = -1;
			do {
				showResult = ShowCursor(TRUE);
				++forcedCursorShowCount;
			} while (showResult < 0 && forcedCursorShowCount < 8);

			SetCursor(LoadCursorW(nullptr, IDC_ARROW));
		}

		void UpdateD3D11MouseCaptureState(bool a_overlayVisible)
		{
			if (!a_overlayVisible) {
				RestoreD3D11CursorShowCount();
				return;
			}

			ReleaseCapture();
			ClipCursor(nullptr);
			if (hwnd) {
				SetActiveWindow(hwnd);
				SetFocus(hwnd);
			}
			if (forcedCursorShowCount == 0) {
				ForceD3D11HardwareCursorVisible();
			} else {
				SetCursor(LoadCursorW(nullptr, IDC_ARROW));
			}
		}

		void UpdateD3D11DisplayMetrics(IDXGISwapChain* a_swapChain)
		{
			if (!a_swapChain) {
				return;
			}

			ID3D11Texture2D* backBuffer = nullptr;
			const auto result = a_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(result) || !backBuffer) {
				return;
			}

			D3D11_TEXTURE2D_DESC backBufferDesc{};
			backBuffer->GetDesc(&backBufferDesc);
			backBuffer->Release();

			d3d11BackBufferWidth = backBufferDesc.Width;
			d3d11BackBufferHeight = backBufferDesc.Height;

			RECT clientRect{};
			if (!hwnd || !GetClientRect(hwnd, &clientRect)) {
				return;
			}

			const auto clientWidth = static_cast<float>(std::max<LONG>(1, clientRect.right - clientRect.left));
			const auto clientHeight = static_cast<float>(std::max<LONG>(1, clientRect.bottom - clientRect.top));

			auto& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(clientWidth, clientHeight);
			io.DisplayFramebufferScale = ImVec2(
				static_cast<float>(d3d11BackBufferWidth) / clientWidth,
				static_cast<float>(d3d11BackBufferHeight) / clientHeight);
		}

		void UpdateD3D11PolledMouseInput()
		{
			if (!hwnd) {
				return;
			}

			auto& io = ImGui::GetIO();
			POINT cursor{};
			if (GetCursorPos(&cursor)) {
				ScreenToClient(hwnd, &cursor);
				io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
			}

			static std::array<bool, 5> previousButtons{};
			constexpr std::array<int, 5> buttonKeys{
				VK_LBUTTON,
				VK_RBUTTON,
				VK_MBUTTON,
				VK_XBUTTON1,
				VK_XBUTTON2
			};
			for (int i = 0; i < static_cast<int>(buttonKeys.size()); ++i) {
				const bool down = (GetAsyncKeyState(buttonKeys[static_cast<std::size_t>(i)]) & 0x8000) != 0;
				if (down != previousButtons[static_cast<std::size_t>(i)]) {
					io.AddMouseButtonEvent(i, down);
					previousButtons[static_cast<std::size_t>(i)] = down;
				}
			}
		}

		float GetD3D11UIScale()
		{
			return d3d11DpiScale * Overlay::GetSingleton()->GetUIScaleOverride();
		}

		void LoadOverlaySelfSettings()
		{
			std::error_code ec;
			if (!std::filesystem::exists(kOverlaySelfSettingsPath, ec)) {
				return;
			}

			CSimpleIniA ini;
			ini.SetUnicode();
			if (ini.LoadFile(kOverlaySelfSettingsPath.string().c_str()) < 0) {
				return;
			}

			auto* overlay = Overlay::GetSingleton();
			auto hotkey = static_cast<int>(ini.GetLongValue(kSettingsSection, "iOverlayHotkey",
				ini.GetLongValue(kSettingsSection, "iHotkey", overlay->GetHotkey())));
			if (hotkey > 0 && hotkey <= 0xFE) {
				overlay->SetHotkey(hotkey);
			}

			auto scale = static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fUIScale", 1.0));
			if (scale < 0.5f) {
				scale = 0.5f;
			}
			if (scale > 4.0f) {
				scale = 4.0f;
			}
			overlay->SetUIScaleOverride(scale);
			overlay->SetIntroEnabled(ini.GetBoolValue(kSettingsSection, "showIntro",
				ini.GetBoolValue(kSettingsSection, "bShowIntro", overlay->IsIntroEnabled())));
		}

		bool InitializeD3D11(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
		{
			if (initialized) {
				return true;
			}
			if (!a_device || !a_context) {
				return false;
			}

			hwnd = GetForegroundWindow();
			if (!hwnd) {
				logger::warn("[CommunityShaders::Menu] Could not locate foreground window for D3D11 overlay");
				return false;
			}

			IMGUI_CHECKVERSION();
			imguiContext = ImGui::CreateContext();
			ImGui::SetCurrentContext(imguiContext);
			ImGui::StyleColorsDark();

			auto& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
			io.IniFilename = nullptr;

			if (auto hdc = GetDC(hwnd)) {
				d3d11DpiScale = static_cast<float>(GetDeviceCaps(hdc, LOGPIXELSX)) / 96.0f;
				ReleaseDC(hwnd, hdc);
				if (d3d11DpiScale < 1.0f) {
					d3d11DpiScale = 1.0f;
				}
				ImGui::GetStyle().ScaleAllSizes(d3d11DpiScale);
				io.FontGlobalScale = d3d11DpiScale;
				logger::info("[CommunityShaders::Menu] D3D11 DPI scale: {:.2f}", d3d11DpiScale);
			}

			if (!ImGui_ImplWin32_Init(hwnd)) {
				logger::error("[CommunityShaders::Menu] ImGui_ImplWin32_Init failed");
				return false;
			}
			if (!ImGui_ImplDX11_Init(a_device, a_context)) {
				logger::error("[CommunityShaders::Menu] ImGui_ImplDX11_Init failed");
				ImGui_ImplWin32_Shutdown();
				return false;
			}

			device = a_device;
			context = a_context;
			LoadOverlaySelfSettings();

			auto* overlay = Overlay::GetSingleton();
			overlay->hwnd = hwnd;
			previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
			overlay->previousWndProc = previousWndProc;
			overlay->SetVisible(false);
			g_overlay = overlay;

			menuOpen = false;
			initTimestamp = GetTickCount64();
			initialized = true;
			logger::info("[CommunityShaders::Menu] D3D11 overlay initialized (hwnd=0x{:X}, hotkey={})",
				reinterpret_cast<uintptr_t>(hwnd),
				overlay->GetHotkey());
			return true;
		}

		bool IsShowingIntroMessage()
		{
			const auto* overlay = Overlay::GetSingleton();
			return overlay->IsIntroEnabled() && (GetTickCount64() - initTimestamp) < 30000;
		}

		void PollHotkeyState()
		{
			auto* overlay = Overlay::GetSingleton();
			overlay->PollHotkeyState();
			menuOpen = overlay->IsVisible();
		}

		void DrawRegisteredPanels()
		{
			auto* overlay = Overlay::GetSingleton();
			if (menuOpen) {
				const float uiScale = GetD3D11UIScale();
				ImGui::SetNextWindowSize(ImVec2(520.0f * uiScale, 480.0f * uiScale), ImGuiCond_FirstUseEver);
				if (ImGui::Begin("Community Shaders", &menuOpen)) {
					overlay->DrawOverlaySettings();
					for (auto* feature : Feature::GetFeatureList()) {
						if (feature->loaded && feature->IsInMenu()) {
							ImGui::PushID(feature);
							feature->DrawSettings();
							ImGui::PopID();
						}
					}
					ImGui::Separator();
					if (ImGui::Button("Save All Settings")) {
						overlay->SaveOverlaySelfSettings();
						for (auto* feature : Feature::GetFeatureList()) {
							if (feature->loaded) {
								feature->SaveSettings();
							}
						}
					}
				}
				ImGui::End();
				overlay->SetVisible(menuOpen);
			}

			if (!menuOpen && IsShowingIntroMessage()) {
				const float padding = 20.0f * GetD3D11UIScale();
				ImGui::SetNextWindowBgAlpha(0.65f);
				ImGui::SetNextWindowPos(ImVec2(padding, padding), ImGuiCond_Always);
				constexpr int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
					ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
					ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
					ImGuiWindowFlags_AlwaysAutoResize;
				if (ImGui::Begin("##CommunityShadersIntro", nullptr, flags)) {
					ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "CommunityShaders loaded.");
					ImGui::SameLine();
					ImGui::Text("Press End to open in-game menu.");
				}
				ImGui::End();
			}
		}

		void RenderD3D11ImGuiDrawData(IDXGISwapChain* a_swapChain)
		{
			if (!a_swapChain || !context) {
				return;
			}

			ID3D11Texture2D* backBuffer = nullptr;
			auto result = a_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(result) || !backBuffer) {
				static bool loggedBackBufferFailure = false;
				if (!loggedBackBufferFailure) {
					logger::warn("[CommunityShaders::Menu] D3D11 overlay waiting for backbuffer (hr=0x{:08X})",
						static_cast<uint32_t>(result));
					loggedBackBufferFailure = true;
				}
				return;
			}

			D3D11_TEXTURE2D_DESC backBufferDesc{};
			backBuffer->GetDesc(&backBufferDesc);

			ID3D11RenderTargetView* rtv = nullptr;
			result = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
			backBuffer->Release();
			if (FAILED(result) || !rtv) {
				static bool loggedRTVFailure = false;
				if (!loggedRTVFailure) {
					logger::warn("[CommunityShaders::Menu] Failed to create D3D11 overlay RTV (hr=0x{:08X})",
						static_cast<uint32_t>(result));
					loggedRTVFailure = true;
				}
				return;
			}

			ID3D11RenderTargetView* oldRTV = nullptr;
			ID3D11DepthStencilView* oldDSV = nullptr;
			context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

			UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			context->RSGetViewports(&oldViewportCount, oldViewports);

			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(backBufferDesc.Width);
			viewport.Height = static_cast<float>(backBufferDesc.Height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			context->OMSetRenderTargets(1, &rtv, nullptr);
			context->RSSetViewports(1, &viewport);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			if (oldViewportCount > 0) {
				context->RSSetViewports(oldViewportCount, oldViewports);
			}
			context->OMSetRenderTargets(1, &oldRTV, oldDSV);
			if (oldRTV) {
				oldRTV->Release();
			}
			if (oldDSV) {
				oldDSV->Release();
			}
			rtv->Release();
		}
	}

	void Setup() {}
	void Draw()
	{
		auto* overlay = Overlay::GetSingleton();
		PollHotkeyState();
		menuOpen = overlay->IsVisible();
	}
	void Render(ID3D11Device* a_device, ID3D11DeviceContext* a_context, IDXGISwapChain* a_swapChain)
	{
		std::lock_guard lock(renderMutex);
		if (!InitializeD3D11(a_device, a_context)) {
			return;
		}

		auto* overlay = Overlay::GetSingleton();
		PollHotkeyState();
		menuOpen = overlay->IsVisible();
		const bool showIntro = IsShowingIntroMessage();
		UpdateD3D11MouseCaptureState(menuOpen);
		if (!menuOpen && !showIntro) {
			return;
		}

		if (!loggedFirstRender) {
			loggedFirstRender = true;
			logger::info("[CommunityShaders::Menu] First D3D11 render (visible={}, intro={})", menuOpen, showIntro);
		}

		ImGui::SetCurrentContext(imguiContext);
		ImGui::GetIO().MouseDrawCursor = false;
		ImGui::GetIO().FontGlobalScale = GetD3D11UIScale();
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		UpdateD3D11DisplayMetrics(a_swapChain);
		if (menuOpen) {
			UpdateD3D11PolledMouseInput();
		}
		ImGui::NewFrame();

		DrawRegisteredPanels();

		ImGui::Render();
		RenderD3D11ImGuiDrawData(a_swapChain);
	}
	void Reset() {}
	bool IsOpen() noexcept
	{
		return Overlay::GetSingleton()->IsVisible();
	}
	void SetOpen(bool a_open) noexcept
	{
		menuOpen = a_open;
		Overlay::GetSingleton()->SetVisible(a_open);
	}
	void SetHwnd(HWND a_hwnd) noexcept
	{
		hwnd = a_hwnd;
	}
}
