#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Head.h"
#include "Core/Git/Lines/LineQuestions.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// What the history header says a reference is compared with. Lead is dim, Line bright, Counts after them;
	// Lead is empty when the reference has no place in the read, and then there is nothing to say.
	struct VsLabel
	{
		std::string Lead;
		std::string Line;
		std::string Counts;
		std::string How; // "squash" or "rebase" for a branch merged by its contents, drawn dim after the line's name
		std::optional<std::string> Mark;
	};

	struct VsInput
	{
		const LineRead& Read;
		const BranchLines& Lines;
		std::string_view ShownRef; // full name

		// How far the reference is from what it is compared with: the branch's tip against the line's tip, or an old
		// copy's against the base it is a copy of. Empty while it is asked.
		std::optional<Core::LineDistance> Distance;

		// What the check of the branch's contents found, when it has: a branch no commit of the line holds can still be
		// merged, by a squash or a rebase. Empty while it is asked, and when it found nothing or could not tell.
		std::optional<Core::ContentVerdict> Content;
	};

	// Whether the verdict says the shown branch's changes are in its line.
	bool IsMergedByContent(const std::optional<Core::ContentVerdict>& verdict);

	// The short name of the ref at `index` of the read; a remote branch keeps its remote in front.
	std::string LineNameOf(const LineRead& read, std::size_t index);

	// The "behind" count of a distance as the header says it: an ellipsis while the distance is unknown.
	std::string BehindText(const std::optional<Core::LineDistance>& distance);

	VsLabel DescribeVs(const VsInput& input);

	// "rebase in progress", "merge in progress", ...; empty for None.
	std::string HeadOperationText(Core::RepositoryOperation operation);
}
