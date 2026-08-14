#include "Editor/FindNavigation.h"

#include "Search/FileMatches.h"
#include "Search/SearchQuery.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		// The state keeps plain offset pairs, because ADR 0002 stops Core naming DocumentMatch,
		// and the stepping functions are written against the matcher's own type. Copying back
		// happens once per step, never once per frame, so the boundary costs an allocation
		// nobody is in a position to feel.
		std::vector<DocumentMatch> MatchesOf(const Core::FindState& find)
		{
			std::vector<DocumentMatch> matches;

			matches.reserve(find.Matches.size());

			for (const std::pair<std::size_t, std::size_t>& range : find.Matches)
			{
				matches.push_back(DocumentMatch{range.first, range.second});
			}

			return matches;
		}

		// Selects the match rather than only placing the caret at it: the selection is what says
		// which of several highlighted matches is the one being stepped through, and it is
		// already what the user would have had by dragging over the text themselves.
		void GoToMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, std::size_t index)
		{
			if (index >= item.Find.Matches.size())
				return;

			const std::pair<std::size_t, std::size_t> range = item.Find.Matches[index];

			item.Find.Current = index;
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.first, false, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, range.second, true, item.TabWidth);

			// The vertical aim was measured against wherever the caret used to sit; the next Up
			// or Down should resolve it from here instead.
			item.RowAimAt = Core::NoRowAim;

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
			item.Find.Matches.emplace_back(match.From, match.To);
		}

		item.Find.Revision = buffer.GetRevision();

		// Re-derived from the caret rather than merely clamped into the new list: after an edit
		// the index that was current may name an entirely different match, and the caret is
		// where the user is. MatchAtOrAfter answers zero for an empty list, which is the only
		// index an empty list can be asked about.
		item.Find.Current = MatchAtOrAfter(matches, Core::SelectionRange(item.Caret).first);
	}

	bool RefreshFindMatchesIfStale(Core::OpenItem& item, const Core::TextBuffer& buffer)
	{
		if (item.Find.Revision == buffer.GetRevision())
			return false;

		RefreshFindMatches(item, buffer);
		return true;
	}

	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, bool backwards)
	{
		const std::vector<DocumentMatch> matches = MatchesOf(item.Find);

		const std::optional<std::size_t> target = backwards
													  ? PreviousMatch(matches, item.Find.Current)
													  : NextMatch(matches, item.Find.Current);

		if (target)
			GoToMatch(item, buffer, *target);
	}
}
