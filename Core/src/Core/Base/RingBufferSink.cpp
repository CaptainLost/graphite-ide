#include "corepch.h"

#include "Core/Base/RingBufferSink.h"

#include "Core/Base/Assert.h"

namespace Core
{
    RingBufferSink::RingBufferSink(std::size_t capacity) : m_Capacity(capacity)
    {
        CORE_ASSERT(capacity > 0, "RingBufferSink requires a capacity greater than zero");
    }

    void RingBufferSink::Write(const LogEntry& entry)
    {
        const std::scoped_lock lock{m_Mutex};

        m_Entries.push_back(entry);
        while (m_Entries.size() > m_Capacity)
            m_Entries.pop_front();
    }

    std::vector<LogEntry> RingBufferSink::GetSnapshot() const
    {
        const std::scoped_lock lock{m_Mutex};
        return {m_Entries.begin(), m_Entries.end()};
    }

    std::size_t RingBufferSink::GetCount() const
    {
        const std::scoped_lock lock{m_Mutex};
        return m_Entries.size();
    }

    void RingBufferSink::Clear()
    {
        const std::scoped_lock lock{m_Mutex};
        m_Entries.clear();
    }
}
