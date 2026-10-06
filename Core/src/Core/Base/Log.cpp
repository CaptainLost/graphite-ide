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
		// The file pattern adds the date and the milliseconds the process console leaves out, so a log
		// read a week later still sorts and greps.
		constexpr const char* kProcessConsolePattern = "%^[%T] [%n] %v%$";
		constexpr const char* kFilePattern = "[%Y-%m-%d %T.%e] [%n] [%l] %v";

		struct LogState
		{
			Ref<RingBufferSink> ConsoleSink;
			Ref<spdlog::logger> EngineLogger;
			Ref<spdlog::logger> ApplicationLogger;
		};

		Ref<spdlog::sinks::sink> MakeProcessConsoleSink()
		{
			auto sink = CreateRef<spdlog::sinks::stdout_color_sink_mt>();
			sink->set_pattern(kProcessConsolePattern);
			return sink;
		}

		Ref<spdlog::logger> MakeLogger(LogSource source, Ref<spdlog::sinks::sink> processConsoleSink,
									   Ref<RingBufferSink> consoleSink)
		{
			const std::string_view loggerName = ToString(source);
			auto logger = CreateRef<spdlog::logger>(std::string{loggerName},
													spdlog::sinks_init_list{
														std::move(processConsoleSink),
														std::move(consoleSink)});
			logger->set_level(spdlog::level::trace);
			logger->flush_on(spdlog::level::warn);
			return logger;
		}

		LogState MakeStateBeforeInit()
		{
			const LogSpec defaults;
			const Ref<spdlog::sinks::sink> processConsoleSink = MakeProcessConsoleSink();
			const Ref<RingBufferSink> consoleSink = CreateRef<RingBufferSink>(defaults.ConsoleCapacity);

			return LogState{.ConsoleSink = consoleSink,
							.EngineLogger = MakeLogger(LogSource::Engine, processConsoleSink, consoleSink),
							.ApplicationLogger = MakeLogger(LogSource::Application, processConsoleSink, consoleSink)};
		}

		LogState& GetState()
		{
			static LogState state = MakeStateBeforeInit();

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

		const bool capacityChanged = state.ConsoleSink->GetCapacity() != spec.ConsoleCapacity;
		if (capacityChanged)
		{
			state.ConsoleSink = CreateRef<RingBufferSink>(spec.ConsoleCapacity);
		}

		std::vector<Ref<spdlog::sinks::sink>> sinks{MakeProcessConsoleSink(), state.ConsoleSink};

		const bool writesLogFile = !spec.FilePath.empty();
		if (writesLogFile)
		{
			std::error_code ignoredDirectoryError;
			std::filesystem::create_directories(spec.FilePath.parent_path(), ignoredDirectoryError);

			try
			{
				auto fileSink = CreateRef<spdlog::sinks::rotating_file_sink_mt>(
					ToUtf8(spec.FilePath), spec.MaximumFileSize, spec.RotatedFilesKept);
				fileSink->set_pattern(kFilePattern);
				sinks.push_back(std::move(fileSink));
			}
			catch (const spdlog::spdlog_ex& fileSinkError)
			{
				// Written straight to stderr, not through CORE_ERROR: the loggers still hold the sinks this
				// call is replacing, so the message could land in a console buffer the panel no longer shows.
				std::fprintf(stderr, "Cannot write the log file '%s': %s\n",
							 ToUtf8(spec.FilePath).c_str(), fileSinkError.what());
			}
		}

		for (const Ref<spdlog::logger>& logger : {state.EngineLogger, state.ApplicationLogger})
		{
			logger->sinks() = sinks;
			logger->set_level(ToBackendLevel(spec.Level));
		}
	}

	const Ref<spdlog::logger>& Log::GetEngineLogger()
	{
		return GetState().EngineLogger;
	}

	const Ref<spdlog::logger>& Log::GetApplicationLogger()
	{
		return GetState().ApplicationLogger;
	}

	const Ref<RingBufferSink>& Log::GetConsoleSink()
	{
		return GetState().ConsoleSink;
	}
}
