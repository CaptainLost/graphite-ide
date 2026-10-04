#include "Sessions/Conversation/Markdown.h"

#include <algorithm>
#include <cctype>

namespace Graphite {

	namespace {
		std::string_view TrimLeft(std::string_view text)
		{
			const std::size_t start = text.find_first_not_of(' ');
			return start == std::string_view::npos ? std::string_view{} : text.substr(start);
		}

		// Appends plain text, merging into the span before it when that has the same style.
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

		// `text` as spans; within bold, everything is bold and a code span is bold code. Bold
		// does not open inside bold: the first "**" after an opening one is what closes it.
		std::vector<Span> ParseSpansIn(std::string_view text, bool bold)
		{
			std::vector<Span> spans;
			std::size_t at = 0;

			while (at < text.size())
			{
				if (text[at] == '`')
				{
					const std::size_t close = text.find('`', at + 1);
					if (close != std::string_view::npos)
					{
						spans.push_back({bold ? SpanStyle::BoldCode : SpanStyle::Code,
										 std::string{text.substr(at + 1, close - at - 1)}});
						at = close + 1;
						continue;
					}
				}
				else if (!bold && text.substr(at).starts_with("**"))
				{
					const std::size_t close = text.find("**", at + 2);
					if (close != std::string_view::npos && close > at + 2)
					{
						for (Span& inner : ParseSpansIn(text.substr(at + 2, close - at - 2), true))
						{
							spans.push_back(std::move(inner));
						}
						at = close + 2;
						continue;
					}
				}

				// Text up to the next character that could open a span.
				std::size_t next = text.find_first_of("`*", at + 1);
				if (next == std::string_view::npos)
				{
					next = text.size();
				}

				AddText(spans, bold ? SpanStyle::Bold : SpanStyle::Plain, text.substr(at, next - at));
				at = next;
			}

			return spans;
		}

		// A line that opens an ordered item: digits, a dot, a space. Returns the marker length.
		std::size_t OrderedMarker(std::string_view line)
		{
			std::size_t digits = 0;
			while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])) != 0)
			{
				++digits;
			}

			if (digits == 0 || digits + 1 >= line.size() || line[digits] != '.' || line[digits + 1] != ' ')
			{
				return 0;
			}

			return digits + 1;
		}
	}

	std::vector<Span> ParseSpans(std::string_view text)
	{
		return ParseSpansIn(text, false);
	}

	std::vector<MarkdownBlock> ParseMarkdown(std::string_view text)
	{
		std::vector<MarkdownBlock> blocks;
		std::string paragraph;

		const auto flush = [&] {
			if (!paragraph.empty())
			{
				blocks.push_back({.Kind = MarkdownKind::Paragraph, .Spans = ParseSpans(paragraph)});
			}
			paragraph.clear();
		};

		std::size_t start = 0;

		while (start <= text.size())
		{
			std::size_t end = text.find('\n', start);
			if (end == std::string_view::npos)
			{
				end = text.size();
			}

			std::string_view line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}

			const std::string_view trimmed = TrimLeft(line);

			if (trimmed.starts_with("```"))
			{
				flush();

				MarkdownBlock code{.Kind = MarkdownKind::Code, .Language = std::string{TrimLeft(trimmed.substr(3))}};
				start = end + 1;

				// Until the closing fence, or the end of the text when there is none.
				while (start <= text.size())
				{
					std::size_t codeEnd = text.find('\n', start);
					if (codeEnd == std::string_view::npos)
					{
						codeEnd = text.size();
					}

					std::string_view codeLine = text.substr(start, codeEnd - start);
					if (!codeLine.empty() && codeLine.back() == '\r')
					{
						codeLine.remove_suffix(1);
					}

					if (TrimLeft(codeLine).starts_with("```"))
					{
						start = codeEnd + 1;
						break;
					}

					if (!code.Code.empty())
					{
						code.Code += '\n';
					}
					code.Code += codeLine;
					start = codeEnd + 1;
				}

				blocks.push_back(std::move(code));
				continue;
			}

			// A '#' line only counts as a heading up to level 6 with a space after the hashes;
			// zero means "not a heading", which a '#'-line falls through on just like any other
			// paragraph text.
			std::size_t headingHashes = 0;
			if (trimmed.starts_with('#'))
			{
				const std::size_t hashes = trimmed.find_first_not_of('#');
				if (hashes != std::string_view::npos && hashes <= 6 && trimmed[hashes] == ' ')
				{
					headingHashes = hashes;
				}
			}

			if (trimmed.empty())
			{
				flush();
			}
			else if (trimmed.starts_with('|'))
			{
				// A table row. Tables are not laid out as grids, but each row stays a line of
				// its own rather than running into its neighbours as one paragraph.
				flush();
				blocks.push_back({.Kind = MarkdownKind::Paragraph, .Spans = ParseSpans(trimmed)});
			}
			else if (headingHashes > 0)
			{
				flush();
				blocks.push_back({.Kind = MarkdownKind::Heading,
								  .Level = static_cast<int>(std::min<std::size_t>(headingHashes, 3)),
								  .Spans = ParseSpans(TrimLeft(trimmed.substr(headingHashes)))});
			}
			else if (trimmed.starts_with("- ") || trimmed.starts_with("* ") || OrderedMarker(trimmed) > 0)
			{
				flush();

				const std::size_t ordered = OrderedMarker(trimmed);
				const std::size_t markerLength = ordered > 0 ? ordered : 1;
				const int depth = static_cast<int>((line.size() - trimmed.size()) / 2);

				blocks.push_back({.Kind = MarkdownKind::ListItem,
								  .Level = depth,
								  .Marker = ordered > 0 ? std::string{trimmed.substr(0, ordered)} : "\xe2\x80\xa2",
								  .Spans = ParseSpans(TrimLeft(trimmed.substr(markerLength)))});
			}
			else
			{
				paragraph += (paragraph.empty() ? "" : " ") + std::string{trimmed};
			}

			start = end + 1;
		}

		flush();
		return blocks;
	}

	std::vector<PlacedWord> LayoutSpans(const std::vector<Span>& spans, float width,
										const std::function<float(std::string_view, SpanStyle)>& measure)
	{
		std::vector<PlacedWord> words;
		float x = 0.0f;
		int line = 0;

		for (std::size_t index = 0; index < spans.size(); ++index)
		{
			const std::string& text = spans[index].Text;
			std::size_t at = 0;

			while (at < text.size())
			{
				// A word and the spaces after it travel together.
				std::size_t end = text.find(' ', at);
				end = end == std::string::npos ? text.size() : text.find_first_not_of(' ', end);
				if (end == std::string::npos)
				{
					end = text.size();
				}

				const std::string word = text.substr(at, end - at);
				const float wordWidth = measure(word, spans[index].Style);

				if (x > 0.0f && x + wordWidth > width)
				{
					++line;
					x = 0.0f;
				}

				words.push_back({index, word, x, line});
				x += wordWidth;
				at = end;
			}
		}

		return words;
	}
}
