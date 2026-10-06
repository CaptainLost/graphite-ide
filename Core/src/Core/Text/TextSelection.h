#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Core {

	// Line is whatever the owner numbers its text by (a document line, a console entry's sequence
	// number, an absolute terminal row), and Column counts in the owner's unit.
	struct TextPoint
	{
		std::int64_t Line = 0;
		std::size_t Column = 0;

		std::strong_ordering operator<=>(const TextPoint&) const = default;
	};

	// Half-open: To is one past the last selected column.
	struct ColumnSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;

		bool operator==(const ColumnSpan&) const = default;
	};

	class TextSelection
	{
	public:
		void Begin(TextPoint pressedPoint);
		void ExtendTo(TextPoint draggedPoint);
		void Select(TextPoint pressedPoint, TextPoint draggedPoint);
		void Clear();

		bool IsEmpty() const;
		TextPoint First() const;
		TextPoint Last() const;

		TextPoint Pressed() const
		{
			return m_Pressed;
		}

		std::optional<ColumnSpan> SpanOn(std::int64_t line, std::size_t lineLength) const;

	private:
		TextPoint m_Pressed;
		TextPoint m_Dragged;
	};

	struct SelectableLine
	{
		std::int64_t Line = 0;
		std::string_view Text;
	};

	// The selection's columns must be byte offsets into each line's Text, whatever unit the owner otherwise counts columns in.
	std::string JoinSelected(const TextSelection& selection, std::span<const SelectableLine> shownLines);
}
