#include "Language/Server/MessageStream.h"

#include "Core/Base/Ascii.h"

#include <charconv>

namespace {

	constexpr std::string_view kTerminator = "\r\n\r\n";
	constexpr std::string_view kContentLengthPrefix = "content-length:";

	// Far past any message a language server sends - a semantic-token answer for a very large
	// file is tens of megabytes - and far short of what a corrupted header can claim. A length
	// over it is treated as no length: waiting for that many bytes would hold back every
	// message after it while the buffer grew without end.
	constexpr std::size_t kMaximumBody = 256u * 1024u * 1024u;

	// Case-insensitive prefix match: the header name's casing is not guaranteed by the protocol,
	// even though every real client sends it the same way.
	bool IStartsWith(std::string_view text, std::string_view prefix)
	{
		return text.size() >= prefix.size() && Core::EqualsIgnoringCase(text.substr(0, prefix.size()), prefix);
	}

	// Looks for a Content-Length line within the header block and parses its value. Returns
	// nothing when no such header is present.
	std::optional<std::size_t> FindContentLength(std::string_view header)
	{
		std::size_t lineStart = 0;

		while (lineStart <= header.size())
		{
			std::size_t lineEnd = header.find("\r\n", lineStart);
			if (lineEnd == std::string_view::npos)
			{
				lineEnd = header.size();
			}

			const std::string_view line = header.substr(lineStart, lineEnd - lineStart);

			if (IStartsWith(line, kContentLengthPrefix))
			{
				std::string_view value = line.substr(kContentLengthPrefix.size());
				while (!value.empty() && value.front() == ' ')
				{
					value.remove_prefix(1);
				}

				std::size_t length = 0;
				const char* const end = value.data() + value.size();
				const auto [stopped, ec] = std::from_chars(value.data(), end, length);

				// The whole value has to be the number, spaces aside: "2x" is not a length of two.
				const std::string_view rest{stopped, static_cast<std::size_t>(end - stopped)};
				const bool onlySpaces = rest.find_first_not_of(" \t") == std::string_view::npos;

				if (ec == std::errc{} && onlySpaces && length <= kMaximumBody)
				{
					return length;
				}
			}

			if (lineEnd == header.size())
			{
				break;
			}

			lineStart = lineEnd + 2;
		}

		return std::nullopt;
	}
}

namespace Graphite {

	void MessageStream::Append(std::string_view bytes)
	{
		m_Buffer.append(bytes);
	}

	std::optional<std::string> MessageStream::Take()
	{
		for (;;)
		{
			const std::size_t terminatorPos = m_Buffer.find(kTerminator);
			if (terminatorPos == std::string::npos)
			{
				return std::nullopt;
			}

			const std::string_view header{m_Buffer.data(), terminatorPos};
			const std::size_t bodyStart = terminatorPos + kTerminator.size();

			const std::optional<std::size_t> length = FindContentLength(header);
			if (!length.has_value())
			{
				// A header block with no usable Content-Length can never be turned into a
				// message. Drop it and keep scanning, rather than returning nothing and leaving
				// the same unparseable bytes to be re-examined forever.
				m_Buffer.erase(0, bodyStart);
				continue;
			}

			if (m_Buffer.size() - bodyStart < *length)
			{
				// The body has not fully arrived yet; leave the header in place so the next
				// Append does not need to be re-parsed.
				return std::nullopt;
			}

			std::string body = m_Buffer.substr(bodyStart, *length);

			// Erasing from the front shifts everything after it, so this is O(remaining buffer)
			// rather than O(1). That is fine here because a body is taken as soon as it is
			// complete: the buffer never holds more than one partial message plus whatever
			// arrived alongside it, not a backlog. Resist swapping this for an offset cursor —
			// it buys nothing at these sizes and rates, and it drags in the bookkeeping needed
			// to keep the cursor and the buffer's capacity from growing without bound.
			m_Buffer.erase(0, bodyStart + *length);
			return body;
		}
	}
}
