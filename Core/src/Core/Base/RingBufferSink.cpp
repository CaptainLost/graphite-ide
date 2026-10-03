#include "corepch.h"

#include "Core/Base/RingBufferSink.h"

#include "Core/Base/Assert.h"

#include <string_view>

namespace Core {

	namespace {
		LogSource SourceOf(const spdlog::details::log_msg& msg)
		{
			const std::string_view name{msg.logger_name.data(), msg.logger_name.size()};
			return name == ToString(LogSource::Application) ? LogSource::Application
															: LogSource::Engine;
		}
	}

	RingBufferSink::RingBufferSink(std::size_t capacity) : m_Capacity(capacity)
	{
		CORE_ASSERT(capacity > 0, "RingBufferSink requires a capacity greater than zero");
	}

	void RingBufferSink::sink_it_(const spdlog::details::log_msg& msg)
	{
		LogEntry entry{FromBackendLevel(msg.level), SourceOf(msg),
					   std::string{msg.payload.data(), msg.payload.size()}};

		const std::scoped_lock lock{m_Mutex};

		entry.Sequence = m_NextSequence++;
		m_Entries.push_back(std::move(entry));
		++m_Revision;
		while (m_Entries.size() > m_Capacity)
			m_Entries.pop_front();
	}

	void RingBufferSink::flush_()
	{
		// Nothing is buffered on the way in, so there is nothing to push out.
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
