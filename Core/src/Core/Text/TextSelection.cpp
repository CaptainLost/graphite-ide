#include "corepch.h"

#include "Core/Text/TextSelection.h"

#include "Core/Text/Encoding.h"

#include <algorithm>

namespace Core {

	void TextSelection::Begin(TextPoint pressedPoint)
	{
		m_Pressed = pressedPoint;
		m_Dragged = pressedPoint;
	}

	void TextSelection::ExtendTo(TextPoint draggedPoint)
	{
		m_Dragged = draggedPoint;
	}

	void TextSelection::Select(TextPoint pressedPoint, TextPoint draggedPoint)
	{
		m_Pressed = pressedPoint;
		m_Dragged = draggedPoint;
	}

	void TextSelection::Clear()
	{
		m_Pressed = TextPoint{};
		m_Dragged = TextPoint{};
	}

	bool TextSelection::IsEmpty() const
	{
		return m_Pressed == m_Dragged;
	}

	TextPoint TextSelection::First() const
	{
		return std::min(m_Pressed, m_Dragged);
	}

	TextPoint TextSelection::Last() const
	{
		return std::max(m_Pressed, m_Dragged);
	}

	std::optional<ColumnSpan> TextSelection::SpanOn(std::int64_t line, std::size_t lineLength) const
	{
		if (IsEmpty())
		{
			return std::nullopt;
		}

		const TextPoint first = First();
		const TextPoint last = Last();

		if (line < first.Line || line > last.Line)
		{
			return std::nullopt;
		}

		const bool selectionStartsOnLine = line == first.Line;
		const bool selectionEndsOnLine = line == last.Line;

		const std::size_t selectedFrom = selectionStartsOnLine ? std::min(first.Column, lineLength) : 0;
		const std::size_t selectedTo = selectionEndsOnLine ? std::min(last.Column, lineLength) : lineLength;

		return ColumnSpan{.From = selectedFrom, .To = std::max(selectedFrom, selectedTo)};
	}

	std::string JoinSelected(const TextSelection& selection, std::span<const SelectableLine> shownLines)
	{
		std::string joined;
		bool anyLineJoined = false;

		for (const SelectableLine& shownLine : shownLines)
		{
			const std::optional<ColumnSpan> span = selection.SpanOn(shownLine.Line, shownLine.Text.size());

			if (!span.has_value())
			{
				continue;
			}

			if (anyLineJoined)
			{
				joined.push_back('\n');
			}

			anyLineJoined = true;
			const std::size_t selectedStart = CharacterBoundaryAtOrBefore(shownLine.Text, span->From);
			const std::size_t selectedEnd = CharacterBoundaryAtOrBefore(shownLine.Text, span->To);
			joined.append(shownLine.Text.substr(selectedStart, selectedEnd - selectedStart));
		}

		return joined;
	}
}
