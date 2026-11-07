#pragma once

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Oid.h"

#include <optional>
#include <string>

namespace Graphite {

	// What is known about whether a branch is merged into its line by its contents.
	struct ContentMark
	{
		enum class State
		{
			Pending, // not checked yet, or the check runs
			Known,
			Unknown, // the check could not answer: an object it needs is missing, or it failed
		} State = State::Pending;

		std::optional<Core::ContentVerdict> Verdict; // when Known
	};

	// The verdict says the branch's changes are in its line.
	inline bool IsMergedByContent(const std::optional<Core::ContentVerdict>& verdict)
	{
		return verdict.has_value() && verdict->Outcome == Core::ContentOutcome::Merged;
	}

	// The mark is a verdict that the branch's changes are in its line.
	inline bool IsMergedByContent(const ContentMark& mark)
	{
		return mark.State == ContentMark::State::Known && IsMergedByContent(mark.Verdict);
	}

	// The branch a commit's change came from: the commit is the landing of the branch's squash, or the copy of one of the
	// branch's own commits.
	struct ContentSource
	{
		std::string Branch;                                 // full name
		Core::ContentKind Kind = Core::ContentKind::Squash; // Squash for a landing, Rebase for a copy
		std::optional<Core::Oid> Own;                       // the branch's commit a copy is of; empty for a landing

		friend bool operator==(const ContentSource&, const ContentSource&) = default;
	};

	// Whether a line holds a copy of a commit: a commit carrying the same patch.
	struct TwinMark
	{
		enum ContentMark::State State = ContentMark::State::Pending;
		std::optional<Core::Oid> Twin; // when Known and the line holds one
	};

	// Whether a mark holds back what is drawn with it: it is pending, and the model is able to answer. The model checks
	// nothing while the window is off screen, nor for a line of a read it does not follow, so a wait on those would never
	// end; an unknown or failed answer is an answer.
	inline bool ContentCheckWaits(const ContentMark& mark, bool modelChecks)
	{
		return modelChecks && mark.State == ContentMark::State::Pending;
	}

	// What the shown branch's content mark asks of a frame. The view's read is the model's read except in the one frame a
	// new read lands (the sidebar is rebuilt later in it); that frame asks nothing, since the line belongs to the read
	// before, and counts as waiting, so what waits for the mark does not let go a frame early.
	enum class ShownContentStep
	{
		NoWait,      // not a candidate, or the window is not drawn
		WaitForRead, // the read just landed: wait, and ask the model next frame
		AskModel,    // ask the model and wait while its mark is pending
	};

	inline ShownContentStep StepForShownContent(bool isCandidate, bool drawn, bool viewFollowsModelsRead)
	{
		if (!isCandidate)
		{
			return ShownContentStep::NoWait;
		}

		if (!viewFollowsModelsRead)
		{
			return drawn ? ShownContentStep::WaitForRead : ShownContentStep::NoWait;
		}

		return ShownContentStep::AskModel;
	}

}
