#include "Sessions/Requests/SentTextLayout.h"

#include "Core/Text/Encoding.h"

#include <algorithm>

namespace Graphite {

	namespace {
		void WrapLine(std::string_view text, std::size_t lineBegin, std::size_t lineEnd, std::size_t columns,
					  ByteRanges& wrappedRanges)
		{
			if (lineBegin == lineEnd)
			{
				wrappedRanges.emplace_back(lineBegin, lineEnd);
				return;
			}

			std::size_t rowBegin = lineBegin;

			while (rowBegin < lineEnd)
			{
				const bool rowFollowsBreak = rowBegin > lineBegin;

				if (rowFollowsBreak && text[rowBegin] == ' ')
				{
					++rowBegin;

					if (rowBegin == lineEnd)
					{
						break;
					}
				}

				std::size_t position = rowBegin;
				std::size_t columnsUsed = 0;
				std::size_t afterLastSpace = rowBegin;

				while (position < lineEnd)
				{
					const std::size_t characterColumns = text[position] == '\t' ? kTabColumns : 1;
					const bool isFirstCharacterOfRow = position == rowBegin;

					if (columnsUsed + characterColumns > columns && !isFirstCharacterOfRow)
					{
						break;
					}

					const std::size_t nextPosition =
						std::min(position + Core::Utf8SequenceLength(text[position]), lineEnd);

					if (text[position] == ' ')
					{
						afterLastSpace = nextPosition;
					}

					columnsUsed += characterColumns;
					position = nextPosition;
				}

				const bool breakAtLastSpace = position < lineEnd && afterLastSpace > rowBegin;
				const std::size_t rowEnd = breakAtLastSpace ? afterLastSpace : position;
				wrappedRanges.emplace_back(rowBegin, rowEnd);
				rowBegin = rowEnd;
			}
		}
	}

	ByteRanges WrapLines(std::string_view text, std::size_t columns)
	{
		ByteRanges wrappedRanges;
		const std::size_t usableColumns = std::max<std::size_t>(columns, 1);
		std::size_t lineBegin = 0;

		while (lineBegin < text.size())
		{
			const std::size_t newlinePosition = text.find('\n', lineBegin);
			const bool lastLine = newlinePosition == std::string_view::npos;
			const std::size_t lineEnd = lastLine ? text.size() : newlinePosition;
			std::size_t visibleEnd = lineEnd;

			if (visibleEnd > lineBegin && text[visibleEnd - 1] == '\r')
			{
				--visibleEnd;
			}

			WrapLine(text, lineBegin, visibleEnd, usableColumns, wrappedRanges);

			if (lastLine)
			{
				break;
			}

			lineBegin = newlinePosition + 1;
		}

		return wrappedRanges;
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

		const std::size_t responseIndex = segment - request.Sent.size();

		return responseIndex < request.Response.size() ? std::string_view{request.Response[responseIndex].Text}
													   : std::string_view{};
	}

	bool IsEmptyThinking(const ApiRequest& request, std::size_t segment)
	{
		if (segment < request.Sent.size())
		{
			return false;
		}

		const std::size_t responseIndex = segment - request.Sent.size();

		return responseIndex < request.Response.size() &&
			   request.Response[responseIndex].Kind == ResponseKind::Thinking &&
			   request.Response[responseIndex].Text.empty();
	}

	std::string_view DisplayText(const ApiRequest& request, std::size_t segment)
	{
		if (IsEmptyThinking(request, segment))
		{
			return "The model reasoned here, but the API returns reasoning hidden: only an encrypted signature "
				   "reaches Claude Code, so there is no text to show. Its tokens are part of this reply's output.";
		}

		return SegmentText(request, segment);
	}

	std::string AllSegmentText(const ApiRequest& request)
	{
		std::string allText;

		for (std::size_t segment = 0; segment < SegmentCount(request); ++segment)
		{
			if (segment > 0)
			{
				allText += "\n\n";
			}

			allText += SegmentText(request, segment);
		}

		return allText;
	}

	void SentTextLayout::Build(const std::vector<std::string_view>& segments, std::size_t columns)
	{
		m_Rows.clear();
		m_FirstRows.clear();

		for (std::size_t segment = 0; segment < segments.size(); ++segment)
		{
			m_FirstRows.push_back(m_Rows.size());
			m_Rows.push_back({.Segment = segment, .Header = true});

			for (const auto& [rowBegin, rowEnd] : WrapLines(segments[segment], columns))
			{
				m_Rows.push_back({.Segment = segment, .Begin = rowBegin, .End = rowEnd});
			}
		}
	}

	std::size_t SentTextLayout::FirstRowOf(std::size_t segment) const
	{
		return segment < m_FirstRows.size() ? m_FirstRows[segment] : m_Rows.size();
	}
}
