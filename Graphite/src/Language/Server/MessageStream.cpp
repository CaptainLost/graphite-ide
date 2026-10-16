#include "Language/Server/MessageStream.h"

#include "Core/Base/Ascii.h"

#include <charconv>

namespace {

	constexpr std::string_view kTerminator = "\r\n\r\n";
	constexpr std::string_view kContentLengthPrefix = "content-length:";

	constexpr std::size_t kBytesPerMegabyte = 1024u * 1024u;
	constexpr std::size_t kMaximumBodyBytes = 256u * kBytesPerMegabyte;

	bool StartsWithIgnoringCase(std::string_view text, std::string_view prefix)
	{
		return text.size() >= prefix.size() && Core::EqualsIgnoringCase(text.substr(0, prefix.size()), prefix);
	}

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

			if (StartsWithIgnoringCase(line, kContentLengthPrefix))
			{
				std::string_view value = line.substr(kContentLengthPrefix.size());
				while (!value.empty() && value.front() == ' ')
				{
					value.remove_prefix(1);
				}

				std::size_t length = 0;
				const char* const valueEnd = value.data() + value.size();
				const auto [stoppedAt, parseError] = std::from_chars(value.data(), valueEnd, length);

				const std::string_view afterNumber{stoppedAt, static_cast<std::size_t>(valueEnd - stoppedAt)};
				const bool onlySpacesAfterNumber = Core::ContainsOnlySpacesAndTabs(afterNumber);

				if (parseError == std::errc{} && onlySpacesAfterNumber && length <= kMaximumBodyBytes)
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
			const std::size_t terminatorPositionition = m_Buffer.find(kTerminator);
			if (terminatorPositionition == std::string::npos)
			{
				return std::nullopt;
			}

			const std::string_view header{m_Buffer.data(), terminatorPositionition};
			const std::size_t bodyStart = terminatorPositionition + kTerminator.size();

			const std::optional<std::size_t> length = FindContentLength(header);
			if (!length.has_value())
			{
				m_Buffer.erase(0, bodyStart);
				continue;
			}

			if (m_Buffer.size() - bodyStart < *length)
			{
				return std::nullopt;
			}

			std::string body = m_Buffer.substr(bodyStart, *length);

			// WARNING: Erasing from the front is linear, but the buffer holds at most one partial message, so an offset cursor buys nothing.
			m_Buffer.erase(0, bodyStart + *length);
			return body;
		}
	}
}
