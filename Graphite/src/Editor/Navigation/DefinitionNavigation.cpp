#include "Editor/Navigation/DefinitionNavigation.h"

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

	void Jump(Core::DocumentSet& documents, const Location& location, const OpenDocument& open)
	{
		static_cast<void>(SelectInDocument(documents, location.Path, location.Line, location.Column, location.Line,
										   location.Column, open));
	}
}
