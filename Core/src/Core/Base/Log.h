#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"

#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	enum class LogLevel
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical
	};

	// Which half of the program spoke. The engine and the application share one logger and
	// one set of sinks, because a reader wants them interleaved in the order they happened;
	// this is what lets a sink still tell them apart when it renders or filters.
	enum class LogSource
	{
		Engine,
		Application
	};

	std::string_view ToString(LogLevel level);
	std::string_view ToString(LogSource source);

	struct LogEntry
	{
		LogLevel Level = LogLevel::Info;
		LogSource Source = LogSource::Engine;
		std::string Message;
	};

	class ILogSink
	{
	public:
		virtual ~ILogSink() = default;
		virtual void Write(const LogEntry& entry) = 0;
	};

	class ConsoleSink final : public ILogSink
	{
	public:
		void Write(const LogEntry& entry) override;
	};

	class Logger final : NonCopyable
	{
	public:
		static Logger& Get();

		void AddSink(Ref<ILogSink> sink);
		void ClearSinks();

		void SetLevel(LogLevel level);
		LogLevel GetLevel() const;

		void Write(LogSource source, LogLevel level, std::string message);

	private:
		Logger() = default;

		mutable std::mutex m_Mutex;
		std::vector<Ref<ILogSink>> m_Sinks;
		LogLevel m_Level = LogLevel::Trace;
	};
}
#define CORE_LOG(source, level, ...) \
	::Core::Logger::Get().Write((source), (level), std::format(__VA_ARGS__))

// The engine logs through CORE_*, the application through APP_*. Anything below the
// engine boundary that used APP_* would be reporting itself as the application, which is
// the one thing this split exists to prevent.
#define CORE_TRACE(...) CORE_LOG(::Core::LogSource::Engine, ::Core::LogLevel::Trace, __VA_ARGS__)
#define CORE_INFO(...) CORE_LOG(::Core::LogSource::Engine, ::Core::LogLevel::Info, __VA_ARGS__)
#define CORE_WARN(...) CORE_LOG(::Core::LogSource::Engine, ::Core::LogLevel::Warn, __VA_ARGS__)
#define CORE_ERROR(...) CORE_LOG(::Core::LogSource::Engine, ::Core::LogLevel::Error, __VA_ARGS__)
#define CORE_CRITICAL(...) \
	CORE_LOG(::Core::LogSource::Engine, ::Core::LogLevel::Critical, __VA_ARGS__)

#define APP_TRACE(...) CORE_LOG(::Core::LogSource::Application, ::Core::LogLevel::Trace, __VA_ARGS__)
#define APP_INFO(...) CORE_LOG(::Core::LogSource::Application, ::Core::LogLevel::Info, __VA_ARGS__)
#define APP_WARN(...) CORE_LOG(::Core::LogSource::Application, ::Core::LogLevel::Warn, __VA_ARGS__)
#define APP_ERROR(...) CORE_LOG(::Core::LogSource::Application, ::Core::LogLevel::Error, __VA_ARGS__)
#define APP_CRITICAL(...) \
	CORE_LOG(::Core::LogSource::Application, ::Core::LogLevel::Critical, __VA_ARGS__)
