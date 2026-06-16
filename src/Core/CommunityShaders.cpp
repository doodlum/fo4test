#include "Core/CommunityShaders.h"

#include "Core/BSShaderHooks.h"
#include "Core/Deferred.h"
#include "Core/Globals.h"
#include "Core/Hooks.h"
#include "Core/Menu.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderDB.h"
#include "Core/State.h"

#include <memory>

namespace CommunityShaders
{
	Runtime* Runtime::GetSingleton()
	{
		static Runtime singleton;
		return std::addressof(singleton);
	}

	void Runtime::Load()
	{
		if (loaded) {
			return;
		}

		State::GetSingleton()->Refresh();
		Hooks::Install();
		BSShaderHooks::Install();
		Deferred::Hooks::Install();

		ShaderCompiler::GetSingleton()->SetSourceRoot("Data\\Shaders");

		// Load user-supplied ShaderDB entries (fallback mechanism)
		auto runtimeName = State::GetSingleton()->GetRuntimeName();
		auto dbPath = std::filesystem::path(
			"Data\\F4SE\\Plugins\\CommunityShaders\\ShaderDB") / std::string(runtimeName);
		ShaderDB::GetSingleton()->Load(runtimeName, dbPath.string());
		ShaderDB::GetSingleton()->Load("Common",
			(std::filesystem::path(
				"Data\\F4SE\\Plugins\\CommunityShaders\\ShaderDB") / "Common").string());

		LoadFeatures();

		loaded = true;
		logger::info("[CommunityShaders] Foundation loaded for {}",
		             State::GetSingleton()->GetRuntimeName());
	}

	void Runtime::PostPostLoad()
	{
		CommunityShaders::PostPostLoad();
	}

	void Runtime::OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		d3d11Device = a_device;
		Hooks::OnD3D11DeviceCreated(a_device);
		Deferred::GetSingleton()->SetupResources();
		SetupResources();
	}

	void Runtime::OnFrame()
	{
		if (!loaded) {
			return;
		}

		++frameCount;
		Hooks::OnFrame();
		BSShaderHooks::OnFrame();  // drain deferred shader replacements
		ResetFeatures();
		Menu::Reset();
		Menu::Draw();
	}
}
