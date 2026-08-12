#pragma once

#include "Core/Base/Log.h"

#include <spdlog/sinks/base_sink.h>

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Core {

	// Keeps the last N messages in memory so a panel can render them. The buffer is
	// bounded because it lives for the whole run of the program: the oldest entry is
	// dropped rather than letting a long session grow without limit.
	//
	// The entries carry the message text alone. Everything a pattern would prepend - the
	// clock, the writer, the severity - is already a field a renderer can lay out or
	// filter on, so formatting it into the string would only have to be parsed back out.
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
		// The base class locks around sink_it_, but a reader calls TakeSnapshot from
		// wherever it renders, so the buffer needs a lock of its own that a const method
		// is allowed to take.
		mutable std::mutex m_Mutex;
		std::deque<LogEntry> m_Entries;
		std::size_t m_Capacity;
	};
}
