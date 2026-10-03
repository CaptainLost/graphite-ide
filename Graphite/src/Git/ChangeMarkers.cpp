#include "Git/ChangeMarkers.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		ChangeKind KindOf(const Core::LineHunk& hunk)
		{
			return hunk.OldCount == 0 ? ChangeKind::Added : ChangeKind::Modified;
		}

		std::ptrdiff_t Shift(const Core::LineHunk& hunk)
		{
			return static_cast<std::ptrdiff_t>(hunk.NewCount) - static_cast<std::ptrdiff_t>(hunk.OldCount);
		}

		// Where index line `line` is in the buffer, or nothing when an unstaged hunk replaced
		// it. `unstaged` is ascending, as DiffTexts returns it.
		std::optional<std::size_t> MapLine(std::span<const Core::LineHunk> unstaged, std::size_t line)
		{
			std::ptrdiff_t shift = 0;

			for (const Core::LineHunk& hunk : unstaged)
			{
				if (line < hunk.OldStart)
				{
					break;
				}

				if (line < hunk.OldStart + hunk.OldCount)
				{
					return std::nullopt;
				}

				shift += Shift(hunk);
			}

			return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(line) + shift);
		}

		// Where the index boundary before line `boundary` is in the buffer. Nothing when an
		// unstaged hunk sits on it: strictly inside the lines it replaced, or an insertion right
		// there. That hunk already marks the spot.
		std::optional<std::size_t> MapBoundary(std::span<const Core::LineHunk> unstaged,
											   std::size_t boundary)
		{
			std::ptrdiff_t shift = 0;

			for (const Core::LineHunk& hunk : unstaged)
			{
				const std::size_t end = hunk.OldStart + hunk.OldCount;

				const bool onIt = hunk.OldCount == 0 ? boundary == hunk.OldStart
													 : boundary > hunk.OldStart && boundary < end;
				if (onIt)
				{
					return std::nullopt;
				}

				if (end > boundary)
				{
					break;
				}

				shift += Shift(hunk);
			}

			return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(boundary) + shift);
		}

		HunkKind HunkKindOf(const Core::LineHunk& hunk)
		{
			if (hunk.NewCount == 0)
			{
				return HunkKind::Deleted;
			}

			return hunk.OldCount == 0 ? HunkKind::Added : HunkKind::Modified;
		}

		// A text's lines without their newlines. A last line with no newline after it is still a
		// line; the empty remainder after a final newline is not one.
		std::vector<std::string_view> SplitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;

			while (start < text.size())
			{
				const std::size_t end = text.find('\n', start);

				if (end == std::string_view::npos)
				{
					lines.push_back(text.substr(start));
					break;
				}

				lines.push_back(text.substr(start, end - start));
				start = end + 1;
			}

			return lines;
		}

		// The first kShownOldLines of the lines a hunk replaced. Clamped to the text, so a hunk
		// computed against a text other than the one handed in yields fewer lines, never a crash.
		std::vector<std::string> ShownOldLines(const std::vector<std::string_view>& lines,
											   const Core::LineHunk& hunk)
		{
			std::vector<std::string> shown;
			const std::size_t end = std::min({hunk.OldStart + hunk.OldCount,
											  hunk.OldStart + kShownOldLines, lines.size()});

			for (std::size_t line = hunk.OldStart; line < end; ++line)
			{
				shown.emplace_back(lines[line]);
			}

			return shown;
		}
	}

	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount, const OldTexts& old)
	{
		ChangeMarkers markers;
		markers.Lines.resize(bufferLineCount);

		// Split only when there is a hunk to read old lines for; an unchanged file, the common
		// case, pays nothing.
		const std::vector<std::string_view> indexLines =
			indexToBuffer.empty() ? std::vector<std::string_view>{} : SplitLines(old.Index);
		const std::vector<std::string_view> headLines =
			headToIndex.empty() ? std::vector<std::string_view>{} : SplitLines(old.Head);

		for (const Core::LineHunk& hunk : indexToBuffer)
		{
			ChangeHunk described{.Kind = HunkKindOf(hunk),
								 .Staged = false,
								 .FirstLine = hunk.NewStart,
								 .OldCount = hunk.OldCount,
								 .OldLines = ShownOldLines(indexLines, hunk)};

			if (hunk.NewCount == 0)
			{
				markers.Deletions.push_back({hunk.NewStart, false});
				markers.Hunks.push_back(std::move(described));
				continue;
			}

			const ChangeKind kind = KindOf(hunk);
			const std::size_t end = std::min(hunk.NewStart + hunk.NewCount, bufferLineCount);

			for (std::size_t line = hunk.NewStart; line < end; ++line)
			{
				markers.Lines[line] = {kind, false};
			}

			described.LineCount = end > hunk.NewStart ? end - hunk.NewStart : 0;
			described.MarkedCount = described.LineCount;
			described.Intact = described.LineCount == hunk.NewCount;
			markers.Hunks.push_back(std::move(described));
		}

		for (const Core::LineHunk& hunk : headToIndex)
		{
			ChangeHunk described{.Kind = HunkKindOf(hunk), .Staged = true, .OldCount = hunk.OldCount};

			if (hunk.NewCount == 0)
			{
				if (const auto at = MapBoundary(indexToBuffer, hunk.NewStart))
				{
					markers.Deletions.push_back({*at, true});

					described.FirstLine = *at;
					described.OldLines = ShownOldLines(headLines, hunk);
					markers.Hunks.push_back(std::move(described));
				}

				continue;
			}

			const ChangeKind kind = KindOf(hunk);
			std::optional<std::size_t> first;
			std::size_t last = 0;
			std::size_t marked = 0;

			for (std::size_t line = hunk.NewStart; line < hunk.NewStart + hunk.NewCount; ++line)
			{
				const auto at = MapLine(indexToBuffer, line);

				if (at && *at < bufferLineCount && markers.Lines[*at].Kind == ChangeKind::None)
				{
					markers.Lines[*at] = {kind, true};

					if (!first)
					{
						first = *at;
					}

					last = *at;
					++marked;
				}
			}

			// Every line rewritten since by an unstaged hunk: nothing of this one is drawn.
			if (!first)
			{
				continue;
			}

			described.FirstLine = *first;
			described.LineCount = last - *first + 1;
			described.MarkedCount = marked;
			described.Intact = marked == hunk.NewCount && described.LineCount == marked;
			described.OldLines = ShownOldLines(headLines, hunk);
			markers.Hunks.push_back(std::move(described));
		}

		// Unstaged first within a line, so the deduplication below keeps it.
		std::ranges::sort(markers.Deletions, [](const DeletionAnchor& a, const DeletionAnchor& b) {
			return a.Line != b.Line ? a.Line < b.Line : (!a.Staged && b.Staged);
		});

		const auto duplicates = std::ranges::unique(markers.Deletions, {}, &DeletionAnchor::Line);
		markers.Deletions.erase(duplicates.begin(), duplicates.end());

		return markers;
	}

	const ChangeHunk* FindLineHunk(const ChangeMarkers& markers, std::size_t line)
	{
		if (line >= markers.Lines.size() || markers.Lines[line].Kind == ChangeKind::None)
		{
			return nullptr;
		}

		const bool staged = markers.Lines[line].Staged;

		for (const ChangeHunk& hunk : markers.Hunks)
		{
			if (hunk.Kind != HunkKind::Deleted && hunk.Staged == staged && line >= hunk.FirstLine &&
				line < hunk.FirstLine + hunk.LineCount)
			{
				return &hunk;
			}
		}

		return nullptr;
	}

	const ChangeHunk* FindDeletionHunk(const ChangeMarkers& markers, std::size_t line)
	{
		// The anchor actually drawn there decides the shade: of two deletions on one boundary,
		// only the unstaged one is drawn, and the tooltip must describe what is on screen.
		const auto drawn = std::ranges::lower_bound(markers.Deletions, line, {}, &DeletionAnchor::Line);

		if (drawn == markers.Deletions.end() || drawn->Line != line)
		{
			return nullptr;
		}

		for (const ChangeHunk& hunk : markers.Hunks)
		{
			if (hunk.Kind == HunkKind::Deleted && hunk.Staged == drawn->Staged && hunk.FirstLine == line)
			{
				return &hunk;
			}
		}

		return nullptr;
	}

	std::string DescribeHunk(const ChangeHunk& hunk)
	{
		const auto lines = [](std::size_t count) {
			return std::to_string(count) + (count == 1 ? " line" : " lines");
		};

		std::string heading;

		switch (hunk.Kind)
		{
		case HunkKind::Added:
			heading = "Added · " + lines(hunk.MarkedCount);
			break;

		case HunkKind::Modified:
			// Only an intact hunk still shows all the lines it introduced; one that unstaged edits
			// have split or partly rewritten would report a replacement count it never made.
			if (hunk.Intact && hunk.OldCount != hunk.MarkedCount)
			{
				heading = "Modified · " + lines(hunk.OldCount) + " replaced by " + std::to_string(hunk.MarkedCount);
			}
			else
			{
				heading = "Modified · " + lines(hunk.MarkedCount);
			}

			break;

		case HunkKind::Deleted:
			heading = "Deleted · " + lines(hunk.OldCount);
			break;
		}

		heading += hunk.Staged ? " · staged" : " · unstaged";

		// Only where old lines follow, so the reader knows which version they are reading.
		if (!hunk.OldLines.empty())
		{
			heading += hunk.Staged ? " (against HEAD)" : " (against the index)";
		}

		return heading;
	}
}
