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
		Code
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

	// The subset of markdown Claude writes in replies and this view renders: headings,
	// paragraphs, list items, fenced code. Tables and links are left as text.
	struct MarkdownBlock
	{
		MarkdownKind Kind = MarkdownKind::Paragraph;

		// Heading: 1..3. List item: nesting depth from 0.
		int Level = 0;

		// List item: "•" for - and *, the number and dot for an ordered item.
		std::string Marker;

		std::vector<Span> Spans;

		// Code block: its text without the fences, and the language after the opening fence.
		std::string Code;
		std::string Language;
	};

	std::vector<MarkdownBlock> ParseMarkdown(std::string_view text);

	// **bold** and `code` within one block's text. A marker that is never closed is text.
	std::vector<Span> ParseSpans(std::string_view text);

	struct PlacedWord
	{
		std::size_t Span = 0;
		std::string Text;
		float X = 0.0f;
		int Line = 0;
	};

	// Places each word (with the spaces after it) left to right, starting a new line where the
	// next word would pass `width`. A word wider than the width is placed alone on its own
	// line rather than split. Pure, so wrapping is tested without ImGui; the view supplies the
	// measure.
	std::vector<PlacedWord> LayoutSpans(const std::vector<Span>& spans, float width,
										const std::function<float(std::string_view, SpanStyle)>& measure);
}
