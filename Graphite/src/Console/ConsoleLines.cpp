#include "Console/ConsoleLines.h"

#include "Core/Base/Base.h"

#include "Core/Text/Columns.h"
#include "Core/Text/WordBounds.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace Graphite {

	namespace {
		std::size_t ColumnsIn(std::string_view message)
		{
			return Core::ColumnCount(message, Core::DefaultTabWidth);
		}

		std::int64_t LineOf(const Core::LogEntry& entry)
		{
			return static_cast<std::int64_t>(entry.Sequence);
		}

		const Core::LogEntry* Find(const std::vector<Core::LogEntry>& shown, std::int64_t line)
		{
			for (const Core::LogEntry& entry : shown)
			{
				if (LineOf(entry) == line)
				{
					return &entry;
				}
			}

			return nullptr;
		}

		// An end of the selection moved from columns to bytes of its entry. An entry that is not
		// shown has no text to convert against, and nothing is joined from it anyway.
		Core::TextPoint ToBytes(const std::vector<Core::LogEntry>& shown, Core::TextPoint at)
		{
			if (const Core::LogEntry* entry = Find(shown, at.Line))
			{
				at.Column = Core::ByteOfColumn(entry->Message, at.Column, Core::DefaultTabWidth);
			}

			return at;
		}
	}

	std::optional<Core::TextPoint> PointAt(const std::vector<Core::LogEntry>& shown,
										   const ConsoleLayout& layout, float x, float y)
	{
		if (shown.empty() || layout.RowHeight <= 0.0f || layout.CellWidth <= 0.0f)
		{
			return std::nullopt;
		}

		const float rows = (y - layout.OriginY) / layout.RowHeight;

		if (rows < 0.0f)
		{
			return Core::TextPoint{LineOf(shown.front()), 0};
		}

		const std::size_t row = static_cast<std::size_t>(rows);

		if (row >= shown.size())
		{
			return Core::TextPoint{LineOf(shown.back()), ColumnsIn(shown.back().Message)};
		}

		const std::size_t columns = ColumnsIn(shown[row].Message);

		// Rounded rather than truncated: the boundary the pointer is nearest to is the one the
		// sweep should stop at, so the right half of a character selects it.
		const float cells = (x - layout.OriginX) / layout.CellWidth + 0.5f;
		const std::size_t column =
			cells <= 0.0f ? 0 : std::min(static_cast<std::size_t>(cells), columns);

		return Core::TextPoint{LineOf(shown[row]), column};
	}

	std::string SelectedConsoleText(const Core::TextSelection& selection,
									const std::vector<Core::LogEntry>& shown)
	{
		if (selection.IsEmpty())
		{
			return {};
		}

		Core::TextSelection inBytes;
		inBytes.Select(ToBytes(shown, selection.First()), ToBytes(shown, selection.Last()));

		std::vector<Core::SelectableLine> lines;
		lines.reserve(shown.size());

		for (const Core::LogEntry& entry : shown)
		{
			lines.push_back(Core::SelectableLine{LineOf(entry), entry.Message});
		}

		return Core::JoinSelected(inBytes, lines);
	}

	Core::ColumnSpan ConsoleWordAt(const std::vector<Core::LogEntry>& shown, Core::TextPoint at)
	{
		const Core::LogEntry* entry = Find(shown, at.Line);

		if (entry == nullptr)
		{
			return Core::ColumnSpan{at.Column, at.Column};
		}

		const std::size_t byte = Core::ByteOfColumn(entry->Message, at.Column, Core::DefaultTabWidth);
		const Core::ColumnSpan bytes = Core::WordSpanAt(entry->Message, byte, Core::WordKind::Code);

		return Core::ColumnSpan{Core::ColumnOfByte(entry->Message, bytes.From, Core::DefaultTabWidth),
								Core::ColumnOfByte(entry->Message, bytes.To, Core::DefaultTabWidth)};
	}
}
