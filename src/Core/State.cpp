#include "Core/State.h"

#include "RuntimeAdapter.h"

#include <memory>

namespace CommunityShaders
{
	State* State::GetSingleton()
	{
		static State singleton;
		return std::addressof(singleton);
	}

	void State::Refresh()
	{
		const auto& capabilities = fo4cs::RuntimeAdapter::Get().GetCapabilities();
		switch (capabilities.flavor) {
		case fo4cs::RuntimeFlavor::kPostAE:
			runtimeFlavor = RuntimeFlavor::PostAE;
			break;
		case fo4cs::RuntimeFlavor::kPostNG:
			runtimeFlavor = RuntimeFlavor::PostNG;
			break;
		case fo4cs::RuntimeFlavor::kPreNG:
		default:
			runtimeFlavor = RuntimeFlavor::PreNG;
			break;
		}
		runtimeName = capabilities.name;

		vr = false;
	}
}
