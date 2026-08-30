#pragma once

// Logs fatal exceptions (module-relative RIP, code, access address) to the
// plugin log before the engine's own handler exits the process silently.
// Installed once at plugin load; diagnostic only, never alters dispatch.
namespace Crash
{
	void Install();
}
