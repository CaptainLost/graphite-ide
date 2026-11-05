#pragma once

#include "Theme/GitPalette.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	enum class CommitRefKind
	{
		Head,
		Local,
		Remote,
		Tag
	};

	struct CommitRef
	{
		std::string Name;
		CommitRefKind Kind = CommitRefKind::Local;
	};

	// WARNING: tags are not in this map because the model does not list them.
	using CommitRefMap = std::unordered_map<Core::Oid, std::vector<CommitRef>>;

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches);

	struct RefPillColours
	{
		ImU32 Fill = 0;
		ImU32 Text = 0;
	};

	// "HEAD → name" for the checked-out branch, the name for the rest.
	std::string RefPillText(const CommitRef& ref);

	RefPillColours RefPillColoursOf(CommitRefKind kind, const GitPalette& palette);

	// The pill's width, whole: its text cut to the longest a pill may be, with the padding.
	float RefPillWidth(const CommitRef& ref);

	// Draws the pill at the cursor as one item a text line high, with the full name in its tooltip. `alpha` scales it all.
	void DrawRefPill(const CommitRef& ref, const GitPalette& palette, float alpha = 1.0f);

	std::size_t RefLabelsThatFit(const std::vector<float>& labelWidths, float gap,
								 float availableWidth, float summaryWidth,
								 float minimumSummaryWidth);
}
