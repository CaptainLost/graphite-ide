#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The transport half of the protocol: a header block ending in a blank line, whose
	// Content-Length names how many bytes of body follow. Split from JsonRpc because a message
	// arriving in pieces is a property of the pipe, not of the protocol, and because framing is
	// then testable without a process or a parser.
	class MessageStream
	{
	public:
		void Append(std::string_view bytes);

		// Removes and returns the next complete body. Nothing means the buffer does not hold a
		// whole one yet — not that the stream has ended.
		std::optional<std::string> Take();

	private:
		std::string m_Buffer;
	};
}
