#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Core {

	// A position in text the owner numbers: a line number the owner chooses (a document line, a
	// console entry, an absolute terminal row) and a column in the owner's unit.
	struct TextPoint
	{
		std::int64_t Line = 0;
		std::size_t Column = 0;

		auto operator<=>(const TextPoint&) const = default;
	};

	// Half-open, in the owner's column unit.
	struct ColumnSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;

		bool operator==(const ColumnSpan&) const = default;
	};

	// Two ends over lines the owner numbers. The model holds no text (ADR 0503): it says which
	// columns of a line are selected, and the owner, who has the text, reads them.
	class TextSelection
	{
	public:
		// Both ends on `at`: the selection is empty.
		void Begin(TextPoint at);
		// Moves the dragged end; the pressed end stays (drag, Shift+click).
		void ExtendTo(TextPoint at);
		// Word, line and select-all: pressed = from, dragged = to.
		void Select(TextPoint from, TextPoint to);
		void Clear();

		bool IsEmpty() const;
		TextPoint First() const;
		TextPoint Last() const;

		TextPoint Pressed() const
		{
			return m_Pressed;
		}

		// Where the selection crosses `line`, clamped to `length`; nullopt when it does not reach
		// the line or is empty.
		std::optional<ColumnSpan> SpanOn(std::int64_t line, std::size_t length) const;

	private:
		TextPoint m_Pressed;
		TextPoint m_Dragged;
	};

	struct SelectableLine
	{
		std::int64_t Line = 0;
		// In the owner's column unit, which is bytes for JoinSelected.
		std::string_view Text;
	};

	// Joins the selected part of every line handed in (in order, already limited by the caller to
	// the lines it shows between First().Line and Last().Line), one per output line. Lines the
	// selection does not reach contribute nothing; columns are byte offsets into Text, clamped and
	// moved back to a UTF-8 boundary.
	std::string JoinSelected(const TextSelection& selection, std::span<const SelectableLine> lines);
}
