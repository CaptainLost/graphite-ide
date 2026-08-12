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

        [[nodiscard]] std::vector<LogEntry> GetSnapshot() const;
        [[nodiscard]] std::size_t GetCount() const;
        [[nodiscard]] std::size_t GetCapacity() const noexcept { return m_Capacity; }

        void Clear();

    private:
        mutable std::mutex m_Mutex;
        std::deque<LogEntry> m_Entries;
        std::size_t m_Capacity;
    };
}
