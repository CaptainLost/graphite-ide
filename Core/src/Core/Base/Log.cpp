#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"

#include "Core/Base/Base.h"

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
		}

		return "?";
	}

	void ConsoleSink::Write(const LogEntry& entry)
	{
		const std::string_view level = ToString(entry.level);
		std::fprintf(entry.level >= LogLevel::Warn ? stderr : stdout, "[%.*s] %s\n",
					 static_cast<int>(level.size()), level.data(), entry.message.c_str());
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

	void Logger::Write(LogLevel level, std::string message)
	{
		std::vector<Ref<ILogSink>> sinks;

		{
			const std::scoped_lock lock{m_Mutex};
			if (level < m_Level)
				return;

			sinks = m_Sinks;
		}

		const LogEntry entry{level, std::move(message)};
		for (const auto& sink : sinks)
			sink->Write(entry);
	}
}