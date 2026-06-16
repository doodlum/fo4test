#pragma once

#include "Core/Feature.h"

#include <memory>
#include <span>
#include <vector>

struct LightLimitFix;
struct FeatureExtendedMaterials;
struct ShaderDump;
struct FeatureUpscaling;
struct FeatureFrameGeneration;
struct FeatureReflex;
struct FeatureOverlay;

// Global Feature instances — each Feature is declared and defined here (Skyrim CS pattern)
namespace globals::features
{
	extern ::LightLimitFix lightLimitFix;
	extern ::FeatureExtendedMaterials extendedMaterials;
	extern ::ShaderDump shaderDump;
	extern ::FeatureUpscaling upscaling;
	extern ::FeatureFrameGeneration frameGeneration;
	extern ::FeatureReflex reflex;
	extern ::FeatureOverlay overlay;
}

// Lifecycle iterators (called from Runtime)
namespace CommunityShaders
{
	std::vector<Feature*>& GetFeatureList();
	void LoadFeatures();
	void DataLoaded();
	void PostPostLoad();
	void SetupResources();
	void ResetFeatures();
	void DrawFeatureSettings();
}
