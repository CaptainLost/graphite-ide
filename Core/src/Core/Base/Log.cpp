#include "corepch.h"

#include "Core/Base/Log.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"

#include "Core/Base/RingBufferSink.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdio>
#include <system_error>
#include <vector>

namespace Core {

	namespace {
		// The file carries the date and sub-second field the console has no use for, so that
		// it still sorts and greps a week later.
		constexpr const char* kConsolePattern = "%^[%T] [%n] %v%$";
		constexpr const char* kFilePattern = "[%Y-%m-%d %T.%e] [%n] [%l] %v";

		struct LogState
		{
			Ref<RingBufferSink> Console;
			Ref<spdlog::logger> Engine;
			Ref<spdlog::logger> Application;
		};

		Ref<spdlog::sinks::sink> MakeProcessConsoleSink()
		{
			auto sink = CreateRef<spdlog::sinks::stdout_color_sink_mt>();
			sink->set_pattern(kConsolePattern);
			return sink;
		}

		Ref<spdlog::logger> MakeLogger(LogSource source, Ref<spdlog::sinks::sink> console,
									   Ref<RingBufferSink> buffer)
		{
			const std::string_view name = ToString(source);
			auto logger = CreateRef<spdlog::logger>(std::string{name},
													spdlog::sinks_init_list{
														std::move(console),
														std::move(buffer)});
			logger->set_level(spdlog::level::trace);
			logger->flush_on(spdlog::level::warn);
			return logger;
		}

		LogState& GetState()
		{
			static LogState state = [] {
				const LogSpec defaults;
				const auto console = MakeProcessConsoleSink();
				const auto buffer = CreateRef<RingBufferSink>(defaults.ConsoleCapacity);

				return LogState{buffer, MakeLogger(LogSource::Engine, console, buffer),
								MakeLogger(LogSource::Application, console, buffer)};
			}();

			return state;
		}
	}

	std::string_view ToString(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Trace:
			return "TRACE";
		case LogLevel::Info:
			return "INFO";
		case LogLevel::Warn:
			return "WARN";
		case LogLevel::Error:
			return "ERROR";
		case LogLevel::Critical:
			return "CRITICAL";
		}

		return "?";
	}

	std::string_view ToString(LogSource source)
	{
		switch (source)
		{
		case LogSource::Engine:
			return "CORE";
		case LogSource::Application:
			return "APP";
		}

		return "?";
	}

	spdlog::level::level_enum ToBackendLevel(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Trace:
			return spdlog::level::trace;
		case LogLevel::Info:
			return spdlog::level::info;
		case LogLevel::Warn:
			return spdlog::level::warn;
		case LogLevel::Error:
			return spdlog::level::err;
		case LogLevel::Critical:
			return spdlog::level::critical;
		}

		return spdlog::level::info;
	}

	LogLevel FromBackendLevel(spdlog::level::level_enum level)
	{
		switch (level)
		{
		case spdlog::level::trace:
		case spdlog::level::debug:
			return LogLevel::Trace;
		case spdlog::level::info:
			return LogLevel::Info;
		case spdlog::level::warn:
			return LogLevel::Warn;
		case spdlog::level::err:
			return LogLevel::Error;
		case spdlog::level::critical:
			return LogLevel::Critical;
		case spdlog::level::off:
		case spdlog::level::n_levels:
			break;
		}

		return LogLevel::Info;
	}

	void Log::Init(const LogSpec& spec)
	{
		LogState& state = GetState();

		// The buffer survives Init whenever the requested capacity is the one it already
		// has, so the messages written before this call are still in the panel afterwards.
		if (state.Console->GetCapacity() != spec.ConsoleCapacity)
		{
			state.Console = CreateRef<RingBufferSink>(spec.ConsoleCapacity);
		}

		std::vector<spdlog::sink_ptr> sinks{MakeProcessConsoleSink(), state.Console};

		if (!spec.FilePath.empty())
		{
			std::error_code code;
			std::filesystem::create_directories(spec.FilePath.parent_path(), code);

			try
			{
				auto file = CreateRef<spdlog::sinks::rotating_file_sink_mt>(
					ToUtf8(spec.FilePath), spec.MaxFileSize, spec.MaxFiles);
				file->set_pattern(kFilePattern);
				sinks.push_back(std::move(file));
			}
			catch (const spdlog::spdlog_ex& e)
			{
				// Reported straight to stderr: the loggers are mid-reconfiguration, so
				// this is the one message that cannot go through them.
				std::fprintf(stderr, "Cannot write the log file '%s': %s\n",
							 ToUtf8(spec.FilePath).c_str(), e.what());
			}
		}

		for (const Ref<spdlog::logger>& logger : {state.Engine, state.Application})
		{
			logger->sinks() = sinks;
			logger->set_level(ToBackendLevel(spec.Level));
		}
	}

	const Ref<spdlog::logger>& Log::GetEngineLogger()
	{
		return GetState().Engine;
	}

	const Ref<spdlog::logger>& Log::GetApplicationLogger()
	{
		return GetState().Application;
	}

	const Ref<RingBufferSink>& Log::GetConsoleSink()
	{
		return GetState().Console;
	}
}
