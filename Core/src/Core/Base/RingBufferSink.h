#pragma once

#include "Core/Base/Log.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Core {

	class RingBufferSink final : public ILogSink
	{
	public:
		explicit RingBufferSink(std::size_t capacity);

		void Write(const LogEntry& entry) override;

		std::vector<LogEntry> GetSnapshot() const;
		std::size_t GetCount() const;
		std::size_t GetCapacity() const { return m_Capacity; }

		void Clear();

	private:
		mutable std::mutex m_Mutex;
		std::deque<LogEntry> m_Entries;
		std::size_t m_Capacity;
	};
}