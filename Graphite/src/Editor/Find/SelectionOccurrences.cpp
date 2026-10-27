#include "Editor/Find/SelectionOccurrences.h"

#include "Search/FileMatches.h"
#include "Search/SearchQuery.h"

#include "Core/Text/TextBuffer.h"
#include "Core/Text/WordBounds.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool IsWordByteAt(const Core::TextBuffer& buffer, std::size_t offset)
		{
			return Core::IsWordByte(buffer.CharAt(offset));
		}

		bool IsWholeWord(const Core::TextBuffer& buffer, Core::OffsetRange selection)
		{
			if (selection.From >= selection.To)
			{
				return false;
			}

			for (std::size_t offset = selection.From; offset < selection.To; ++offset)
			{
				if (!IsWordByteAt(buffer, offset))
				{
					return false;
				}
			}

			const bool startsOnBoundary = selection.From == 0 || !IsWordByteAt(buffer, selection.From - 1);
			const bool endsOnBoundary = selection.To == buffer.GetLength() || !IsWordByteAt(buffer, selection.To);

			return startsOnBoundary && endsOnBoundary;
		}

		constexpr std::size_t kOnlyTheSelection = 1;

		bool FindShowsMatches(const Core::FindState& find)
		{
			return find.Open && !find.Matches.empty();
		}

		const SelectionOccurrences& NoOccurrences()
		{
			static const SelectionOccurrences none;
			return none;
		}
	}

	SelectionOccurrences FindSelectionOccurrences(const Core::TextBuffer& buffer, Core::OffsetRange selection)
	{
		SelectionOccurrences found;

		if (!IsWholeWord(buffer, selection))
		{
			return found;
		}

		SearchQuery query;

		query.Needle = buffer.Substring(selection.From, selection.To);
		query.MatchCase = true;
		query.WholeWord = true;

		for (const DocumentMatch& match : FindInBuffer(buffer, query))
		{
			found.Matches.push_back(Core::FindMatch{.From = match.From, .To = match.To});

			const std::size_t line = buffer.ToPosition(match.From).Line;

			if (found.Lines.empty() || found.Lines.back() != line)
			{
				found.Lines.push_back(line);
			}
		}

		return found;
	}

	const SelectionOccurrences& OccurrencesToShow(OccurrenceMemo& memo, const Core::TextBuffer& buffer,
												  const Core::OpenItem& item, bool enabled)
	{
		if (!enabled || FindShowsMatches(item.Find))
		{
			return NoOccurrences();
		}

		const Core::OffsetRange selection = Core::SelectionRange(item.Caret);
		const std::size_t revision = buffer.GetRevision();

		if (!memo.Filled || memo.Selection != selection || memo.Revision != revision)
		{
			memo.Selection = selection;
			memo.Revision = revision;
			memo.Filled = true;
			memo.Found = FindSelectionOccurrences(buffer, selection);
		}

		const bool foundElsewhere = memo.Found.Matches.size() > kOnlyTheSelection;

		return foundElsewhere ? memo.Found : NoOccurrences();
	}

	std::vector<OccurrenceMark> BuildOccurrenceOverview(const std::vector<std::size_t>& lines,
														const std::function<float(std::size_t)>& lineTop,
														float minFraction)
	{
		return BuildOccurrenceOverview(lines, lineTop, [&lineTop](std::size_t line) { return lineTop(line + 1); },
									   minFraction);
	}

	std::vector<OccurrenceMark> BuildOccurrenceOverview(const std::vector<std::size_t>& lines,
														const std::function<float(std::size_t)>& lineTop,
														const std::function<float(std::size_t)>& lineEnd,
														float minFraction)
	{
		std::vector<OccurrenceMark> marks;

		const auto endOf = [&](std::size_t line) { return std::max(lineEnd(line), lineTop(line) + minFraction); };
		const auto startsPast = [&](float to, std::size_t line) { return to < lineTop(line); };

		std::size_t first = 0;

		while (first < lines.size())
		{
			OccurrenceMark mark{.From = lineTop(lines[first]), .To = endOf(lines[first])};
			std::size_t last = first;

			// WARNING: Tops and ends both rise with the line, so the lines a mark swallows are one run,
			// found by binary search, and the run's last line is the one that ends it. Visiting every
			// line instead cost time in proportion to the occurrences on every frame.
			for (;;)
			{
				const auto past = std::upper_bound(lines.begin() + static_cast<std::ptrdiff_t>(last + 1), lines.end(),
												   mark.To, startsPast);
				const std::size_t swallowedUpTo = static_cast<std::size_t>(past - lines.begin());

				if (swallowedUpTo == last + 1)
				{
					break;
				}

				last = swallowedUpTo - 1;
				mark.To = std::max(mark.To, endOf(lines[last]));
			}

			// WARNING: Pushed back up rather than cut short, so an occurrence on the last line keeps
			// its height.
			if (mark.To > 1.0f)
			{
				mark.From = std::max(0.0f, mark.From - (mark.To - 1.0f));
				mark.To = 1.0f;
			}

			marks.push_back(mark);
			first = last + 1;
		}

		return marks;
	}
}
