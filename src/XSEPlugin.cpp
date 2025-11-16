
#include "DX11Hooks.h"
#include "Upscaling.h"

void InitializeLog()
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = spdlog::level::info;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%v"s);
}

#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.PluginVersion(Plugin::VERSION);
	data.PluginName(Plugin::NAME.data());
	data.AuthorName("");
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	data.CompatibleVersions({ F4SE::RUNTIME_LATEST });

	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	a_info->name = Plugin::NAME.data();
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->version = 0;
	return true;
}
#endif

void MessageHandler(F4SE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case F4SE::MessagingInterface::kPostPostLoad:
		{
			Upscaling::GetSingleton()->PostPostLoad();
			break;
		}
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

#ifndef NDEBUG
#	if defined(FALLOUT_POST_NG)
	while (!REX::W32::IsDebuggerPresent()) {};
#	else
	while (!IsDebuggerPresent()) {};
#	endif
#endif

	InitializeLog();

	DX11Hooks::Install();
	Upscaling::InstallHooks();

	Upscaling::GetSingleton()->LoadSettings();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	return true;
}
