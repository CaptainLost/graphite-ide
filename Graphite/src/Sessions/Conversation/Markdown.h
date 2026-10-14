#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class SpanStyle
	{
		Plain,
		Bold,
		Code,
		BoldCode
	};

	struct Span
	{
		SpanStyle Style = SpanStyle::Plain;
		std::string Text;

		friend bool operator==(const Span&, const Span&) = default;
	};

	enum class MarkdownKind
	{
		Paragraph,
		Heading,
		ListItem,
		Code
	};

	struct MarkdownBlock
	{
		MarkdownKind Kind = MarkdownKind::Paragraph;
		int Level = 0;
		std::string Marker;
		std::vector<Span> Spans;
		std::string Code;
		std::string Language;
	};

	std::vector<MarkdownBlock> ParseMarkdown(std::string_view text);

	std::vector<Span> ParseSpans(std::string_view text);

	struct PlacedWord
	{
		std::size_t Span = 0;
		std::string Text;
		float X = 0.0f;
		int Line = 0;
	};

	std::vector<PlacedWord> LayoutSpans(const std::vector<Span>& spans, float width,
										const std::function<float(std::string_view, SpanStyle)>& measure);
}
