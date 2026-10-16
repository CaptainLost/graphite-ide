#include "Git/Changes/ChangeMarkers.h"

#include "Core/Base/Strings.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr bool kStaged = true;
		constexpr bool kUnstaged = false;

		ChangeKind ChangeKindOf(const Core::LineHunk& hunk)
		{
			return hunk.OldCount == 0 ? ChangeKind::Added : ChangeKind::Modified;
		}

		std::ptrdiff_t LineShiftOf(const Core::LineHunk& hunk)
		{
			return static_cast<std::ptrdiff_t>(hunk.NewCount) - static_cast<std::ptrdiff_t>(hunk.OldCount);
		}

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

				shift += LineShiftOf(hunk);
			}

			return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(line) + shift);
		}

		std::optional<std::size_t> MapBoundary(std::span<const Core::LineHunk> unstaged,
											   std::size_t boundary)
		{
			std::ptrdiff_t shift = 0;

			for (const Core::LineHunk& hunk : unstaged)
			{
				const std::size_t oldEnd = hunk.OldStart + hunk.OldCount;

				const bool hunkSitsOnBoundary = hunk.OldCount == 0
													? boundary == hunk.OldStart
													: boundary > hunk.OldStart && boundary < oldEnd;
				if (hunkSitsOnBoundary)
				{
					return std::nullopt;
				}

				if (oldEnd > boundary)
				{
					break;
				}

				shift += LineShiftOf(hunk);
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

		std::vector<std::string_view> SplitAtNewlines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t lineStart = 0;

			while (lineStart < text.size())
			{
				const std::size_t newlineOffset = text.find('\n', lineStart);

				if (newlineOffset == std::string_view::npos)
				{
					lines.push_back(text.substr(lineStart));
					break;
				}

				lines.push_back(text.substr(lineStart, newlineOffset - lineStart));
				lineStart = newlineOffset + 1;
			}

			return lines;
		}

		// WARNING: The range is clamped to the text, so a hunk computed against another text
		// yields fewer lines, never an out-of-range read.
		std::vector<std::string> ShownOldLines(const std::vector<std::string_view>& lines,
											   const Core::LineHunk& hunk)
		{
			std::vector<std::string> shown;
			const std::size_t shownEnd = std::min({hunk.OldStart + hunk.OldCount,
												   hunk.OldStart + kShownOldLines, lines.size()});

			for (std::size_t line = hunk.OldStart; line < shownEnd; ++line)
			{
				shown.emplace_back(lines[line]);
			}

			return shown;
		}

		bool DeletionAnchorPrecedes(const DeletionAnchor& anchor, const DeletionAnchor& other)
		{
			if (anchor.Line != other.Line)
			{
				return anchor.Line < other.Line;
			}

			return !anchor.Staged && other.Staged;
		}

		std::string LineCountText(std::size_t count)
		{
			return Core::Counted(count, "line");
		}
	}

	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount, const OldTexts& oldTexts)
	{
		ChangeMarkers markers;
		markers.Lines.resize(bufferLineCount);

		const std::vector<std::string_view> indexLines =
			indexToBuffer.empty() ? std::vector<std::string_view>{} : SplitAtNewlines(oldTexts.Index);
		const std::vector<std::string_view> headLines =
			headToIndex.empty() ? std::vector<std::string_view>{} : SplitAtNewlines(oldTexts.Head);

		for (const Core::LineHunk& hunk : indexToBuffer)
		{
			ChangeHunk described{.Kind = HunkKindOf(hunk),
								 .Staged = kUnstaged,
								 .FirstLine = hunk.NewStart,
								 .OldCount = hunk.OldCount,
								 .OldLines = ShownOldLines(indexLines, hunk)};

			if (hunk.NewCount == 0)
			{
				markers.Deletions.push_back({.Line = hunk.NewStart, .Staged = kUnstaged});
				markers.Hunks.push_back(std::move(described));
				continue;
			}

			const ChangeKind kind = ChangeKindOf(hunk);
			const std::size_t newEnd = std::min(hunk.NewStart + hunk.NewCount, bufferLineCount);

			for (std::size_t line = hunk.NewStart; line < newEnd; ++line)
			{
				markers.Lines[line] = {.Kind = kind, .Staged = kUnstaged};
			}

			described.LineCount = newEnd > hunk.NewStart ? newEnd - hunk.NewStart : 0;
			described.MarkedCount = described.LineCount;
			described.Intact = described.LineCount == hunk.NewCount;
			markers.Hunks.push_back(std::move(described));
		}

		for (const Core::LineHunk& hunk : headToIndex)
		{
			ChangeHunk described{
				.Kind = HunkKindOf(hunk), .Staged = kStaged, .OldCount = hunk.OldCount};

			if (hunk.NewCount == 0)
			{
				const std::optional<std::size_t> boundaryInBuffer =
					MapBoundary(indexToBuffer, hunk.NewStart);

				if (boundaryInBuffer)
				{
					markers.Deletions.push_back({.Line = *boundaryInBuffer, .Staged = kStaged});

					described.FirstLine = *boundaryInBuffer;
					described.OldLines = ShownOldLines(headLines, hunk);
					markers.Hunks.push_back(std::move(described));
				}

				continue;
			}

			const ChangeKind kind = ChangeKindOf(hunk);
			std::optional<std::size_t> firstMarkedLine;
			std::size_t lastMarkedLine = 0;
			std::size_t markedCount = 0;

			for (std::size_t line = hunk.NewStart; line < hunk.NewStart + hunk.NewCount; ++line)
			{
				const std::optional<std::size_t> lineInBuffer = MapLine(indexToBuffer, line);

				if (lineInBuffer && *lineInBuffer < bufferLineCount &&
					markers.Lines[*lineInBuffer].Kind == ChangeKind::None)
				{
					markers.Lines[*lineInBuffer] = {.Kind = kind, .Staged = kStaged};

					if (!firstMarkedLine)
					{
						firstMarkedLine = *lineInBuffer;
					}

					lastMarkedLine = *lineInBuffer;
					++markedCount;
				}
			}

			if (!firstMarkedLine)
			{
				continue;
			}

			described.FirstLine = *firstMarkedLine;
			described.LineCount = lastMarkedLine - *firstMarkedLine + 1;
			described.MarkedCount = markedCount;
			described.Intact = markedCount == hunk.NewCount && described.LineCount == markedCount;
			described.OldLines = ShownOldLines(headLines, hunk);
			markers.Hunks.push_back(std::move(described));
		}

		std::ranges::sort(markers.Deletions, DeletionAnchorPrecedes);

		const std::ranges::subrange<std::vector<DeletionAnchor>::iterator> duplicates =
			std::ranges::unique(markers.Deletions, {}, &DeletionAnchor::Line);
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
		// WARNING: Of two deletions on one boundary only the unstaged anchor is drawn, so the
		// anchor found here decides the shade the tooltip describes.
		const std::vector<DeletionAnchor>::const_iterator drawn =
			std::ranges::lower_bound(markers.Deletions, line, {}, &DeletionAnchor::Line);

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
		std::string heading;

		switch (hunk.Kind)
		{
		case HunkKind::Added:
			heading = "Added · " + LineCountText(hunk.MarkedCount);
			break;

		case HunkKind::Modified:
			// WARNING: Only an intact hunk reports a replacement count; a split or partly rewritten
			// one would report a count it never made.
			if (hunk.Intact && hunk.OldCount != hunk.MarkedCount)
			{
				heading = "Modified · " + LineCountText(hunk.OldCount) + " replaced by " +
						  std::to_string(hunk.MarkedCount);
			}
			else
			{
				heading = "Modified · " + LineCountText(hunk.MarkedCount);
			}

			break;

		case HunkKind::Deleted:
			heading = "Deleted · " + LineCountText(hunk.OldCount);
			break;
		}

		heading += hunk.Staged ? " · staged" : " · unstaged";

		if (!hunk.OldLines.empty())
		{
			heading += hunk.Staged ? " (against HEAD)" : " (against the index)";
		}

		return heading;
	}
}
