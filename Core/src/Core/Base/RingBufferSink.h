#pragma once

#include "Core/Base/Log.h"

#include <spdlog/sinks/base_sink.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Core {

	class RingBufferSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit RingBufferSink(std::size_t capacity);

		std::vector<LogEntry> TakeSnapshot() const;
		std::size_t GetCount() const;
		std::size_t GetCapacity() const { return m_Capacity; }
		std::uint64_t GetRevision() const;

		void Clear();

	protected:
		void sink_it_(const spdlog::details::log_msg& message) override;
		void flush_() override;

	private:
		// spdlog's base_sink takes its mutex only inside its own members, never around TakeSnapshot,
		// GetCount, GetRevision or Clear, which are called from wherever a panel renders, so the
		// entries need a lock of their own.
		mutable std::mutex m_Mutex;
		std::deque<LogEntry> m_Entries;
		std::size_t m_Capacity;
		std::uint64_t m_NextSequence = 1;
		std::uint64_t m_Revision = 0;
	};
}
