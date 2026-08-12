#pragma once

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
		Error
	};

	std::string_view ToString(LogLevel level);

	struct LogEntry
	{
		LogLevel level = LogLevel::Info;
		std::string message;
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

		void AddSink(std::shared_ptr<ILogSink> sink);
		void ClearSinks();

		void SetLevel(LogLevel level);
		LogLevel GetLevel() const;

		void Write(LogLevel level, std::string message);

	private:
		Logger() = default;

		mutable std::mutex m_Mutex;
		std::vector<std::shared_ptr<ILogSink>> m_Sinks;
		LogLevel m_Level = LogLevel::Trace;
	};
}
#define CORE_LOG(level, ...) ::Core::Logger::Get().Write((level), std::format(__VA_ARGS__))

#define CORE_TRACE(...) CORE_LOG(::Core::LogLevel::Trace, __VA_ARGS__)
#define CORE_INFO(...) CORE_LOG(::Core::LogLevel::Info, __VA_ARGS__)
#define CORE_WARN(...) CORE_LOG(::Core::LogLevel::Warn, __VA_ARGS__)
#define CORE_ERROR(...) CORE_LOG(::Core::LogLevel::Error, __VA_ARGS__)
