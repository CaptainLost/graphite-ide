#include "Sessions/SentTextLayout.h"

#include <algorithm>

namespace Graphite {

	namespace {
		// The length of the UTF-8 sequence `lead` begins; a stray continuation byte or an
		// invalid lead counts as one, so a malformed text still advances.
		std::size_t SequenceLength(unsigned char lead)
		{
			if ((lead & 0x80u) == 0)
			{
				return 1;
			}
			if ((lead & 0xE0u) == 0xC0u)
			{
				return 2;
			}
			if ((lead & 0xF0u) == 0xE0u)
			{
				return 3;
			}
			if ((lead & 0xF8u) == 0xF0u)
			{
				return 4;
			}
			return 1;
		}

		// Breaks the line [begin, end) of `text` at the width, appending its pieces.
		void WrapLine(std::string_view text, std::size_t begin, std::size_t end, std::size_t columns,
					  std::vector<std::pair<std::size_t, std::size_t>>& lines)
		{
			if (begin == end)
			{
				lines.emplace_back(begin, end);
				return;
			}

			std::size_t start = begin;

			while (start < end)
			{
				// A row the width broke right before a space would start with it; the space is
				// the break, so the row starts after it. Only on a row a break began: a line's
				// own leading space is part of its text.
				if (start > begin && text[start] == ' ')
				{
					++start;

					if (start == end)
					{
						break;
					}
				}

				std::size_t at = start;
				std::size_t used = 0;
				std::size_t afterSpace = start;

				while (at < end)
				{
					const std::size_t width = text[at] == '\t' ? kTabColumns : 1;

					// The first character goes on the line whatever its width, so a line always
					// moves on.
					if (used + width > columns && at > start)
					{
						break;
					}

					const std::size_t next = std::min(at + SequenceLength(static_cast<unsigned char>(text[at])), end);

					if (text[at] == ' ')
					{
						afterSpace = next;
					}

					used += width;
					at = next;
				}

				const std::size_t cut = at < end && afterSpace > start ? afterSpace : at;
				lines.emplace_back(start, cut);
				start = cut;
			}
		}
	}

	std::vector<std::pair<std::size_t, std::size_t>> WrapLines(std::string_view text, std::size_t columns)
	{
		std::vector<std::pair<std::size_t, std::size_t>> lines;
		const std::size_t width = std::max<std::size_t>(columns, 1);
		std::size_t start = 0;

		while (start < text.size())
		{
			const std::size_t newline = text.find('\n', start);
			const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
			std::size_t visibleEnd = lineEnd;

			if (visibleEnd > start && text[visibleEnd - 1] == '\r')
			{
				--visibleEnd;
			}

			WrapLine(text, start, visibleEnd, width, lines);

			if (newline == std::string_view::npos)
			{
				break;
			}

			start = newline + 1;
		}

		return lines;
	}

	std::size_t SegmentCount(const ApiRequest& request)
	{
		return request.Sent.size() + request.Response.size();
	}

	std::string_view SegmentText(const ApiRequest& request, std::size_t segment)
	{
		if (segment < request.Sent.size())
		{
			return request.Sent[segment].Text;
		}

		segment -= request.Sent.size();

		return segment < request.Response.size() ? std::string_view{request.Response[segment].Text} : std::string_view{};
	}

	std::string_view DisplayText(const ApiRequest& request, std::size_t segment)
	{
		if (segment >= request.Sent.size())
		{
			const std::size_t response = segment - request.Sent.size();

			if (response < request.Response.size() && request.Response[response].Kind == ResponseKind::Thinking &&
				request.Response[response].Text.empty())
			{
				return "The model reasoned here, but the API returns reasoning hidden: only an encrypted signature "
					   "reaches Claude Code, so there is no text to show. Its tokens are part of this reply's output.";
			}
		}

		return SegmentText(request, segment);
	}

	std::string AllSegmentText(const ApiRequest& request)
	{
		std::string all;

		for (std::size_t i = 0; i < SegmentCount(request); ++i)
		{
			if (i > 0)
			{
				all += "\n\n";
			}
			all += SegmentText(request, i);
		}

		return all;
	}

	void SentTextLayout::Build(const std::vector<std::string_view>& segments, std::size_t columns)
	{
		m_Rows.clear();
		m_FirstRows.clear();

		for (std::size_t segment = 0; segment < segments.size(); ++segment)
		{
			m_FirstRows.push_back(m_Rows.size());
			m_Rows.push_back({.Segment = segment, .Header = true});

			for (const auto& [begin, end] : WrapLines(segments[segment], columns))
			{
				m_Rows.push_back({.Segment = segment, .Begin = begin, .End = end});
			}
		}
	}

	std::size_t SentTextLayout::FirstRowOf(std::size_t segment) const
	{
		return segment < m_FirstRows.size() ? m_FirstRows[segment] : m_Rows.size();
	}
}
