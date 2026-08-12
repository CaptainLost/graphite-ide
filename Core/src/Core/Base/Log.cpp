#include "corepch.h"

#include "Core/Base/Log.h"

#include <cstdio>

namespace Core
{
    std::string_view ToString(LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
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

    void Logger::AddSink(std::shared_ptr<ILogSink> sink)
    {
        if (!sink)
            return;

        const std::scoped_lock lock{m_mutex};
        m_sinks.push_back(std::move(sink));
    }

    void Logger::ClearSinks()
    {
        const std::scoped_lock lock{m_mutex};
        m_sinks.clear();
    }

    void Logger::SetLevel(LogLevel level)
    {
        const std::scoped_lock lock{m_mutex};
        m_level = level;
    }

    LogLevel Logger::GetLevel() const
    {
        const std::scoped_lock lock{m_mutex};
        return m_level;
    }

    void Logger::Write(LogLevel level, std::string message)
    {
        std::vector<std::shared_ptr<ILogSink>> sinks;

        {
            const std::scoped_lock lock{m_mutex};
            if (level < m_level)
                return;

            sinks = m_sinks;
        }

        const LogEntry entry{level, std::move(message)};
        for (const auto& sink : sinks)
            sink->Write(entry);
    }
}
