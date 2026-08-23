#include "Core/Base/Base.h"

#include "Panels/ConsoleSelection.h"

#include "Core/Text/Columns.h"

#include <algorithm>
#include <string_view>
#include <tuple>

namespace Graphite {

	namespace {
		bool Precedes(const ConsoleAnchor& left, const ConsoleAnchor& right)
		{
			return std::tie(left.Sequence, left.Column) < std::tie(right.Sequence, right.Column);
		}

		std::size_t ColumnsIn(std::string_view message)
		{
			return Core::ColumnCount(message, Core::DefaultTabWidth);
		}
	}

	std::optional<ConsoleAnchor> AnchorAtPoint(const std::vector<Core::LogEntry>& entries,
											   const ConsoleLayout& layout, float x, float y)
	{
		if (entries.empty() || layout.RowHeight <= 0.0f || layout.CellWidth <= 0.0f)
			return std::nullopt;

		const float rows = (y - layout.OriginY) / layout.RowHeight;

		if (rows < 0.0f)
			return ConsoleAnchor{entries.front().Sequence, 0};

		const std::size_t row = static_cast<std::size_t>(rows);

		if (row >= entries.size())
		{
			return ConsoleAnchor{entries.back().Sequence, ColumnsIn(entries.back().Message)};
		}

		const std::size_t columns = ColumnsIn(entries[row].Message);

		// Rounded rather than truncated: the boundary the pointer is nearest to is the one the
		// sweep should stop at, so the right half of a character selects it.
		const float cells = (x - layout.OriginX) / layout.CellWidth + 0.5f;
		const std::size_t column =
			cells <= 0.0f ? 0 : std::min(static_cast<std::size_t>(cells), columns);

		return ConsoleAnchor{entries[row].Sequence, column};
	}

	void ConsoleSelection::Begin(ConsoleAnchor at)
	{
		m_Pressed = at;
		m_Dragged = at;
	}

	void ConsoleSelection::ExtendTo(ConsoleAnchor at)
	{
		m_Dragged = at;
	}

	void ConsoleSelection::SelectAll(const std::vector<Core::LogEntry>& entries)
	{
		if (entries.empty())
		{
			Clear();
			return;
		}

		m_Pressed = ConsoleAnchor{entries.front().Sequence, 0};
		m_Dragged = ConsoleAnchor{entries.back().Sequence, ColumnsIn(entries.back().Message)};
	}

	void ConsoleSelection::Clear()
	{
		m_Pressed = ConsoleAnchor{};
		m_Dragged = ConsoleAnchor{};
	}

	bool ConsoleSelection::IsEmpty() const
	{
		return m_Pressed.Sequence == m_Dragged.Sequence && m_Pressed.Column == m_Dragged.Column;
	}

	ConsoleAnchor ConsoleSelection::First() const
	{
		return Precedes(m_Pressed, m_Dragged) ? m_Pressed : m_Dragged;
	}

	ConsoleAnchor ConsoleSelection::Last() const
	{
		return Precedes(m_Pressed, m_Dragged) ? m_Dragged : m_Pressed;
	}

	std::optional<ConsoleColumnSpan> ConsoleSelection::ColumnsOn(std::uint64_t sequence,
																 std::size_t columnCount) const
	{
		if (IsEmpty())
			return std::nullopt;

		const ConsoleAnchor first = First();
		const ConsoleAnchor last = Last();

		if (sequence < first.Sequence || sequence > last.Sequence)
			return std::nullopt;

		const std::size_t from = sequence == first.Sequence ? std::min(first.Column, columnCount) : 0;
		const std::size_t to =
			sequence == last.Sequence ? std::min(last.Column, columnCount) : columnCount;

		if (from >= to)
			return std::nullopt;

		return ConsoleColumnSpan{from, to};
	}

	std::string ConsoleSelection::ExtractText(const std::vector<Core::LogEntry>& entries) const
	{
		if (IsEmpty())
			return {};

		const ConsoleAnchor first = First();
		const ConsoleAnchor last = Last();

		std::string text;
		// Counted rather than read off text.empty(), so a first line the selection only
		// touches the end of still ends in a line break instead of quietly merging into the
		// next one.
		std::size_t taken = 0;

		for (const Core::LogEntry& entry : entries)
		{
			if (entry.Sequence < first.Sequence || entry.Sequence > last.Sequence)
				continue;

			const std::size_t columns = ColumnsIn(entry.Message);
			const std::size_t from =
				entry.Sequence == first.Sequence ? std::min(first.Column, columns) : 0;
			const std::size_t to =
				entry.Sequence == last.Sequence ? std::min(last.Column, columns) : columns;

			if (taken++ > 0)
				text.append("\n");

			if (from < to)
			{
				const std::size_t fromByte =
					Core::ByteOfColumn(entry.Message, from, Core::DefaultTabWidth);
				const std::size_t toByte =
					Core::ByteOfColumn(entry.Message, to, Core::DefaultTabWidth);

				text.append(entry.Message, fromByte, toByte - fromByte);
			}
		}

		return text;
	}
}
