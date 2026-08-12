#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Base/Log.h"

#include <cstdio>

namespace Core {

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

	void ConsoleSink::Write(const LogEntry& entry)
	{
		const std::string_view source = ToString(entry.Source);
		const std::string_view level = ToString(entry.Level);
		std::fprintf(entry.Level >= LogLevel::Warn ? stderr : stdout, "[%.*s] [%.*s] %s\n",
					 static_cast<int>(source.size()), source.data(), static_cast<int>(level.size()),
					 level.data(), entry.Message.c_str());
	}

	Logger& Logger::Get()
	{
		static Logger instance;
		return instance;
	}

	void Logger::AddSink(Ref<ILogSink> sink)
	{
		if (!sink)
			return;

		const std::scoped_lock lock{m_Mutex};
		m_Sinks.push_back(std::move(sink));
	}

	void Logger::ClearSinks()
	{
		const std::scoped_lock lock{m_Mutex};
		m_Sinks.clear();
	}

	void Logger::SetLevel(LogLevel level)
	{
		const std::scoped_lock lock{m_Mutex};
		m_Level = level;
	}

	LogLevel Logger::GetLevel() const
	{
		const std::scoped_lock lock{m_Mutex};
		return m_Level;
	}

	void Logger::Write(LogSource source, LogLevel level, std::string message)
	{
		std::vector<Ref<ILogSink>> sinks;

		{
			const std::scoped_lock lock{m_Mutex};
			if (level < m_Level)
				return;

			sinks = m_Sinks;
		}

		const LogEntry entry{level, source, std::move(message)};
		for (const auto& sink : sinks)
			sink->Write(entry);
	}
}