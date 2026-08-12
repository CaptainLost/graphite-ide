#pragma once

#include "Core/Base/Log.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Core
{
    /// Sink that holds the last N entries. Data source for the console panel in the IDE.
    class RingBufferSink final : public ILogSink
    {
    public:
        explicit RingBufferSink(std::size_t capacity);

        void Write(const LogEntry& entry) override;

        [[nodiscard]] std::vector<LogEntry> Snapshot() const;
        [[nodiscard]] std::size_t Count() const;
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

        void Clear();

    private:
        mutable std::mutex m_mutex;
        std::deque<LogEntry> m_entries;
        std::size_t m_capacity;
    };
}
