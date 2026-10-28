#pragma once

#include "Core/Base/Base.h"
#include "Core/Git/LineHistory.h"
#include "Core/Git/TextDiff.h"
#include "Git/Blame/HeadBlame.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Graphite {

	enum class LineAuthorKind : std::uint8_t
	{
		Unknown,
		Committed,
		Uncommitted
	};

	struct LineAuthor
	{
		LineAuthorKind Kind = LineAuthorKind::Unknown;
		std::size_t Hunk = 0;                  // index into Blame::Hunks; meaningful when Committed
		std::optional<std::size_t> HeadLine;   // one-based HEAD line this line is (Committed) or replaced (Uncommitted)
	};

	struct LineAuthors
	{
		Core::Ref<const HeadBlame> Head;
		std::vector<LineAuthor> Lines;         // one per buffer line, zero-based
	};

	LineAuthors BuildLineAuthors(Core::Ref<const HeadBlame> head, std::span<const Core::LineHunk> headToBuffer,
								 std::size_t bufferLineCount);

	// The request that lists this buffer line's commits; empty for an Unknown line or one added since HEAD.
	std::optional<Core::LineHistoryRequest> LineHistoryStart(const LineAuthors& authors, std::size_t bufferLine);
}
