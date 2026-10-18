#include "Console/ConsoleLines.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Columns.h"
#include "Core/Text/WordBounds.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace Graphite {

	// WARNING: Rows are measured here, not read back from ImGui, because the selection must
	// know where a row is before it is drawn or the highlight lags a frame behind the pointer.
	// This arithmetic has to match the one ImGui walks as it emits the rows.
	ConsoleLayout MeasureConsoleLayout(float rowSpacing)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		Core::PushCodeFont();

		ConsoleLayout layout;
		layout.OriginX = origin.x;
		layout.OriginY = origin.y;
		layout.RowHeight = ImGui::GetTextLineHeight() + rowSpacing;
		layout.CellWidth = Core::MonospaceCellWidth();

		ImGui::PopFont();

		return layout;
	}

	namespace {
		constexpr float kRoundToNearestBoundary = 0.5f;
		constexpr float kTruncateToCharacterUnderPoint = 0.0f;
		constexpr std::size_t kStartOfLine = 0;

		std::size_t ColumnsIn(std::string_view message)
		{
			return Core::ColumnCount(message, Core::DefaultTabWidth);
		}

		std::int64_t LineNumberOf(const Core::LogEntry& entry)
		{
			return static_cast<std::int64_t>(entry.Sequence);
		}

		const Core::LogEntry* FindEntryOnLine(const std::vector<Core::LogEntry>& shown, std::int64_t line)
		{
			for (const Core::LogEntry& entry : shown)
			{
				if (LineNumberOf(entry) == line)
				{
					return &entry;
				}
			}

			return nullptr;
		}

		Core::TextPoint WithByteColumn(const std::vector<Core::LogEntry>& shown, Core::TextPoint at)
		{
			if (const Core::LogEntry* entry = FindEntryOnLine(shown, at.Line))
			{
				at.Column = Core::ByteOfColumn(entry->Message, at.Column, Core::DefaultTabWidth);
			}

			return at;
		}

		std::optional<Core::TextPoint> OnRows(const std::vector<Core::LogEntry>& shown,
											  const ConsoleLayout& layout, float x, float y,
											  float roundingBias)
		{
			if (shown.empty() || layout.RowHeight <= 0.0f || layout.CellWidth <= 0.0f)
			{
				return std::nullopt;
			}

			const float rowsFromOrigin = (y - layout.OriginY) / layout.RowHeight;

			if (rowsFromOrigin < 0.0f)
			{
				return Core::TextPoint{LineNumberOf(shown.front()), kStartOfLine};
			}

			const std::size_t row = static_cast<std::size_t>(rowsFromOrigin);

			if (row >= shown.size())
			{
				return Core::TextPoint{LineNumberOf(shown.back()), ColumnsIn(shown.back().Message)};
			}

			const std::size_t columnsInRow = ColumnsIn(shown[row].Message);
			const float cellsFromOrigin = (x - layout.OriginX) / layout.CellWidth + roundingBias;
			const std::size_t column =
				cellsFromOrigin <= 0.0f ? kStartOfLine
										: std::min(static_cast<std::size_t>(cellsFromOrigin), columnsInRow);

			return Core::TextPoint{LineNumberOf(shown[row]), column};
		}
	}

	std::optional<Core::TextPoint> PointAt(const std::vector<Core::LogEntry>& shown,
										   const ConsoleLayout& layout, float x, float y)
	{
		return OnRows(shown, layout, x, y, kRoundToNearestBoundary);
	}

	std::optional<Core::TextPoint> CharacterAt(const std::vector<Core::LogEntry>& shown,
											   const ConsoleLayout& layout, float x, float y)
	{
		return OnRows(shown, layout, x, y, kTruncateToCharacterUnderPoint);
	}

	std::string SelectedConsoleText(const Core::TextSelection& selection,
									const std::vector<Core::LogEntry>& shown)
	{
		if (selection.IsEmpty())
		{
			return {};
		}

		Core::TextSelection selectionInBytes;
		selectionInBytes.Select(WithByteColumn(shown, selection.First()), WithByteColumn(shown, selection.Last()));

		std::vector<Core::SelectableLine> lines;
		lines.reserve(shown.size());

		for (const Core::LogEntry& entry : shown)
		{
			lines.push_back(Core::SelectableLine{LineNumberOf(entry), entry.Message});
		}

		return Core::JoinSelected(selectionInBytes, lines);
	}

	Core::ColumnSpan ConsoleWordAt(const std::vector<Core::LogEntry>& shown, Core::TextPoint at)
	{
		const Core::LogEntry* entry = FindEntryOnLine(shown, at.Line);

		if (entry == nullptr)
		{
			return Core::ColumnSpan{at.Column, at.Column};
		}

		const std::size_t byte = Core::ByteOfColumn(entry->Message, at.Column, Core::DefaultTabWidth);
		const Core::ColumnSpan wordInBytes = Core::WordSpanAt(entry->Message, byte, Core::WordKind::Code);

		return Core::ColumnSpan{Core::ColumnOfByte(entry->Message, wordInBytes.From, Core::DefaultTabWidth),
								Core::ColumnOfByte(entry->Message, wordInBytes.To, Core::DefaultTabWidth)};
	}

	std::size_t ConsoleLineLength(const std::vector<Core::LogEntry>& shown, std::int64_t line)
	{
		const Core::LogEntry* entry = FindEntryOnLine(shown, line);

		if (entry == nullptr)
		{
			return 0;
		}

		return ColumnsIn(entry->Message);
	}

	std::pair<Core::TextPoint, Core::TextPoint> EverythingShown(const std::vector<Core::LogEntry>& shown)
	{
		if (shown.empty())
		{
			return {};
		}

		const Core::TextPoint start{LineNumberOf(shown.front()), kStartOfLine};
		const Core::TextPoint end{LineNumberOf(shown.back()), ColumnsIn(shown.back().Message)};

		return {start, end};
	}
}
