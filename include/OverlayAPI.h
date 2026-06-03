#pragma once

// OverlayAPI.h — Public C-API for NuclearGFX Overlay registration
//
// Feature DLLs (FrameGen, Upscaler, Reflex) call these functions
// to register their settings panels with the D3D12 overlay host.
// Include this header in any plugin that wants to register panels.
//
// Overlay.dll exports these symbols. Feature DLLs resolve them via
// GetProcAddress(GetModuleHandleW(L"Overlay.dll"), "Overlay_RegisterPanel").
//
// All functions are thread-safe and may be called from any F4SE load phase.

#if defined(_WIN32) && defined(FO4CS_OVERLAY_API_EXPORTS)
#	define FO4CS_OVERLAY_API __declspec(dllexport)
#else
#	define FO4CS_OVERLAY_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// --- Panel category for grouping in the settings UI ---
enum OverlayPanelCategory {
	kOverlayCategory_Rendering = 0,  // FrameGen, Upscaler
	kOverlayCategory_Latency,        // Reflex
	kOverlayCategory_Debug,          // ShaderDump, logging
	kOverlayCategory_Overlay,        // Hotkey, UI scale, intro
};

// --- Callbacks a feature DLL provides ---
struct OverlayPanelCallbacks {
	// Render the ImGui settings panel. Called inside a Begin/End block.
	// Return non-zero if any setting was modified (triggers save reminder).
	int (*render)(void* userData);

	// Persist current settings to INI file. The host appends a per-panel
	// "Save Settings" button when this is set; aggregate panels that render
	// their own "Save All Settings" button must leave this null.
	void (*save)(void* userData);

	// Optional: called when the overlay menu opens.
	void (*onOpen)(void* userData);

	// Optional: called when the overlay menu closes.
	void (*onClose)(void* userData);

	// Opaque pointer passed to all callbacks.
	void* userData;
};

// --- Registration ---
// Returns panel ID >= 0 on success, -1 on failure (duplicate name).
FO4CS_OVERLAY_API int Overlay_RegisterPanel(const char* name,
                          int category,
                          const struct OverlayPanelCallbacks* callbacks);

FO4CS_OVERLAY_API void Overlay_UnregisterPanel(int panelId);

// --- Hotkey ---
FO4CS_OVERLAY_API int  Overlay_GetHotkey(void);
FO4CS_OVERLAY_API void Overlay_SetHotkey(int vkCode);

// --- UI scale ---
FO4CS_OVERLAY_API float Overlay_GetUIScale(void);
FO4CS_OVERLAY_API void Overlay_SetUIScale(float scale);

// --- Visibility ---
FO4CS_OVERLAY_API int  Overlay_GetVisible(void);
FO4CS_OVERLAY_API void Overlay_SetVisible(int visible);

// --- Stats update (key=value string pairs for debug panels) ---
FO4CS_OVERLAY_API void Overlay_UpdateStats(const char* statKey, const char* statValue);

#ifdef __cplusplus
}
#endif
