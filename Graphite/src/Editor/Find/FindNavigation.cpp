#include "Editor/Find/FindNavigation.h"

#include "Search/FileMatches.h"
#include "Search/SearchQuery.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	namespace {

		std::vector<DocumentMatch> DocumentMatchesOf(const Core::FindState& find)
		{
			std::vector<DocumentMatch> matches;

			matches.reserve(find.Matches.size());

			for (const Core::FindMatch& range : find.Matches)
			{
				matches.push_back(DocumentMatch{range.From, range.To});
			}

			return matches;
		}

		void SelectMatchAt(Core::OpenItem& item, const Core::TextBuffer& buffer, std::size_t matchIndex)
		{
			if (matchIndex >= item.Find.Matches.size())
			{
				return;
			}

			const Core::FindMatch range = item.Find.Matches[matchIndex];

			item.Find.CurrentMatch = matchIndex;
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.From,
									   Core::SelectionChange::Collapse, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.To, Core::SelectionChange::Extend,
									   item.TabWidth);

			item.RowAimCaretOffset = Core::NoRowAim;
			item.PendingScroll = true;
		}
	}

	void RefreshFindMatches(Core::OpenItem& item, const Core::TextBuffer& buffer)
	{
		SearchQuery query;

		query.Needle = item.Find.Query;
		query.MatchCase = item.Find.MatchCase;
		query.WholeWord = item.Find.WholeWord;

		const std::vector<DocumentMatch> matches = FindInBuffer(buffer, query);

		item.Find.Matches.clear();
		item.Find.Matches.reserve(matches.size());

		for (const DocumentMatch& match : matches)
		{
			item.Find.Matches.push_back(Core::FindMatch{.From = match.From, .To = match.To});
		}

		item.Find.MatchesRevision = buffer.GetRevision();

		item.Find.CurrentMatch = MatchAtOrAfter(matches, Core::SelectionRange(item.Caret).From);
	}

	bool RefreshFindMatchesIfStale(Core::OpenItem& item, const Core::TextBuffer& buffer)
	{
		if (item.Find.MatchesRevision == buffer.GetRevision())
		{
			return false;
		}

		RefreshFindMatches(item, buffer);
		return true;
	}

	// WARNING: Escape makes an ImGui field revert to the value it held when it was activated and
	// report itself edited on that frame, so the edit seen then is ImGui undoing the typing.
	bool AcceptsQueryEdit(const FindFieldEvents& events)
	{
		return events.Edited && !events.EscapePressed;
	}

	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer,
					   StepDirection direction)
	{
		const std::vector<DocumentMatch> matches = DocumentMatchesOf(item.Find);

		const std::optional<std::size_t> target =
			direction == StepDirection::Backwards
				? PreviousMatch(matches, item.Find.CurrentMatch)
				: NextMatch(matches, item.Find.CurrentMatch);

		if (target)
		{
			SelectMatchAt(item, buffer, *target);
		}
	}

	void RevealFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer)
	{
		SelectMatchAt(item, buffer, item.Find.CurrentMatch);
	}
}
