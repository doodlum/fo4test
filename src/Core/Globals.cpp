#include "Core/Globals.h"

#include "Features/LightLimitFix.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ShaderDump.h"
#include "Features/FrameGeneration.h"
#include "Features/Overlay.h"
#include "Features/Reflex.h"
#include "Features/Upscaling.h"

namespace globals::features
{
	LightLimitFix lightLimitFix;
	FeatureExtendedMaterials extendedMaterials;
	ShaderDump shaderDump;
	FeatureUpscaling upscaling;
	FeatureFrameGeneration frameGeneration;
	FeatureReflex reflex;
	FeatureOverlay overlay;
}

namespace CommunityShaders
{
	std::vector<Feature*>& GetFeatureList()
	{
		static std::vector<Feature*> features = {
		&globals::features::lightLimitFix,
		&globals::features::extendedMaterials,
		&globals::features::shaderDump,
		&globals::features::upscaling,
		&globals::features::frameGeneration,
		&globals::features::reflex,
		&globals::features::overlay,
		};
		return features;
	}

	void LoadFeatures()
	{
		for (auto* feature : GetFeatureList()) {
			try {
				feature->LoadSettings();
				feature->Load();
				feature->loaded = true;
				logger::info("[CommunityShaders] Loaded feature {}", feature->GetName());
			} catch (const std::exception& e) {
				feature->loaded = false;
				feature->failedLoadedMessage = e.what();
				logger::error("[CommunityShaders] Failed to load feature {}: {}", feature->GetName(), e.what());
			}
		}
	}

	void DataLoaded()
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->DataLoaded();
			}
		}
	}

	void PostPostLoad()
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->PostPostLoad();
			}
		}
	}

	void SetupResources()
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->SetupResources();
			}
		}
	}

	void ResetFeatures()
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->Reset();
			}
		}
	}

	void DrawFeatureSettings()
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->DrawSettings();
			} else if (feature->DrawFailLoadMessage()) {
				feature->DrawUnloadedUI();
			}
		}
	}
}
