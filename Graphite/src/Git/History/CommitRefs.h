#pragma once

#include "Theme/GitPalette.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Tag.h"

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

	using CommitRefMap = std::unordered_map<Core::Oid, std::vector<CommitRef>>;

	// The refs on each commit: its branches, the checked-out one, then the local and the remote ones each by name, a
	// remote's symbolic HEAD left out; then its tags in the order `tags` lists them, newest first as the model does.
	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches, const std::vector<Core::Tag>& tags);

	struct RefPillColours
	{
		ImU32 Fill = 0;
		ImU32 Text = 0;
	};

	// "HEAD → name" for the checked-out branch, the name for the rest.
	std::string RefPillText(const CommitRef& ref);

	RefPillColours RefPillColoursOf(CommitRefKind kind, const GitPalette& palette);

	// A pill cut and measured once, to be laid out and drawn without measuring again.
	struct RefPill
	{
		CommitRefKind Kind = CommitRefKind::Local;
		std::string Full;
		std::string Shown; // Full cut to the longest a pill may be, with an ellipsis
		float Width = 0.0f; // whole, with the padding
		bool IsCut = false;
	};

	RefPill MeasureRefPill(const CommitRef& ref);

	// Draws the pill at the cursor as one item a text line high; its tooltip gives the full text when it was cut. `alpha` scales it all.
	void DrawRefPill(const RefPill& pill, const GitPalette& palette, float alpha = 1.0f);

	std::size_t RefLabelsThatFit(const std::vector<float>& labelWidths, float gap,
								 float availableWidth, float summaryWidth,
								 float minimumSummaryWidth);
}
