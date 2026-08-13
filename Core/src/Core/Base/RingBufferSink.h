#pragma once

#include "Core/Base/Log.h"

#include <spdlog/sinks/base_sink.h>

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Core {

	// Keeps the last N messages in memory so a panel can render them, dropping the oldest
	// rather than letting a long session grow without limit. The entries carry the message
	// text alone: the clock, the writer and the severity stay fields a renderer can lay out or
	// filter on, rather than being formatted in and parsed back out.
	class RingBufferSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit RingBufferSink(std::size_t capacity);

		std::vector<LogEntry> TakeSnapshot() const;
		std::size_t GetCount() const;
		std::size_t GetCapacity() const { return m_Capacity; }

		void Clear();

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override;
		void flush_() override;

	private:
		// The base class only locks around sink_it_, and TakeSnapshot is called from wherever
		// a panel renders.
		mutable std::mutex m_Mutex;
		std::deque<LogEntry> m_Entries;
		std::size_t m_Capacity;
	};
}
