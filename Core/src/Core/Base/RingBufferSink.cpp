#include "corepch.h"

#include "Core/Base/RingBufferSink.h"

#include "Core/Base/Assert.h"

#include <string_view>

namespace Core {

	namespace {
		LogSource SourceFromLoggerName(std::string_view loggerName)
		{
			return loggerName == ToString(LogSource::Application) ? LogSource::Application
																  : LogSource::Engine;
		}
	}

	RingBufferSink::RingBufferSink(std::size_t capacity) : m_Capacity(capacity)
	{
		CORE_ASSERT(capacity > 0, "RingBufferSink requires a capacity greater than zero");
	}

	// Keeps the raw message text, not spdlog's formatted line: the writer and the severity stay
	// separate fields a renderer can lay out or filter on, instead of being parsed back out.
	void RingBufferSink::sink_it_(const spdlog::details::log_msg& message)
	{
		LogEntry entry{.Level = FromBackendLevel(message.level),
					   .Source = SourceFromLoggerName(std::string_view{message.logger_name.data(), message.logger_name.size()}),
					   .Message = std::string{message.payload.data(), message.payload.size()}};

		const std::scoped_lock lock{m_Mutex};

		entry.Sequence = m_NextSequence++;
		m_Entries.push_back(std::move(entry));
		++m_Revision;
		while (m_Entries.size() > m_Capacity)
		{
			m_Entries.pop_front();
		}
	}

	void RingBufferSink::flush_()
	{
		// sink_it_ stores each entry as it arrives, so nothing is held back to flush.
	}

	std::vector<LogEntry> RingBufferSink::TakeSnapshot() const
	{
		const std::scoped_lock lock{m_Mutex};
		return {m_Entries.begin(), m_Entries.end()};
	}

	std::size_t RingBufferSink::GetCount() const
	{
		const std::scoped_lock lock{m_Mutex};
		return m_Entries.size();
	}

	std::uint64_t RingBufferSink::GetRevision() const
	{
		const std::scoped_lock lock{m_Mutex};
		return m_Revision;
	}

	void RingBufferSink::Clear()
	{
		const std::scoped_lock lock{m_Mutex};
		m_Entries.clear();
		++m_Revision;
	}
}
