#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	struct ConsoleLayout
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float RowHeight = 0.0f;
		float CellWidth = 0.0f;
	};

	std::optional<Core::TextPoint> PointAt(const std::vector<Core::LogEntry>& shown,
										   const ConsoleLayout& layout, float x, float y);

	std::optional<Core::TextPoint> CharacterAt(const std::vector<Core::LogEntry>& shown,
											   const ConsoleLayout& layout, float x, float y);

	std::string SelectedConsoleText(const Core::TextSelection& selection,
									const std::vector<Core::LogEntry>& shown);

	Core::ColumnSpan ConsoleWordAt(const std::vector<Core::LogEntry>& shown, Core::TextPoint at);

	std::size_t ConsoleLineLength(const std::vector<Core::LogEntry>& shown, std::int64_t line);

	std::pair<Core::TextPoint, Core::TextPoint> EverythingShown(const std::vector<Core::LogEntry>& shown);
}
