#pragma once

#include "Git/History/CommitDetails.h"
#include "Git/History/CommitRefs.h"
#include "Theme/GitPalette.h"

#include "Core/Base/Base.h"

#include "Core/Git/Commit.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What a merge brought, as the details show it.
	struct BroughtDetails
	{
		Core::Ref<const Core::LogPage> Page; // null while it is read, and when the read failed
		std::optional<std::string> Failure;
	};

	struct CommitDetailsContent
	{
		InBases Bases;

		// "Squash of feature/x." under the bases line when the commit is the landing of a branch's squash or the copy of one
		// of its commits and the branch is in the read.
		std::optional<std::string> Source;

		// Under the source sentence; drawn whenever the repository has tags, and one line high in every state.
		FirstTagLine FirstTag;
		std::vector<CommitRef> Refs;
		std::optional<BroughtDetails> Brought; // only for a merge

		// Why the commit's diff could not be read; empty while it is read or once it is in.
		std::string DiffFailure;
	};

	struct CommitDetailsOutcome
	{
		bool Closed = false;
		std::optional<Core::Oid> ChosenBrought; // a commit the merge brought, clicked
	};

	// WARNING: stats absent means the diff has not arrived, or failed; its line still draws dimmed so the pane does not change height when it lands.
	CommitDetailsOutcome RenderCommitDetails(const Core::CommitSummary& commit,
											 const std::optional<DiffStats>& stats, bool diffFailed, const std::chrono::time_zone* zone,
											 const GitPalette& palette, const CommitDetailsContent& content);
}
