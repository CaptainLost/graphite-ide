#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace Core {

	// The byte stream to a WSL agent: its stdin and stdout in production, an in-memory channel in
	// tests.
	class AgentPipe
	{
	public:
		virtual ~AgentPipe() = default;

		// Blocks until some bytes arrive; 0 once the other end is gone or Close was called.
		virtual std::size_t Read(std::span<char> buffer) = 0;

		virtual bool Write(std::string_view bytes) = 0;

		// Ends the stream; a Read waiting on another thread returns 0.
		virtual void Close() = 0;
	};
}
