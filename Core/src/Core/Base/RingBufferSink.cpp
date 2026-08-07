#include "corepch.h"

#include "Core/Base/RingBufferSink.h"

#include "Core/Base/Assert.h"

namespace core
{
    RingBufferSink::RingBufferSink(std::size_t capacity) : m_capacity(capacity)
    {
        CORE_ASSERT(capacity > 0, "RingBufferSink requires a capacity greater than zero");
    }

    void RingBufferSink::Write(const LogEntry& entry)
    {
        const std::scoped_lock lock{m_mutex};

        m_entries.push_back(entry);
        while (m_entries.size() > m_capacity)
            m_entries.pop_front();
    }

    std::vector<LogEntry> RingBufferSink::Snapshot() const
    {
        const std::scoped_lock lock{m_mutex};
        return {m_entries.begin(), m_entries.end()};
    }

    std::size_t RingBufferSink::Count() const
    {
        const std::scoped_lock lock{m_mutex};
        return m_entries.size();
    }

    void RingBufferSink::Clear()
    {
        const std::scoped_lock lock{m_mutex};
        m_entries.clear();
    }
}
