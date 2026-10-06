#pragma once

#include "Core/Base/Pointers.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
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
		// A reader that remembers a message, a selection say, keeps this number and not an index:
		// the buffer drops its oldest entries, and every index then names the next message along.
		std::uint64_t Sequence = 0;
	};

	class RingBufferSink;

	struct LogSpec
	{
		std::filesystem::path FilePath;
		std::size_t MaximumFileSize = 5u * 1024u * 1024u;
		std::size_t RotatedFilesKept = 3;
		std::size_t ConsoleCapacity = 4096;
		LogLevel Level = LogLevel::Trace;
	};

	class Log final
	{
	public:
		static void Init(const LogSpec& spec = LogSpec{});

		static const Ref<spdlog::logger>& GetEngineLogger();
		static const Ref<spdlog::logger>& GetApplicationLogger();
		static const Ref<RingBufferSink>& GetConsoleSink();
	};

	// Defined by the client, not by Core: the entry point calls it to set up logging before the
	// application exists.
	LogSpec CreateLogSpec();
}
// Core logs through CORE_* and the application through APP_*: a Core file that used APP_* would
// report itself as the application.
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
