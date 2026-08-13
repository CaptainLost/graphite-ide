#pragma once

#include "Core/Base/Base.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	enum class LogLevel
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical
	};

	// Which half of the program spoke. Both write through named loggers sharing one set of
	// sinks, so a reader gets them interleaved and a sink can still tell them apart.
	enum class LogSource
	{
		Engine,
		Application
	};

	std::string_view ToString(LogLevel level);
	std::string_view ToString(LogSource source);

	spdlog::level::level_enum ToBackendLevel(LogLevel level);
	LogLevel FromBackendLevel(spdlog::level::level_enum level);

	struct LogEntry
	{
		LogLevel Level = LogLevel::Info;
		LogSource Source = LogSource::Engine;
		std::string Message;
	};

	class RingBufferSink;

	// An empty FilePath means no file is written at all, which is what a test run wants.
	struct LogSpec
	{
		std::filesystem::path FilePath;
		std::size_t MaxFileSize = 5u * 1024u * 1024u;
		std::size_t MaxFiles = 3;
		std::size_t ConsoleCapacity = 4096;
		LogLevel Level = LogLevel::Trace;
	};

	class Log final
	{
	public:
		// Both loggers exist from the first message written through them, so anything logged
		// before Init still reaches the console and the in-memory buffer. Init replaces the
		// sink set, and is the only point at which capacity and level can be chosen.
		static void Init(const LogSpec& spec = LogSpec{});

		static const Ref<spdlog::logger>& GetEngineLogger();
		static const Ref<spdlog::logger>& GetApplicationLogger();
		static const Ref<RingBufferSink>& GetConsoleSink();
	};

	// Defined by the client, next to CreateApplication and for the same reason: the entry
	// point runs before the application exists.
	LogSpec CreateLogSpec();
}
// The engine logs through CORE_*, the application through APP_*: anything below the engine
// boundary that used APP_* would be reporting itself as the application.
#define CORE_TRACE(...) ::Core::Log::GetEngineLogger()->trace(__VA_ARGS__)
#define CORE_INFO(...) ::Core::Log::GetEngineLogger()->info(__VA_ARGS__)
#define CORE_WARN(...) ::Core::Log::GetEngineLogger()->warn(__VA_ARGS__)
#define CORE_ERROR(...) ::Core::Log::GetEngineLogger()->error(__VA_ARGS__)
#define CORE_CRITICAL(...) ::Core::Log::GetEngineLogger()->critical(__VA_ARGS__)

#define APP_TRACE(...) ::Core::Log::GetApplicationLogger()->trace(__VA_ARGS__)
#define APP_INFO(...) ::Core::Log::GetApplicationLogger()->info(__VA_ARGS__)
#define APP_WARN(...) ::Core::Log::GetApplicationLogger()->warn(__VA_ARGS__)
#define APP_ERROR(...) ::Core::Log::GetApplicationLogger()->error(__VA_ARGS__)
#define APP_CRITICAL(...) ::Core::Log::GetApplicationLogger()->critical(__VA_ARGS__)
