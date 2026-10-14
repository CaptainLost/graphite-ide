#include "Editor/Navigation/DefinitionNavigation.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <optional>

namespace Graphite {

	namespace {

		bool Precedes(std::size_t line, Utf16Column column, std::size_t otherLine, Utf16Column otherColumn)
		{
			return line != otherLine ? line < otherLine : column.Value < otherColumn.Value;
		}

		bool LocationCoversPosition(const Location& location, const FilePosition& position)
		{
			if (location.Path != position.Path)
			{
				return false;
			}

			if (location.Line == position.Line && location.Column == position.Column)
			{
				return true;
			}

			if (Precedes(position.Line, position.Column, location.Line, location.Column))
			{
				return false;
			}

			return Precedes(position.Line, position.Column, location.EndLine, location.EndColumn);
		}

		bool NamesSameSpot(const Location& existing, const Location& candidate)
		{
			return existing.Path == candidate.Path && existing.Line == candidate.Line &&
				   existing.Column == candidate.Column;
		}

		bool ContainsSpotOf(const std::vector<Location>& locations, const Location& candidate)
		{
			for (const Location& existing : locations)
			{
				if (NamesSameSpot(existing, candidate))
				{
					return true;
				}
			}

			return false;
		}
	}

	DefinitionOutcome ResolveDefinition(const std::vector<Location>& locations, const FilePosition& from,
										std::vector<Location>& candidates)
	{
		candidates.clear();

		for (const Location& location : locations)
		{
			if (LocationCoversPosition(location, from))
			{
				continue;
			}

			if (!ContainsSpotOf(candidates, location))
			{
				candidates.push_back(location);
			}
		}

		if (candidates.empty())
		{
			return DefinitionOutcome::None;
		}

		if (candidates.size() == 1)
		{
			return DefinitionOutcome::Jumped;
		}

		return DefinitionOutcome::NeedsChoice;
	}

	void Jump(Core::DocumentSet& documents, const Location& location)
	{
		documents.Open(location.Path, Core::TabKind::Permanent);

		const std::optional<std::size_t> openIndex = documents.Find(location.Path);

		if (!openIndex)
		{
			return;
		}

		Core::OpenItem& item = documents.At(*openIndex);

		if (!item.Document)
		{
			return;
		}

		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		const std::size_t line = std::min(location.Line, buffer.GetLineCount() - 1);
		const std::string lineText = buffer.LineText(line);
		const std::size_t byteColumn = FromUtf16Column(lineText, location.Column);
		const std::size_t offset = buffer.LineStart(line) + byteColumn;

		item.Caret = Core::PlaceAt(buffer, item.Caret, offset, Core::SelectionChange::Collapse, item.TabWidth);

		// WARNING: The editor did not see the caret move during its own processing this frame,
		// so it must be told to scroll to it.
		item.PendingScroll = true;
	}
}
