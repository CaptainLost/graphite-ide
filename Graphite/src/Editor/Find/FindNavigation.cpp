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
		// The state keeps its own FindMatch ranges, because ADR 0002 stops Core naming DocumentMatch,
		// and the stepping functions are written against the matcher's own type. Copying back
		// happens once per step, never once per frame, so the boundary costs an allocation
		// nobody is in a position to feel.
		std::vector<DocumentMatch> MatchesOf(const Core::FindState& find)
		{
			std::vector<DocumentMatch> matches;

			matches.reserve(find.Matches.size());

			for (const Core::FindMatch& range : find.Matches)
			{
				matches.push_back(DocumentMatch{range.From, range.To});
			}

			return matches;
		}

		// Selects the match rather than only placing the caret at it: the selection is what says
		// which of several highlighted matches is the one being stepped through, and it is
		// already what the user would have had by dragging over the text themselves.
		void GoToMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, std::size_t index)
		{
			if (index >= item.Find.Matches.size())
			{
				return;
			}

			const Core::FindMatch range = item.Find.Matches[index];

			item.Find.CurrentMatch = index;
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.From, Core::SelectionChange::Collapse, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.To, Core::SelectionChange::Extend, item.TabWidth);

			// The vertical aim was measured against wherever the caret used to sit; the next Up
			// or Down should resolve it from here instead.
			item.RowAimCaretOffset = Core::NoRowAim;

			// The editor decides where to scroll before it draws the bar the step came from, so
			// the scroll lands on the next frame. That is the case PendingScroll exists for.
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

		// Re-derived from the caret rather than merely clamped into the new list: after an edit
		// the index that was current may name an entirely different match, and the caret is
		// where the user is. MatchAtOrAfter answers zero for an empty list, which is the only
		// index an empty list can be asked about.
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

	bool AcceptsQueryEdit(const FindFieldEvents& events)
	{
		return events.Edited && !events.EscapePressed;
	}

	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, bool backwards)
	{
		const std::vector<DocumentMatch> matches = MatchesOf(item.Find);

		const std::optional<std::size_t> target = backwards
													  ? PreviousMatch(matches, item.Find.CurrentMatch)
													  : NextMatch(matches, item.Find.CurrentMatch);

		if (target)
		{
			GoToMatch(item, buffer, *target);
		}
	}

	void RevealFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer)
	{
		GoToMatch(item, buffer, item.Find.CurrentMatch);
	}
}
