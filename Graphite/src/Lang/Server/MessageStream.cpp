#include "Lang/Server/MessageStream.h"

#include <cctype>
#include <charconv>

namespace {

	constexpr std::string_view kTerminator = "\r\n\r\n";
	constexpr std::string_view kContentLengthPrefix = "content-length:";

	bool IEquals(char a, char b)
	{
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	}

	// Case-insensitive prefix match: the header name's casing is not guaranteed by the protocol,
	// even though every real client sends it the same way.
	bool IStartsWith(std::string_view text, std::string_view prefix)
	{
		if (text.size() < prefix.size())
		{
			return false;
		}

		for (std::size_t i = 0; i < prefix.size(); ++i)
		{
			if (!IEquals(text[i], prefix[i]))
			{
				return false;
			}
		}

		return true;
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
				const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), length);
				if (ec == std::errc{})
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
