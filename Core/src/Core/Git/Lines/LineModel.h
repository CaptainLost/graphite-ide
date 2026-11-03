#pragma once

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineMeasure.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Core {

	struct LineRef
	{
		std::string FullName;                        // refs/heads/x, refs/remotes/<remote>/x
		std::string Remote;                          // empty for a local branch
		std::string ShortName;                       // x
		Oid Tip;
		RefKind Kind = RefKind::Local;
		bool IsRemoteDefault = false;
		std::optional<std::string> UpstreamFullName; // local branches only
	};

	enum class BaseReason
	{
		ServerDefault,
		MainLineByUser,
		MainLineGuess,
		AddedByUser,
		Kept,
		MergedLines,
		Authors
	};

	struct LineBase
	{
		std::size_t Ref = 0; // index into the input refs
		BaseReason Reason = BaseReason::ServerDefault;
		LineQualities Qualities;
		bool Active = false; // its tip is within kActiveWindow of Now
	};

	// A server default that is an ancestor of another base: Ref and Of index the input refs.
	struct OldCopy
	{
		std::size_t Ref = 0;
		std::size_t Of = 0;
	};

	enum class ComparedKind
	{
		None,
		Base,
		Branch
	};

	enum class BranchState
	{
		NoOwnCommits,
		MergedByHistory,
		NotMerged,
		NotCompared,
		NoCommonHistory,
		HistoryCutOff,
		TipMissing
	};

	struct BranchLine
	{
		std::size_t Ref = 0;
		ComparedKind Kind = ComparedKind::None;
		std::optional<std::size_t> ComparedWith; // index into the input refs
		bool ByAnswer = false;                   // LineAnswers::Comparisons decided it
		BranchState State = BranchState::NotMerged;
		std::size_t Ahead = 0;                   // commits it has that ComparedWith lacks
		std::optional<std::size_t> SameRowAs;    // its upstream, when the short names match
	};

	struct LineModel
	{
		std::vector<LineBase> Bases; // server defaults or the main line first, then in the order found
		std::vector<OldCopy> OldCopies;
		std::vector<BranchLine> Branches; // every ref that is not a base, in input order
		bool MainLineGuessed = false;
	};

	struct LineModelInput
	{
		const AncestryGraph& Graph;
		std::span<const LineRef> Refs;
		const LineAnswers& Answers;
		std::int64_t Now = 0;
	};

	LineModel ComputeLines(const LineModelInput& input);
}
