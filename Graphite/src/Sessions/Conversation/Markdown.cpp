#include "Sessions/Conversation/Markdown.h"

#include "Core/Base/Strings.h"

#include <algorithm>
#include <cctype>

namespace Graphite {

	namespace {
		constexpr std::string_view kBoldMarker = "**";
		constexpr std::string_view kCodeFence = "```";
		constexpr char kInlineCodeMarker = '`';
		constexpr std::string_view kSpanMarkers = "`*";
		constexpr const char* kBulletMarker = "\xe2\x80\xa2";

		constexpr std::size_t kMaxHeadingHashes = 6;
		constexpr std::size_t kMaxRenderedHeadingLevel = 3;
		constexpr std::size_t kSpacesPerListDepth = 2;

		std::string_view WithoutLeadingSpaces(std::string_view text)
		{
			const std::size_t firstNonSpace = text.find_first_not_of(' ');
			return firstNonSpace == std::string_view::npos ? std::string_view{} : text.substr(firstNonSpace);
		}

		std::size_t LineEndFrom(std::string_view text, std::size_t lineStart)
		{
			const std::size_t newlineIndex = text.find('\n', lineStart);
			return newlineIndex == std::string_view::npos ? text.size() : newlineIndex;
		}

		std::string_view WithoutTrailingCarriageReturn(std::string_view line)
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}

			return line;
		}

		void AddText(std::vector<Span>& spans, SpanStyle style, std::string_view text)
		{
			if (text.empty())
			{
				return;
			}

			if (!spans.empty() && spans.back().Style == style)
			{
				spans.back().Text += text;
			}
			else
			{
				spans.push_back({style, std::string{text}});
			}
		}

		std::vector<Span> ParseSpansIn(std::string_view text, bool insideBold)
		{
			std::vector<Span> spans;
			std::size_t position = 0;

			while (position < text.size())
			{
				if (text[position] == kInlineCodeMarker)
				{
					const std::size_t closingIndex = text.find(kInlineCodeMarker, position + 1);
					if (closingIndex != std::string_view::npos)
					{
						const SpanStyle codeStyle = insideBold ? SpanStyle::BoldCode : SpanStyle::Code;
						spans.push_back({codeStyle, std::string{text.substr(position + 1, closingIndex - position - 1)}});
						position = closingIndex + 1;
						continue;
					}
				}
				else if (!insideBold && text.substr(position).starts_with(kBoldMarker))
				{
					const std::size_t boldStart = position + kBoldMarker.size();
					const std::size_t closingIndex = text.find(kBoldMarker, boldStart);
					if (closingIndex != std::string_view::npos && closingIndex > boldStart)
					{
						const bool innerIsBold = true;
						for (Span& innerSpan : ParseSpansIn(text.substr(boldStart, closingIndex - boldStart), innerIsBold))
						{
							spans.push_back(std::move(innerSpan));
						}
						position = closingIndex + kBoldMarker.size();
						continue;
					}
				}

				std::size_t nextMarkerIndex = text.find_first_of(kSpanMarkers, position + 1);
				if (nextMarkerIndex == std::string_view::npos)
				{
					nextMarkerIndex = text.size();
				}

				const SpanStyle textStyle = insideBold ? SpanStyle::Bold : SpanStyle::Plain;
				AddText(spans, textStyle, text.substr(position, nextMarkerIndex - position));
				position = nextMarkerIndex;
			}

			return spans;
		}

		std::size_t OrderedMarkerLength(std::string_view line)
		{
			std::size_t digitCount = 0;
			while (digitCount < line.size() && std::isdigit(static_cast<unsigned char>(line[digitCount])) != 0)
			{
				++digitCount;
			}

			const std::size_t dotIndex = digitCount;
			const std::size_t spaceIndex = digitCount + 1;
			if (digitCount == 0 || spaceIndex >= line.size() || line[dotIndex] != '.' || line[spaceIndex] != ' ')
			{
				return 0;
			}

			return digitCount + 1;
		}

		std::size_t HeadingHashCount(std::string_view trimmedLine)
		{
			if (!trimmedLine.starts_with('#'))
			{
				return 0;
			}

			const std::size_t hashCount = trimmedLine.find_first_not_of('#');
			if (hashCount != std::string_view::npos && hashCount <= kMaxHeadingHashes && trimmedLine[hashCount] == ' ')
			{
				return hashCount;
			}

			return 0;
		}

		void FlushParagraph(std::vector<MarkdownBlock>& blocks, std::string& paragraph)
		{
			if (!paragraph.empty())
			{
				blocks.push_back({.Kind = MarkdownKind::Paragraph, .Spans = ParseSpans(paragraph)});
			}
			paragraph.clear();
		}
	}

	std::vector<Span> ParseSpans(std::string_view text)
	{
		const bool startsInsideBold = false;
		return ParseSpansIn(text, startsInsideBold);
	}

	std::vector<MarkdownBlock> ParseMarkdown(std::string_view text)
	{
		std::vector<MarkdownBlock> blocks;
		std::string paragraph;
		std::size_t lineStart = 0;

		while (lineStart <= text.size())
		{
			const std::size_t lineEnd = LineEndFrom(text, lineStart);
			const std::string_view line = WithoutTrailingCarriageReturn(text.substr(lineStart, lineEnd - lineStart));
			const std::string_view trimmed = WithoutLeadingSpaces(line);

			if (trimmed.starts_with(kCodeFence))
			{
				FlushParagraph(blocks, paragraph);

				MarkdownBlock code{.Kind = MarkdownKind::Code,
								   .Language = std::string{WithoutLeadingSpaces(trimmed.substr(kCodeFence.size()))}};
				lineStart = lineEnd + 1;

				while (lineStart <= text.size())
				{
					const std::size_t codeLineEnd = LineEndFrom(text, lineStart);
					const std::string_view codeLine =
						WithoutTrailingCarriageReturn(text.substr(lineStart, codeLineEnd - lineStart));

					if (WithoutLeadingSpaces(codeLine).starts_with(kCodeFence))
					{
						lineStart = codeLineEnd + 1;
						break;
					}

					if (!code.Code.empty())
					{
						code.Code += '\n';
					}
					code.Code += codeLine;
					lineStart = codeLineEnd + 1;
				}

				blocks.push_back(std::move(code));
				continue;
			}

			const std::size_t headingHashes = HeadingHashCount(trimmed);

			if (trimmed.empty())
			{
				FlushParagraph(blocks, paragraph);
			}
			else if (trimmed.starts_with('|'))
			{
				FlushParagraph(blocks, paragraph);
				blocks.push_back({.Kind = MarkdownKind::Paragraph, .Spans = ParseSpans(trimmed)});
			}
			else if (headingHashes > 0)
			{
				FlushParagraph(blocks, paragraph);
				blocks.push_back(
					{.Kind = MarkdownKind::Heading,
					 .Level = static_cast<int>(std::min(headingHashes, kMaxRenderedHeadingLevel)),
					 .Spans = ParseSpans(WithoutLeadingSpaces(trimmed.substr(headingHashes)))});
			}
			else if (trimmed.starts_with("- ") || trimmed.starts_with("* ") || OrderedMarkerLength(trimmed) > 0)
			{
				FlushParagraph(blocks, paragraph);

				const std::size_t orderedMarkerLength = OrderedMarkerLength(trimmed);
				const bool isOrdered = orderedMarkerLength > 0;
				const std::size_t markerLength = isOrdered ? orderedMarkerLength : 1;
				const int depth = static_cast<int>((line.size() - trimmed.size()) / kSpacesPerListDepth);

				blocks.push_back({.Kind = MarkdownKind::ListItem,
								  .Level = depth,
								  .Marker = isOrdered ? std::string{trimmed.substr(0, orderedMarkerLength)} : kBulletMarker,
								  .Spans = ParseSpans(WithoutLeadingSpaces(trimmed.substr(markerLength)))});
			}
			else
			{
				Core::AppendSeparated(paragraph, " ", trimmed);
			}

			lineStart = lineEnd + 1;
		}

		FlushParagraph(blocks, paragraph);
		return blocks;
	}

	std::vector<PlacedWord> LayoutSpans(const std::vector<Span>& spans, float width,
										const std::function<float(std::string_view, SpanStyle)>& measure)
	{
		std::vector<PlacedWord> words;
		float x = 0.0f;
		int lineIndex = 0;

		for (std::size_t spanIndex = 0; spanIndex < spans.size(); ++spanIndex)
		{
			const std::string& text = spans[spanIndex].Text;
			std::size_t wordStart = 0;

			while (wordStart < text.size())
			{
				std::size_t wordEnd = text.find(' ', wordStart);
				wordEnd = wordEnd == std::string::npos ? text.size() : text.find_first_not_of(' ', wordEnd);
				if (wordEnd == std::string::npos)
				{
					wordEnd = text.size();
				}

				const std::string word = text.substr(wordStart, wordEnd - wordStart);
				const float wordWidth = measure(word, spans[spanIndex].Style);

				if (x > 0.0f && x + wordWidth > width)
				{
					++lineIndex;
					x = 0.0f;
				}

				words.push_back({spanIndex, word, x, lineIndex});
				x += wordWidth;
				wordStart = wordEnd;
			}
		}

		return words;
	}
}
