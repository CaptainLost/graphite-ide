#include "Git/ChangeMarkers.h"

#include <algorithm>
#include <cstddef>
#include <optional>

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
					break;

				if (line < hunk.OldStart + hunk.OldCount)
					return std::nullopt;

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
					return std::nullopt;

				if (end > boundary)
					break;

				shift += Shift(hunk);
			}

			return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(boundary) + shift);
		}
	}

	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount)
	{
		ChangeMarkers markers;
		markers.Lines.resize(bufferLineCount);

		for (const Core::LineHunk& hunk : indexToBuffer)
		{
			if (hunk.NewCount == 0)
			{
				markers.Deletions.push_back({hunk.NewStart, false});
				continue;
			}

			const ChangeKind kind = KindOf(hunk);
			const std::size_t end = std::min(hunk.NewStart + hunk.NewCount, bufferLineCount);

			for (std::size_t line = hunk.NewStart; line < end; ++line)
				markers.Lines[line] = {kind, false};
		}

		for (const Core::LineHunk& hunk : headToIndex)
		{
			if (hunk.NewCount == 0)
			{
				if (const auto at = MapBoundary(indexToBuffer, hunk.NewStart))
					markers.Deletions.push_back({*at, true});

				continue;
			}

			const ChangeKind kind = KindOf(hunk);

			for (std::size_t line = hunk.NewStart; line < hunk.NewStart + hunk.NewCount; ++line)
			{
				const auto at = MapLine(indexToBuffer, line);

				if (at && *at < bufferLineCount && markers.Lines[*at].Kind == ChangeKind::None)
					markers.Lines[*at] = {kind, true};
			}
		}

		// Unstaged first within a line, so the deduplication below keeps it.
		std::ranges::sort(markers.Deletions, [](const DeletionAnchor& a, const DeletionAnchor& b) {
			return a.Line != b.Line ? a.Line < b.Line : (!a.Staged && b.Staged);
		});

		const auto duplicates = std::ranges::unique(markers.Deletions, {}, &DeletionAnchor::Line);
		markers.Deletions.erase(duplicates.begin(), duplicates.end());

		return markers;
	}
}
